#define _GNU_SOURCE
#include <libgen.h>
#include <limits.h>
#include <quickjs/quickjs-libc.h>
#include <quickjs/quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anyconsole.h"
#include "skin.h"

#define NATIVE_MODULE "native"
#define LIBRARY_DIR SKINS_DIR "/lib"
#define MODULE_EXTENSION ".js"
#define SCRIPT_SECONDS 1.0
#define TIMERS_MAX 256
#define LISTENERS 2

struct timer { int id; double due; JSValue callback; };

static JSRuntime *runtime;
static JSContext *context;
static JSClassID node_class;
static JSValue listeners[LISTENERS];
static struct timer timers[TIMERS_MAX];
static int timer_count, next_timer_id;
static double deadline;
static char skin_folder[PATH_MAX];

static void report(JSContext *ctx) {
    JSValue exception = JS_GetException(ctx), stack = JS_GetPropertyStr(ctx, exception, "stack");
    const char *message = JS_ToCString(ctx, exception), *trace = JS_IsUndefined(stack) ? NULL : JS_ToCString(ctx, stack);
    fprintf(stderr, "skin %s: %s\n%s", skin_folder, message ? message : "unknown error", trace ? trace : "");
    JS_FreeCString(ctx, message);
    if (trace) JS_FreeCString(ctx, trace);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exception);
}

static int interrupted(JSRuntime *rt, void *opaque) {
    (void)rt, (void)opaque;
    return skin_now() > deadline;
}

static void settle_jobs(void) {
    JSContext *job_context;
    for (int done; (done = JS_ExecutePendingJob(runtime, &job_context)) != 0;)
        if (done < 0) report(job_context);
}

static int call(JSValueConst function, int count, JSValueConst *arguments) {
    deadline = skin_now() + SCRIPT_SECONDS;
    JSValue result = JS_Call(context, function, JS_UNDEFINED, count, arguments);
    int truthy = JS_IsException(result) ? (report(context), 0) : JS_ToBool(context, result);
    JS_FreeValue(context, result);
    settle_jobs();
    return truthy > 0;
}

static struct node *node_of(JSValueConst value) { return JS_GetOpaque(value, node_class); }

static void finalize_node(JSRuntime *rt, JSValue value) {
    (void)rt;
    struct node *node = JS_GetOpaque(value, node_class);
    if (node) scene_free(node);
}

static JSValue wrap(JSContext *ctx, struct node *node) {
    JSValue handle = JS_NewObjectClass(ctx, node_class);
    JS_SetOpaque(handle, node);
    return handle;
}

static JSValue native_node(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    const char *type = JS_ToCString(ctx, arguments[0]);
    JSValue handle = wrap(ctx, scene_node(type ? type : ""));
    JS_FreeCString(ctx, type);
    return handle;
}

static JSValue native_root(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count, (void)arguments;
    return wrap(ctx, scene_root());
}

static JSValue native_insert(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    int index;
    JS_ToInt32(ctx, &index, arguments[2]);
    scene_insert(node_of(arguments[0]), node_of(arguments[1]), index);
    return JS_UNDEFINED;
}

static JSValue native_remove(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)ctx, (void)self, (void)count;
    scene_remove(node_of(arguments[0]));
    return JS_UNDEFINED;
}

static JSValue native_text(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    const char *text = JS_ToCString(ctx, arguments[1]);
    scene_text(node_of(arguments[0]), text ? text : "");
    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

static void each_property(JSContext *ctx, JSValueConst object, void (*visit)(JSContext *, struct node *, const char *, JSValueConst), struct node *node) {
    JSPropertyEnum *names;
    uint32_t count;
    if (!JS_IsObject(object) || JS_GetOwnPropertyNames(ctx, &names, &count, object, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) return;
    for (uint32_t i = 0; i < count; i++) {
        const char *name = JS_AtomToCString(ctx, names[i].atom);
        JSValue value = JS_GetProperty(ctx, object, names[i].atom);
        visit(ctx, node, name, value);
        JS_FreeValue(ctx, value);
        JS_FreeCString(ctx, name);
        JS_FreeAtom(ctx, names[i].atom);
    }
    js_free(ctx, names);
}

static double number_at(JSContext *ctx, JSValueConst object, const char *name) {
    double number = 0;
    JSValue value = JS_GetPropertyStr(ctx, object, name);
    if (JS_IsNumber(value)) JS_ToFloat64(ctx, &number, value);
    JS_FreeValue(ctx, value);
    return number;
}

static void set_transition(JSContext *ctx, struct node *node, const char *name, JSValueConst spec) {
    double milliseconds = 0;
    if (JS_IsNumber(spec)) {
        JS_ToFloat64(ctx, &milliseconds, spec);
        scene_transition(node, name, milliseconds, NULL, 0);
        return;
    }
    const char *word = JS_IsString(spec) ? JS_ToCString(ctx, spec) : NULL;
    if (word) {
        scene_transition(node, name, 0, word, !strcmp(word, "spring") ? DEFAULT_STIFFNESS : 0);
        JS_FreeCString(ctx, word);
        return;
    }
    JSValue easing = JS_GetPropertyStr(ctx, spec, "easing");
    const char *curve = JS_IsString(easing) ? JS_ToCString(ctx, easing) : NULL;
    scene_transition(node, name, number_at(ctx, spec, "duration"), curve, number_at(ctx, spec, "spring"));
    if (curve) JS_FreeCString(ctx, curve);
    JS_FreeValue(ctx, easing);
}

static void set_from(JSContext *ctx, struct node *node, const char *name, JSValueConst value) {
    double number = 0;
    const char *text = JS_IsString(value) ? JS_ToCString(ctx, value) : NULL;
    if (!text) JS_ToFloat64(ctx, &number, value);
    scene_from(node, name, text, number);
    if (text) JS_FreeCString(ctx, text);
}

static void set_value(JSContext *ctx, struct node *node, const char *name, JSValueConst value) {
    double number;
    if (JS_IsUndefined(value) || JS_IsNull(value) || (JS_IsBool(value) && !JS_ToBool(ctx, value))) {
        scene_unset(node, name);
    } else if (JS_IsNumber(value) || JS_IsBool(value)) {
        JS_ToFloat64(ctx, &number, value);
        scene_number(node, name, number);
    } else {
        const char *text = JS_ToCString(ctx, value);
        if (text) scene_string(node, name, text);
        JS_FreeCString(ctx, text);
    }
}

static JSValue native_set(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    const char *name = JS_ToCString(ctx, arguments[1]);
    if (name) set_value(ctx, node_of(arguments[0]), name, arguments[2]);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static JSValue native_transition(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    each_property(ctx, arguments[1], set_transition, node_of(arguments[0]));
    return JS_UNDEFINED;
}

static JSValue native_from(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    each_property(ctx, arguments[1], set_from, node_of(arguments[0]));
    return JS_UNDEFINED;
}

static JSValue native_timer(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    double milliseconds = 0;
    JS_ToFloat64(ctx, &milliseconds, arguments[1]);
    if (timer_count == TIMERS_MAX) return JS_ThrowRangeError(ctx, "too many timers");
    timers[timer_count] = (struct timer){++next_timer_id, skin_now() + milliseconds / MS_PER_SECOND, JS_DupValue(ctx, arguments[0])};
    return JS_NewInt32(ctx, timers[timer_count++].id);
}

static void drop_timer(int index) {
    JS_FreeValue(context, timers[index].callback);
    timers[index] = timers[--timer_count];
}

static JSValue native_cancel(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    int id = 0;
    JS_ToInt32(ctx, &id, arguments[0]);
    for (int i = 0; i < timer_count; i++)
        if (timers[i].id == id) drop_timer(i);
    return JS_UNDEFINED;
}

static JSValue native_listen(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    for (int i = 0; i < LISTENERS; i++) {
        JS_FreeValue(ctx, listeners[i]);
        listeners[i] = JS_DupValue(ctx, arguments[i]);
    }
    return JS_UNDEFINED;
}

static JSValue native_select(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    int index = 0;
    JS_ToInt32(ctx, &index, arguments[0]);
    skin_select(index);
    return JS_UNDEFINED;
}

static JSValue native_play(JSContext *ctx, JSValueConst self, int count, JSValueConst *arguments) {
    (void)self, (void)count;
    char resolved[PATH_MAX];
    const char *name = JS_ToCString(ctx, arguments[0]), *path = name ? scene_resolve(name, resolved) : NULL;
    if (path) sound_play(path);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry natives[] = {
    JS_CFUNC_DEF("node", 1, native_node),     JS_CFUNC_DEF("root", 0, native_root),       JS_CFUNC_DEF("insert", 3, native_insert),
    JS_CFUNC_DEF("remove", 1, native_remove), JS_CFUNC_DEF("text", 2, native_text),       JS_CFUNC_DEF("set", 3, native_set),
    JS_CFUNC_DEF("timer", 2, native_timer),   JS_CFUNC_DEF("cancel", 1, native_cancel),   JS_CFUNC_DEF("listen", 2, native_listen),
    JS_CFUNC_DEF("select", 1, native_select), JS_CFUNC_DEF("play", 1, native_play),       JS_CFUNC_DEF("transition", 2, native_transition),
    JS_CFUNC_DEF("from", 2, native_from),
};

static int init_native(JSContext *ctx, JSModuleDef *module) { return JS_SetModuleExportList(ctx, module, natives, sizeof natives / sizeof *natives); }

static char *normalize(JSContext *ctx, const char *base, const char *name, void *opaque) {
    (void)opaque;
    char path[PATH_MAX], directory[PATH_MAX], resolved[PATH_MAX];
    snprintf(directory, sizeof directory, "%s", base);
    if (!strcmp(name, NATIVE_MODULE)) snprintf(path, sizeof path, "%s", name);
    else if (*name == '/') snprintf(path, sizeof path, "%s", name);
    else if (*name == '.') snprintf(path, sizeof path, "%s/%s", dirname(directory), name);
    else snprintf(path, sizeof path, "%s/%s" MODULE_EXTENSION, LIBRARY_DIR, name);
    return js_strdup(ctx, *path == '/' && realpath(path, resolved) ? resolved : path);
}

static void unload(void) {
    if (!runtime) return;
    for (int i = 0; i < LISTENERS; i++) {
        JS_FreeValue(context, listeners[i]);
        listeners[i] = JS_UNDEFINED;
    }
    while (timer_count) drop_timer(0);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    runtime = NULL;
    context = NULL;
}

int script_load(const char *folder) {
    char entry[PATH_MAX];
    unload();
    snprintf(skin_folder, sizeof skin_folder, "%s", folder);
    snprintf(entry, sizeof entry, "%s/" SKIN_ENTRY, folder);
    scene_folder(folder);
    runtime = JS_NewRuntime();
    context = JS_NewContext(runtime);
    for (int i = 0; i < LISTENERS; i++) listeners[i] = JS_UNDEFINED;
    if (!node_class) JS_NewClassID(&node_class);
    JS_NewClass(runtime, node_class, &(JSClassDef){.class_name = "Node", .finalizer = finalize_node});
    JS_SetModuleLoaderFunc2(runtime, normalize, js_module_loader, js_module_check_attributes, NULL);
    JS_SetInterruptHandler(runtime, interrupted, NULL);
    JSModuleDef *module = JS_NewCModule(context, NATIVE_MODULE, init_native);
    JS_AddModuleExportList(context, module, natives, sizeof natives / sizeof *natives);
    deadline = skin_now() + SCRIPT_SECONDS;
    size_t length;
    uint8_t *source = js_load_file(context, &length, entry);
    JSValue result = source ? JS_Eval(context, (const char *)source, length, entry, JS_EVAL_TYPE_MODULE) : JS_ThrowReferenceError(context, "cannot load %s", entry);
    js_free(context, source);
    settle_jobs();
    int failed = JS_IsException(result) || JS_PromiseState(context, result) == JS_PROMISE_REJECTED;
    if (failed && !JS_IsException(result)) JS_Throw(context, JS_PromiseResult(context, result));
    if (failed) report(context);
    JS_FreeValue(context, result);
    return !failed;
}

void script_state(const char *json) {
    if (!runtime || !JS_IsFunction(context, listeners[0])) return;
    JSValue text = JS_NewString(context, json);
    call(listeners[0], 1, &text);
    JS_FreeValue(context, text);
}

int script_command(const char *name) {
    if (!runtime || !JS_IsFunction(context, listeners[1])) return 0;
    JSValue text = JS_NewString(context, name);
    int handled = call(listeners[1], 1, &text);
    JS_FreeValue(context, text);
    return handled;
}

double script_next_timer(void) {
    double due = -1;
    for (int i = 0; i < timer_count; i++)
        if (due < 0 || timers[i].due < due) due = timers[i].due;
    return due;
}

void script_run_timers(double now) {
    for (int i = 0; i < timer_count;) {
        if (timers[i].due > now) {
            i++;
            continue;
        }
        JSValue callback = timers[i].callback;
        timers[i] = timers[--timer_count];
        call(callback, 0, NULL);
        JS_FreeValue(context, callback);
        i = 0;
    }
}
