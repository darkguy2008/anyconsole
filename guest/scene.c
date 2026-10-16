#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_MODULE_H
#include <hb-ft.h>
#include <hb.h>
#include <limits.h>
#include <math.h>
#include <pixman.h>
#include <png.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <turbojpeg.h>
#include <wayland-egl.h>
#include <yoga/Yoga.h>
#include "anyconsole.h"
#include "shader.h"
#include "skin.h"

#define DESIGN_HEIGHT 1080.0f
#define TYPE_MAX 32
#define SDF_SIZE 48
#define SDF_SPREAD 8
#define SDF_EDGE 128.0f
#define BYTE_MAX 255.0f
#define ATLAS_SIZE 2048
#define ATLAS_PAGES_MAX 16
#define GLYPH_PADDING 1
#define FACES_MAX 32
#define SKIN_IMAGES_MAX 256
#define IMAGES_MAX (2 * GAMES_MAX + SKIN_IMAGES_MAX)
#define BLUR_SMOOTHING 4
#define FILTER_SUBSAMPLE_BITS 4
#define IMAGE_LOADS_PER_FRAME 2
#define VARIATION_MAX 128
#define AXES_MAX 16
#define FIXED_ONE 65536.0
#define POSITION_ONE 64.0f
#define TAG_LENGTH 4
#define DEFAULT_FONT_SIZE 32
#define DEFAULT_WEIGHT 400
#define DEFAULT_LINE_HEIGHT 1.25f
#define SPRING_SUBSTEPS 4
#define LONGEST_STEP 0.1
#define PIXEL_PRECISION 0.1f
#define UNIT_PRECISION 0.002f
#define SETTLE_SPEED_RATIO 10.0f
#define MARQUEE_PAUSE 1.5
#define MARQUEE_SPEED 90.0
#define CROSSFADE_SECONDS 0.35
#define CORNERS 4
#define RECT_FLOATS 4
#define AXES 2
#define BLUR_REACH (4 << BLUR_LEVELS)
#define CHANNELS 4
#define FLOATS_PER_GLYPH_VERTEX 4
#define VERTICES_PER_GLYPH 6
#define VISIBLE_ALPHA (1.0f / 512)
#define DEFAULT_ENUM (-1)

enum kind { LENGTH, NUMBER, RATIO, INDEX, COLOR, ENUM, STRING };
enum group { DIMENSION, POSITION_EDGE, MARGIN_EDGE, PADDING_EDGE };
enum prop {
    WIDTH, HEIGHT, MIN_WIDTH, MIN_HEIGHT, MAX_WIDTH, MAX_HEIGHT, FLEX_BASIS, FLEX_GROW, FLEX_SHRINK, ASPECT_RATIO, GAP, ROW_GAP, COLUMN_GAP,
    TOP, LEFT, RIGHT, BOTTOM, MARGIN, MARGIN_TOP, MARGIN_LEFT, MARGIN_RIGHT, MARGIN_BOTTOM, MARGIN_HORIZONTAL, MARGIN_VERTICAL,
    PADDING, PADDING_TOP, PADDING_LEFT, PADDING_RIGHT, PADDING_BOTTOM, PADDING_HORIZONTAL, PADDING_VERTICAL, BORDER_WIDTH,
    OPACITY, TRANSLATE_X, TRANSLATE_Y, SCALE, BORDER_RADIUS, SHADOW_BLUR, SHADOW_OFFSET_X, SHADOW_OFFSET_Y, FONT_SIZE, FONT_WEIGHT, LINE_HEIGHT,
    BACKDROP_BLUR, BLUR, FOLLOW, TRACK, BACKGROUND_COLOR, BORDER_COLOR, COLOR_PROP, SHADOW_COLOR,
    FLEX_DIRECTION, JUSTIFY_CONTENT, ALIGN_ITEMS, ALIGN_SELF, ALIGN_CONTENT, FLEX_WRAP, POSITION, DISPLAY, OVERFLOW, TEXT_ALIGN, TEXT_OVERFLOW, OBJECT_FIT,
    FONT_FAMILY, FONT_VARIATION, SOURCE, PROP_COUNT
};
static const struct { const char *name; enum kind kind; float initial; int inherited; enum group group; YGEdge edge; } props[PROP_COUNT] = {
    [WIDTH] = {"width", LENGTH, NAN}, [HEIGHT] = {"height", LENGTH, NAN}, [MIN_WIDTH] = {"minWidth", LENGTH, NAN}, [MIN_HEIGHT] = {"minHeight", LENGTH, NAN},
    [MAX_WIDTH] = {"maxWidth", LENGTH, NAN}, [MAX_HEIGHT] = {"maxHeight", LENGTH, NAN}, [FLEX_BASIS] = {"flexBasis", LENGTH, NAN},
    [FLEX_GROW] = {"flexGrow", LENGTH, NAN}, [FLEX_SHRINK] = {"flexShrink", LENGTH, NAN}, [ASPECT_RATIO] = {"aspectRatio", LENGTH, NAN},
    [GAP] = {"gap", LENGTH, NAN}, [ROW_GAP] = {"rowGap", LENGTH, NAN}, [COLUMN_GAP] = {"columnGap", LENGTH, NAN},
    [TOP] = {"top", LENGTH, NAN, 0, POSITION_EDGE, YGEdgeTop}, [LEFT] = {"left", LENGTH, NAN, 0, POSITION_EDGE, YGEdgeLeft},
    [RIGHT] = {"right", LENGTH, NAN, 0, POSITION_EDGE, YGEdgeRight}, [BOTTOM] = {"bottom", LENGTH, NAN, 0, POSITION_EDGE, YGEdgeBottom},
    [MARGIN] = {"margin", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeAll}, [MARGIN_TOP] = {"marginTop", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeTop},
    [MARGIN_LEFT] = {"marginLeft", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeLeft}, [MARGIN_RIGHT] = {"marginRight", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeRight},
    [MARGIN_BOTTOM] = {"marginBottom", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeBottom},
    [MARGIN_HORIZONTAL] = {"marginHorizontal", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeHorizontal},
    [MARGIN_VERTICAL] = {"marginVertical", LENGTH, NAN, 0, MARGIN_EDGE, YGEdgeVertical},
    [PADDING] = {"padding", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeAll}, [PADDING_TOP] = {"paddingTop", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeTop},
    [PADDING_LEFT] = {"paddingLeft", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeLeft}, [PADDING_RIGHT] = {"paddingRight", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeRight},
    [PADDING_BOTTOM] = {"paddingBottom", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeBottom},
    [PADDING_HORIZONTAL] = {"paddingHorizontal", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeHorizontal},
    [PADDING_VERTICAL] = {"paddingVertical", LENGTH, NAN, 0, PADDING_EDGE, YGEdgeVertical},
    [BORDER_WIDTH] = {"borderWidth", LENGTH, 0}, [OPACITY] = {"opacity", RATIO, 1}, [TRANSLATE_X] = {"translateX", NUMBER, 0},
    [TRANSLATE_Y] = {"translateY", NUMBER, 0}, [SCALE] = {"scale", RATIO, 1}, [BORDER_RADIUS] = {"borderRadius", NUMBER, 0},
    [SHADOW_BLUR] = {"shadowBlur", NUMBER, 0}, [SHADOW_OFFSET_X] = {"shadowOffsetX", NUMBER, 0}, [SHADOW_OFFSET_Y] = {"shadowOffsetY", NUMBER, 0},
    [FONT_SIZE] = {"fontSize", NUMBER, DEFAULT_FONT_SIZE, 1}, [FONT_WEIGHT] = {"fontWeight", NUMBER, DEFAULT_WEIGHT, 1},
    [LINE_HEIGHT] = {"lineHeight", RATIO, DEFAULT_LINE_HEIGHT, 1}, [BACKDROP_BLUR] = {"backdropBlur", NUMBER, 0}, [BLUR] = {"blur", NUMBER, 0}, [FOLLOW] = {"follow", INDEX}, [TRACK] = {"track", INDEX},
    [BACKGROUND_COLOR] = {"backgroundColor", COLOR}, [BORDER_COLOR] = {"borderColor", COLOR}, [COLOR_PROP] = {"color", COLOR, 1, 1},
    [SHADOW_COLOR] = {"shadowColor", COLOR}, [FLEX_DIRECTION] = {"flexDirection", ENUM}, [JUSTIFY_CONTENT] = {"justifyContent", ENUM},
    [ALIGN_ITEMS] = {"alignItems", ENUM}, [ALIGN_SELF] = {"alignSelf", ENUM}, [ALIGN_CONTENT] = {"alignContent", ENUM},
    [FLEX_WRAP] = {"flexWrap", ENUM}, [POSITION] = {"position", ENUM}, [DISPLAY] = {"display", ENUM}, [OVERFLOW] = {"overflow", ENUM},
    [TEXT_ALIGN] = {"textAlign", ENUM, 0, 1}, [TEXT_OVERFLOW] = {"textOverflow", ENUM, 0, 1}, [OBJECT_FIT] = {"objectFit", ENUM},
    [FONT_FAMILY] = {"fontFamily", STRING, 0, 1}, [FONT_VARIATION] = {"fontVariation", STRING, 0, 1}, [SOURCE] = {"src", STRING},
};
static const char *const text_aligns[] = {"left", "center", "right", NULL};
static const char *const text_overflows[] = {"wrap", "marquee", "clip", NULL};
static const char *const object_fits[] = {"contain", "cover", "fill", NULL};
enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };
enum { WRAP, MARQUEE, CLIP };
enum { CONTAIN, COVER, FILL };
enum motion_kind { JUMP, EASE, SPRING };
enum easing { LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT };
static const char *const easings[] = {"linear", "in", "out", "in-out", NULL};

struct value { float now, target, velocity, origin, from; double started, stepped; char set, has_from, ready, unit; };
struct motion { enum motion_kind kind; float seconds, stiffness; enum easing easing; };
struct glyph { unsigned id; int page, x, y, width, height, left, top; };
struct face {
    char key[PATH_MAX + VARIATION_MAX];
    FT_Face ft;
    hb_font_t *hb;
    struct glyph *glyphs;
    int glyph_count, glyph_capacity;
    unsigned folder_generation;
};
struct image { char key[PATH_MAX]; GLuint texture; int width, height, users, named, loaded, pending; time_t mtime; unsigned long drawn; cairo_surface_t *surface; };
struct decode { struct decode *next; struct image *image; char path[PATH_MAX]; float blur; time_t mtime; cairo_surface_t *picture; };
struct shaped { unsigned id; float advance, x_offset, y_offset; int breakable; };
struct line { int start, end; float width; };
struct node {
    char type[TYPE_MAX];
    int text_node, container;
    struct node *parent, **children;
    int count, capacity;
    YGNodeRef yoga;
    char *text, *strings[PROP_COUNT];
    int enums[PROP_COUNT];
    struct value values[PROP_COUNT][CHANNELS];
    struct motion motions[PROP_COUNT];
    struct face *shaped_face;
    char *shaped_text;
    struct shaped *glyphs;
    int glyph_count;
    struct line *lines;
    int line_count, broken, broken_wrap;
    float broken_available, broken_ratio;
    struct value scroll[AXES], tracked[RECT_FLOATS];
    int tracking;
    struct image *image;
    double marquee_since;
};
struct page { GLuint texture; int x, y, shelf; };
struct transform { float scale, x, y, opacity; int clip[RECT_FLOATS]; };
struct program { GLuint id; GLint rect, viewport, half, radius, border, fill, edge, opacity, textured, blur, ratio, color, region, offset; };

static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLConfig egl_config;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static struct wl_egl_window *egl_window;
static int surface_width, surface_height, loads_left, moving, layout_dirty, decoded_fd;
static unsigned long frame;
static float unit;
static struct node *root;
static YGConfigRef yoga_config;
static YGNodeRef pristine;
static FT_Library library;
static struct face faces[FACES_MAX];
static int face_count;
static unsigned folder_generation;
static struct page pages[ATLAS_PAGES_MAX];
static int page_count;
static struct image images[IMAGES_MAX];
static struct decode *requested, *decoded;
static pthread_mutex_t decode_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t decode_wanted = PTHREAD_COND_INITIALIZER;
static struct program box_program, shadow_program, text_program, *bound;
static GLuint quad_buffer, glyph_buffer, vertex_array, snapshot_texture, snapshot_framebuffer, blur_textures[BLUR_LEVELS + 1], blur_framebuffers[BLUR_LEVELS + 1];
static int blur_width, blur_height;
static float *glyph_vertices;
static size_t glyph_vertex_capacity;
static double crossfade_started = -1, now_seconds;
static char folder[PATH_MAX];
static struct box *blur_boxes;
static int blur_count, blur_capacity;

static const char *vertex_source =
    "#version 300 es\n"
    "in vec2 corner;\n"
    "uniform vec4 rect;\n"
    "uniform vec2 viewport;\n"
    "out vec2 local;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "  vec2 position = rect.xy + corner * rect.zw;\n"
    "  gl_Position = vec4(position / viewport * 2.0 - 1.0, 0.0, 1.0);\n"
    "  gl_Position.y = -gl_Position.y;\n"
    "  local = (corner - 0.5) * rect.zw;\n"
    "  uv = corner;\n"
    "}\n";
static const char *rounded_source =
    "float rounded(vec2 point, vec2 half_size, float radius) {\n"
    "  vec2 q = abs(point) - half_size + radius;\n"
    "  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;\n"
    "}\n";
static const char *box_source =
    "uniform vec2 half_size;\n"
    "uniform vec4 region;\n"
    "uniform float radius, border, opacity;\n"
    "uniform vec4 fill, edge;\n"
    "uniform bool textured;\n"
    "uniform sampler2D image;\n"
    "in vec2 local;\n"
    "in vec2 uv;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "  float distance = rounded(local, half_size, radius);\n"
    "  float coverage = clamp(0.5 - distance, 0.0, 1.0);\n"
    "  vec4 base = fill;\n"
    "  if (textured) base = texture(image, region.xy + uv * region.zw);\n"
    "  float inside = clamp(0.5 - distance - border, 0.0, 1.0);\n"
    "  vec4 color = border > 0.0 ? mix(edge, base, inside) : base;\n"
    "  result = color * coverage * opacity;\n"
    "}\n";
static const char *shadow_source =
    "uniform vec2 half_size, offset;\n"
    "uniform float radius, blur, opacity;\n"
    "uniform vec4 fill;\n"
    "in vec2 local;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "  float distance = rounded(local, half_size, radius), outside = clamp(rounded(local + offset, half_size, radius) + 0.5, 0.0, 1.0);\n"
    "  result = fill * (1.0 - smoothstep(-blur, blur, distance)) * outside * opacity;\n"
    "}\n";
static const char *glyph_vertex_source =
    "#version 300 es\n"
    "in vec4 corner;\n"
    "uniform vec2 viewport;\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "  gl_Position = vec4(corner.xy / viewport * 2.0 - 1.0, 0.0, 1.0);\n"
    "  gl_Position.y = -gl_Position.y;\n"
    "  uv = corner.zw;\n"
    "}\n";
static const char *text_source =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D image;\n"
    "uniform vec4 color;\n"
    "uniform float ratio;\n"
    "in vec2 uv;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "  float distance = (texture(image, uv).r - 128.0 / 255.0) * ratio;\n"
    "  result = color * clamp(distance + 0.5, 0.0, 1.0);\n"
    "}\n";

static struct program link_program(const char *vertex, const char *const *fragments, int count) {
    struct program program = {.id = glCreateProgram()};
    glAttachShader(program.id, compile_shader(GL_VERTEX_SHADER, &vertex, 1));
    glAttachShader(program.id, compile_shader(GL_FRAGMENT_SHADER, fragments, count));
    glBindAttribLocation(program.id, 0, "corner");
    glLinkProgram(program.id);
    program.rect = glGetUniformLocation(program.id, "rect");
    program.viewport = glGetUniformLocation(program.id, "viewport");
    program.half = glGetUniformLocation(program.id, "half_size");
    program.radius = glGetUniformLocation(program.id, "radius");
    program.border = glGetUniformLocation(program.id, "border");
    program.fill = glGetUniformLocation(program.id, "fill");
    program.edge = glGetUniformLocation(program.id, "edge");
    program.opacity = glGetUniformLocation(program.id, "opacity");
    program.textured = glGetUniformLocation(program.id, "textured");
    program.blur = glGetUniformLocation(program.id, "blur");
    program.ratio = glGetUniformLocation(program.id, "ratio");
    program.offset = glGetUniformLocation(program.id, "offset");
    program.color = glGetUniformLocation(program.id, "color");
    program.region = glGetUniformLocation(program.id, "region");
    return program;
}

static void create_programs(void) {
    static const float corners[] = {0, 0, 1, 0, 0, 1, 1, 1};
    const char *header = "#version 300 es\nprecision mediump float;\n";
    box_program = link_program(vertex_source, (const char *[]){header, rounded_source, box_source}, 3);
    shadow_program = link_program(vertex_source, (const char *[]){header, rounded_source, shadow_source}, 3);
    text_program = link_program(glyph_vertex_source, &text_source, 1);
    glGenVertexArrays(1, &vertex_array);
    glBindVertexArray(vertex_array);
    glGenBuffers(1, &quad_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, quad_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
    glGenBuffers(1, &glyph_buffer);
    glEnableVertexAttribArray(0);
}

void scene_init(struct wl_display *display) {
    EGLint count, major, minor;
    static const EGLint config_attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8,
                                               EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    static const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    eglInitialize(egl_display, &major, &minor);
    eglBindAPI(EGL_OPENGL_ES_API);
    eglChooseConfig(egl_display, config_attributes, &egl_config, 1, &count);
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attributes);
    if (!count || egl_context == EGL_NO_CONTEXT) fprintf(stderr, "scene: no GLES 3 context (EGL %d.%d)\n", major, minor);
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context);
    create_programs();
    FT_Init_FreeType(&library);
    FT_Property_Set(library, "bsdf", "spread", &(FT_Int){SDF_SPREAD});
    yoga_config = YGConfigNew();
    YGConfigSetPointScaleFactor(yoga_config, 0);
    pristine = YGNodeNewWithConfig(yoga_config);
}

void scene_attach(struct wl_surface *surface, int width, int height) {
    surface_width = width;
    surface_height = height;
    unit = height / DESIGN_HEIGHT;
    layout_dirty = 1;
    if (egl_window) {
        wl_egl_window_resize(egl_window, width, height, 0, 0);
        return;
    }
    egl_window = wl_egl_window_create(surface, width, height);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    eglSwapInterval(egl_display, 0);
}

void scene_detach(void) {
    if (!egl_window) return;
    eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context);
    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_destroy(egl_window);
    egl_window = NULL;
    egl_surface = EGL_NO_SURFACE;
}

void scene_folder(const char *path) {
    snprintf(folder, sizeof folder, "%s", path);
    folder_generation++;
}

const char *scene_resolve(const char *name, char path[PATH_MAX]) {
    if (*name == '/') return name;
    return snprintf(path, PATH_MAX, "%s/%s", folder, name) < PATH_MAX ? path : NULL;
}

static int find_prop(const char *name) {
    for (int i = 0; i < PROP_COUNT; i++)
        if (!strcmp(props[i].name, name)) return i;
    return -1;
}

static int channels(int prop) { return props[prop].kind == COLOR ? CHANNELS : 1; }

static float precision(int prop) { return props[prop].kind == RATIO || props[prop].kind == COLOR ? UNIT_PRECISION : PIXEL_PRECISION; }

static void mark_text_dirty(struct node *node) {
    if (node && YGNodeHasMeasureFunc(node->yoga)) YGNodeMarkDirty(node->yoga);
    layout_dirty = 1;
}

static float length_of(struct node *node, int prop) { return node->values[prop][0].set ? node->values[prop][0].now : NAN; }

static void apply_dimension(YGNodeRef yoga, int prop, float length, char unit_kind) {
    int percent = unit_kind == '%', automatic = unit_kind == 'a';
    switch (prop) {
    case WIDTH: automatic ? YGNodeStyleSetWidthAuto(yoga) : percent ? YGNodeStyleSetWidthPercent(yoga, length) : YGNodeStyleSetWidth(yoga, length); break;
    case HEIGHT: automatic ? YGNodeStyleSetHeightAuto(yoga) : percent ? YGNodeStyleSetHeightPercent(yoga, length) : YGNodeStyleSetHeight(yoga, length); break;
    case MIN_WIDTH: percent ? YGNodeStyleSetMinWidthPercent(yoga, length) : YGNodeStyleSetMinWidth(yoga, length); break;
    case MIN_HEIGHT: percent ? YGNodeStyleSetMinHeightPercent(yoga, length) : YGNodeStyleSetMinHeight(yoga, length); break;
    case MAX_WIDTH: percent ? YGNodeStyleSetMaxWidthPercent(yoga, length) : YGNodeStyleSetMaxWidth(yoga, length); break;
    case MAX_HEIGHT: percent ? YGNodeStyleSetMaxHeightPercent(yoga, length) : YGNodeStyleSetMaxHeight(yoga, length); break;
    case FLEX_BASIS: automatic ? YGNodeStyleSetFlexBasisAuto(yoga) : percent ? YGNodeStyleSetFlexBasisPercent(yoga, length) : YGNodeStyleSetFlexBasis(yoga, length); break;
    case FLEX_GROW: YGNodeStyleSetFlexGrow(yoga, length); break;
    case FLEX_SHRINK: YGNodeStyleSetFlexShrink(yoga, length); break;
    case ASPECT_RATIO: YGNodeStyleSetAspectRatio(yoga, length); break;
    case GAP: YGNodeStyleSetGap(yoga, YGGutterAll, length); break;
    case ROW_GAP: YGNodeStyleSetGap(yoga, YGGutterRow, length); break;
    case COLUMN_GAP: YGNodeStyleSetGap(yoga, YGGutterColumn, length); break;
    case BORDER_WIDTH: YGNodeStyleSetBorder(yoga, YGEdgeAll, length); break;
    }
}

static void apply_layout(struct node *node, int prop) {
    YGNodeRef yoga = node->yoga;
    YGEdge edge = props[prop].edge;
    float length = length_of(node, prop);
    char unit_kind = node->values[prop][0].unit;
    if (props[prop].kind != LENGTH) return;
    switch (props[prop].group) {
    case DIMENSION: apply_dimension(yoga, prop, length, unit_kind); break;
    case POSITION_EDGE: unit_kind == '%' ? YGNodeStyleSetPositionPercent(yoga, edge, length) : YGNodeStyleSetPosition(yoga, edge, length); break;
    case MARGIN_EDGE:
        unit_kind == 'a' ? YGNodeStyleSetMarginAuto(yoga, edge) : unit_kind == '%' ? YGNodeStyleSetMarginPercent(yoga, edge, length) : YGNodeStyleSetMargin(yoga, edge, length);
        break;
    case PADDING_EDGE: unit_kind == '%' ? YGNodeStyleSetPaddingPercent(yoga, edge, length) : YGNodeStyleSetPadding(yoga, edge, length); break;
    }
    layout_dirty = 1;
}

#define PARSE_YOGA(type, text) ({ \
    int found = DEFAULT_ENUM; \
    for (int option = 0; strcmp(type##ToString((type)option), "unknown"); option++) \
        if (!strcmp(type##ToString((type)option), text)) found = option; \
    found; })

static int parse_words(const char *const *words, const char *text) {
    for (int i = 0; words[i]; i++)
        if (!strcmp(words[i], text)) return i;
    return DEFAULT_ENUM;
}

static int parse_enum(int prop, const char *text) {
    switch (prop) {
    case FLEX_DIRECTION: return PARSE_YOGA(YGFlexDirection, text);
    case JUSTIFY_CONTENT: return PARSE_YOGA(YGJustify, text);
    case ALIGN_ITEMS: case ALIGN_SELF: case ALIGN_CONTENT: return PARSE_YOGA(YGAlign, text);
    case FLEX_WRAP: return PARSE_YOGA(YGWrap, text);
    case POSITION: return PARSE_YOGA(YGPositionType, text);
    case DISPLAY: return PARSE_YOGA(YGDisplay, text);
    case OVERFLOW: return PARSE_YOGA(YGOverflow, text);
    case TEXT_ALIGN: return parse_words(text_aligns, text);
    case TEXT_OVERFLOW: return parse_words(text_overflows, text);
    case OBJECT_FIT: return parse_words(object_fits, text);
    }
    return DEFAULT_ENUM;
}

static void apply_enum(struct node *node, int prop) {
    YGNodeRef yoga = node->yoga;
    int value = node->enums[prop];
    switch (prop) {
    case FLEX_DIRECTION: YGNodeStyleSetFlexDirection(yoga, value < 0 ? YGNodeStyleGetFlexDirection(pristine) : (YGFlexDirection)value); break;
    case JUSTIFY_CONTENT: YGNodeStyleSetJustifyContent(yoga, value < 0 ? YGNodeStyleGetJustifyContent(pristine) : (YGJustify)value); break;
    case ALIGN_ITEMS: YGNodeStyleSetAlignItems(yoga, value < 0 ? YGNodeStyleGetAlignItems(pristine) : (YGAlign)value); break;
    case ALIGN_SELF: YGNodeStyleSetAlignSelf(yoga, value < 0 ? YGNodeStyleGetAlignSelf(pristine) : (YGAlign)value); break;
    case ALIGN_CONTENT: YGNodeStyleSetAlignContent(yoga, value < 0 ? YGNodeStyleGetAlignContent(pristine) : (YGAlign)value); break;
    case FLEX_WRAP: YGNodeStyleSetFlexWrap(yoga, value < 0 ? YGNodeStyleGetFlexWrap(pristine) : (YGWrap)value); break;
    case POSITION: YGNodeStyleSetPositionType(yoga, value < 0 ? YGNodeStyleGetPositionType(pristine) : (YGPositionType)value); break;
    case DISPLAY: YGNodeStyleSetDisplay(yoga, value < 0 ? YGNodeStyleGetDisplay(pristine) : (YGDisplay)value); break;
    case OVERFLOW: YGNodeStyleSetOverflow(yoga, value < 0 ? YGNodeStyleGetOverflow(pristine) : (YGOverflow)value); break;
    default: break;
    }
    layout_dirty = 1;
}

static void mark_subtree_text(struct node *node) {
    mark_text_dirty(node);
    for (int i = 0; i < node->count; i++) mark_subtree_text(node->children[i]);
}

static float inherited(struct node *node, int prop, int channel) {
    for (; node; node = node->parent)
        if (node->values[prop][channel].set) return node->values[prop][channel].now;
    return props[prop].initial;
}

static int inherited_enum(struct node *node, int prop) {
    for (; node; node = node->parent)
        if (node->enums[prop] >= 0) return node->enums[prop];
    return 0;
}

static const char *inherited_string(struct node *node, int prop) {
    for (; node; node = node->parent)
        if (node->strings[prop]) return node->strings[prop];
    return NULL;
}

static float own(struct node *node, int prop, int channel) {
    return node->values[prop][channel].set ? node->values[prop][channel].now : props[prop].initial * (channel == CHANNELS - 1 || props[prop].kind != COLOR);
}

static int parse_hex(const char *text, float color[CHANNELS]) {
    size_t length = strlen(text);
    if (*text != '#' || (length != sizeof "#rrggbb" - 1 && length != sizeof "#rrggbbaa" - 1) || strspn(text + 1, "0123456789abcdefABCDEF") != length - 1) return 0;
    unsigned long packed = strtoul(text + 1, NULL, 16);
    if (length == sizeof "#rrggbb" - 1) packed = packed << 8 | 0xff;
    for (int i = 0; i < CHANNELS; i++) color[i] = (packed >> (8 * (CHANNELS - 1 - i)) & 0xff) / BYTE_MAX;
    return 1;
}

static void target(struct value *value, float goal, char unit_kind) {
    if (value->ready && value->target == goal && value->unit == unit_kind) return;
    if (value->ready) {
        value->origin = value->now;
        value->started = skin_now();
        if (value->velocity == 0) value->stepped = value->started;
    }
    value->target = goal;
    value->unit = unit_kind;
    value->set = 1;
}

void scene_number(struct node *node, const char *name, double number) {
    int prop = find_prop(name);
    if (prop < 0 || props[prop].kind == COLOR || props[prop].kind == ENUM || props[prop].kind == STRING) return;
    target(&node->values[prop][0], number, 0);
    if (props[prop].inherited) mark_subtree_text(node);
    layout_dirty = 1;
}

void scene_string(struct node *node, const char *name, const char *text) {
    int prop = find_prop(name);
    float color[CHANNELS];
    if (prop < 0) return;
    if (props[prop].kind == ENUM) {
        node->enums[prop] = parse_enum(prop, text);
        apply_enum(node, prop);
    } else if (props[prop].kind == STRING) {
        free(node->strings[prop]);
        node->strings[prop] = strdup(text);
    } else if (props[prop].kind == COLOR) {
        if (!strcmp(text, "transparent")) memset(color, 0, sizeof color);
        else if (!parse_hex(text, color)) return;
        for (int i = 0; i < CHANNELS; i++) target(&node->values[prop][i], color[i], 0);
    } else if (props[prop].kind == LENGTH) {
        char *end;
        double number = strtod(text, &end);
        if (!strcmp(text, "auto")) target(&node->values[prop][0], 0, 'a');
        else if (end != text && !strcmp(end, "%")) target(&node->values[prop][0], number, '%');
        else return;
        node->values[prop][0].now = node->values[prop][0].target;
        apply_layout(node, prop);
    }
    if (props[prop].inherited) mark_subtree_text(node);
    layout_dirty = 1;
}

void scene_unset(struct node *node, const char *name) {
    int prop = find_prop(name);
    if (prop < 0) return;
    for (int i = 0; i < channels(prop); i++) node->values[prop][i] = (struct value){0};
    if (props[prop].kind == ENUM) {
        node->enums[prop] = DEFAULT_ENUM;
        apply_enum(node, prop);
    }
    if (props[prop].kind == STRING) {
        free(node->strings[prop]);
        node->strings[prop] = NULL;
    }
    apply_layout(node, prop);
    mark_subtree_text(node);
}

void scene_transition(struct node *node, const char *name, double milliseconds, const char *easing, double stiffness) {
    int prop = find_prop(name);
    if (prop < 0) return;
    int curve = easing ? parse_words(easings, easing) : EASE_OUT;
    node->motions[prop] = stiffness > 0     ? (struct motion){.kind = SPRING, .stiffness = stiffness}
                          : milliseconds > 0 ? (struct motion){.kind = EASE, .seconds = milliseconds / MSEC_PER_SEC, .easing = curve < 0 ? EASE_OUT : curve}
                                             : (struct motion){.kind = JUMP};
}

void scene_from(struct node *node, const char *name, const char *text, double number) {
    int prop = find_prop(name);
    float color[CHANNELS] = {number};
    if (prop < 0 || (text && !parse_hex(text, color))) return;
    for (int i = 0; i < channels(prop); i++) {
        node->values[prop][i].from = color[i];
        node->values[prop][i].has_from = !node->values[prop][i].ready;
    }
}

static float eased(enum easing easing, float t) {
    float inverse = 1 - t;
    switch (easing) {
    case LINEAR: return t;
    case EASE_IN: return t * t * t;
    case EASE_OUT: return 1 - inverse * inverse * inverse;
    case EASE_IN_OUT: return t < 0.5f ? 4 * t * t * t : 1 - 4 * inverse * inverse * inverse;
    }
    return t;
}

static int step_value(struct value *value, struct motion *motion, float tolerance) {
    if (!value->set) return 0;
    if (!value->ready) {
        value->ready = 1;
        value->now = value->origin = value->has_from ? value->from : value->target;
        value->started = value->stepped = now_seconds;
        return 1;
    }
    if (value->now == value->target && value->velocity == 0) return 0;
    if (motion->kind == JUMP) {
        value->now = value->target;
        value->velocity = 0;
        return 1;
    }
    if (motion->kind == EASE) {
        float progress = fminf((now_seconds - value->started) / motion->seconds, 1);
        value->now = progress >= 1 ? value->target : value->origin + (value->target - value->origin) * eased(motion->easing, progress);
        return 1;
    }
    float elapsed = fmin(now_seconds - value->stepped, LONGEST_STEP) / SPRING_SUBSTEPS, damping = 2 * sqrtf(motion->stiffness);
    value->stepped = now_seconds;
    for (int i = 0; i < SPRING_SUBSTEPS; i++) {
        value->velocity += (-motion->stiffness * (value->now - value->target) - damping * value->velocity) * elapsed;
        value->now += value->velocity * elapsed;
    }
    if (fabsf(value->now - value->target) < tolerance && fabsf(value->velocity) < tolerance * SETTLE_SPEED_RATIO) {
        value->now = value->target;
        value->velocity = 0;
    }
    return 1;
}

static int settled(const struct value *value) { return value->now == value->target && value->velocity == 0; }

static void step_node(struct node *node) {
    for (int prop = 0; prop < PROP_COUNT; prop++) {
        if (props[prop].kind == ENUM || props[prop].kind == STRING || props[prop].kind == INDEX) continue;
        int changed = 0;
        for (int i = 0; i < channels(prop); i++) {
            changed |= step_value(&node->values[prop][i], &node->motions[prop], precision(prop));
            moving |= !settled(&node->values[prop][i]);
        }
        if (!changed) continue;
        apply_layout(node, prop);
        if (props[prop].inherited) mark_subtree_text(node);
    }
    for (int i = 0; i < node->count; i++) step_node(node->children[i]);
}

static struct face *open_face(const char *family, const char *variation, float weight) {
    char key[sizeof faces->key], resolved[PATH_MAX];
    const char *path = family ? scene_resolve(family, resolved) : NULL;
    if (!path) return NULL;
    snprintf(key, sizeof key, "%s|%s|%g", path, variation ? variation : "", weight);
    struct face *face = NULL;
    for (int i = 0; i < face_count && !face; i++)
        if (!strcmp(faces[i].key, key)) face = &faces[i];
    if (face && (face->ft || face->folder_generation == folder_generation)) return face->ft ? face : NULL;
    if (!face) {
        if (face_count == FACES_MAX) return NULL;
        face = &faces[face_count++];
        snprintf(face->key, sizeof face->key, "%s", key);
    }
    face->folder_generation = folder_generation;
    if (FT_New_Face(library, path, 0, &face->ft)) {
        fprintf(stderr, "scene: cannot open font %s\n", path);
        face->ft = NULL;
        return NULL;
    }
    FT_Set_Pixel_Sizes(face->ft, 0, SDF_SIZE);
    FT_MM_Var *axes;
    if (!FT_Get_MM_Var(face->ft, &axes)) {
        FT_Fixed coordinates[AXES_MAX];
        FT_UInt count = axes->num_axis < AXES_MAX ? axes->num_axis : AXES_MAX;
        for (FT_UInt i = 0; i < count; i++) {
            char tag[TAG_LENGTH + 1];
            const char *at;
            for (int j = 0; j < TAG_LENGTH; j++) tag[j] = axes->axis[i].tag >> (8 * (TAG_LENGTH - 1 - j));
            tag[TAG_LENGTH] = 0;
            coordinates[i] = axes->axis[i].def;
            if (!strcmp(tag, "wght")) coordinates[i] = weight * FIXED_ONE;
            if (variation && (at = strstr(variation, tag)) && at[TAG_LENGTH] == '=') coordinates[i] = strtod(at + TAG_LENGTH + 1, NULL) * FIXED_ONE;
            if (coordinates[i] < axes->axis[i].minimum) coordinates[i] = axes->axis[i].minimum;
            if (coordinates[i] > axes->axis[i].maximum) coordinates[i] = axes->axis[i].maximum;
        }
        FT_Set_Var_Design_Coordinates(face->ft, count, coordinates);
        FT_Done_MM_Var(library, axes);
    }
    face->hb = hb_ft_font_create_referenced(face->ft);
    return face;
}

static struct glyph *find_glyph(struct face *face, unsigned id) {
    if (face->glyph_count * 2 >= face->glyph_capacity) {
        struct glyph *old = face->glyphs;
        int old_capacity = face->glyph_capacity;
        face->glyph_capacity = old_capacity ? old_capacity * 2 : 256;
        face->glyphs = calloc(face->glyph_capacity, sizeof *face->glyphs);
        for (int i = 0; i < old_capacity; i++) {
            if (!old[i].page) continue;
            int slot = old[i].id % face->glyph_capacity;
            while (face->glyphs[slot].page) slot = (slot + 1) % face->glyph_capacity;
            face->glyphs[slot] = old[i];
        }
        free(old);
    }
    int slot = id % face->glyph_capacity;
    while (face->glyphs[slot].page && face->glyphs[slot].id != id) slot = (slot + 1) % face->glyph_capacity;
    return &face->glyphs[slot];
}

static int place(int width, int height, int *x, int *y) {
    struct page *page = page_count ? &pages[page_count - 1] : NULL;
    if (page && page->x + width > ATLAS_SIZE) {
        page->x = 0;
        page->y += page->shelf;
        page->shelf = 0;
    }
    if (!page || page->y + height > ATLAS_SIZE) {
        if (page_count == ATLAS_PAGES_MAX) return -1;
        page = &pages[page_count++];
        glGenTextures(1, &page->texture);
        glBindTexture(GL_TEXTURE_2D, page->texture);
        unsigned char *blank = calloc(ATLAS_SIZE, ATLAS_SIZE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RED, GL_UNSIGNED_BYTE, blank);
        free(blank);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    *x = page->x;
    *y = page->y;
    page->x += width + GLYPH_PADDING;
    if (height + GLYPH_PADDING > page->shelf) page->shelf = height + GLYPH_PADDING;
    return page_count - 1;
}

static struct glyph *glyph(struct face *face, unsigned id) {
    struct glyph *entry = find_glyph(face, id);
    if (entry->page) return entry;
    *entry = (struct glyph){.id = id, .page = -1};
    face->glyph_count++;
    if (FT_Load_Glyph(face->ft, id, FT_LOAD_NO_HINTING) || FT_Render_Glyph(face->ft->glyph, FT_RENDER_MODE_NORMAL) ||
        FT_Render_Glyph(face->ft->glyph, FT_RENDER_MODE_SDF))
        return entry;
    FT_Bitmap *bitmap = &face->ft->glyph->bitmap;
    int x, y, page = bitmap->width && bitmap->rows ? place(bitmap->width, bitmap->rows, &x, &y) : -1;
    if (page < 0) return entry;
    glBindTexture(GL_TEXTURE_2D, pages[page].texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bitmap->pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, bitmap->width, bitmap->rows, GL_RED, GL_UNSIGNED_BYTE, bitmap->buffer);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    *entry = (struct glyph){id, page + 1, x, y, bitmap->width, bitmap->rows, face->ft->glyph->bitmap_left, face->ft->glyph->bitmap_top};
    return entry;
}

static char *text_of(struct node *node) {
    if (!node->container) return node->text ? strdup(node->text) : strdup("");
    size_t length = 0;
    for (int i = 0; i < node->count; i++)
        if (node->children[i]->text) length += strlen(node->children[i]->text);
    char *joined = calloc(length + 1, 1), *end = joined;
    for (int i = 0; i < node->count; i++)
        if (node->children[i]->text) end = stpcpy(end, node->children[i]->text);
    return joined;
}

static struct face *face_of(struct node *node) {
    return open_face(inherited_string(node, FONT_FAMILY), inherited_string(node, FONT_VARIATION), inherited(node, FONT_WEIGHT, 0));
}

static void shape(struct node *node) {
    struct face *face = face_of(node);
    char *text = text_of(node);
    if (face == node->shaped_face && node->shaped_text && !strcmp(text, node->shaped_text)) {
        free(text);
        return;
    }
    free(node->shaped_text);
    free(node->glyphs);
    free(node->lines);
    node->shaped_text = text;
    node->shaped_face = face;
    node->glyphs = NULL;
    node->lines = NULL;
    node->glyph_count = node->line_count = node->broken = 0;
    if (!face || !*text) return;
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(face->hb, buffer, NULL, 0);
    unsigned count;
    hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);
    node->glyphs = calloc(count, sizeof *node->glyphs);
    node->lines = calloc(count + 1, sizeof *node->lines);
    node->glyph_count = count;
    for (unsigned i = 0; i < count; i++) {
        node->glyphs[i] = (struct shaped){infos[i].codepoint, positions[i].x_advance / POSITION_ONE, positions[i].x_offset / POSITION_ONE,
                                          positions[i].y_offset / POSITION_ONE, i > 0 && text[infos[i - 1].cluster] == ' '};
    }
    hb_buffer_destroy(buffer);
}

static float ratio_of(struct node *node) { return inherited(node, FONT_SIZE, 0) / SDF_SIZE; }

static void break_lines(struct node *node, float available) {
    float ratio = ratio_of(node);
    int wrap = inherited_enum(node, TEXT_OVERFLOW) == WRAP;
    if (node->broken && node->broken_available == available && node->broken_ratio == ratio && node->broken_wrap == wrap) return;
    node->broken = 1;
    node->broken_available = available;
    node->broken_ratio = ratio;
    node->broken_wrap = wrap;
    node->line_count = 0;
    int start = 0, last_break = -1;
    float width = 0, width_at_break = 0;
    for (int i = 0; i < node->glyph_count; i++) {
        float advance = node->glyphs[i].advance * ratio;
        if (node->glyphs[i].breakable) {
            last_break = i;
            width_at_break = width;
        }
        if (wrap && width + advance > available && i > start) {
            int end = last_break > start ? last_break : i;
            node->lines[node->line_count++] = (struct line){start, end, last_break > start ? width_at_break : width};
            width = last_break > start ? width - width_at_break : 0;
            start = end;
            last_break = -1;
        }
        width += advance;
    }
    if (node->glyph_count) node->lines[node->line_count++] = (struct line){start, node->glyph_count, width};
}

static float line_height(struct node *node) { return inherited(node, FONT_SIZE, 0) * inherited(node, LINE_HEIGHT, 0); }

static YGSize measure(YGNodeConstRef yoga, float width, YGMeasureMode width_mode, float height, YGMeasureMode height_mode) {
    (void)height, (void)height_mode;
    struct node *node = YGNodeGetContext(yoga);
    shape(node);
    break_lines(node, width_mode == YGMeasureModeUndefined ? INFINITY : width);
    float widest = 0;
    for (int i = 0; i < node->line_count; i++) widest = fmaxf(widest, node->lines[i].width);
    if (width_mode == YGMeasureModeExactly) widest = width;
    else if (width_mode == YGMeasureModeAtMost) widest = fminf(widest, width);
    return (YGSize){ceilf(widest), node->line_count * line_height(node)};
}

struct node *scene_node(const char *type) {
    struct node *node = calloc(1, sizeof *node);
    snprintf(node->type, sizeof node->type, "%s", type);
    node->text_node = !*type;
    node->container = !strcmp(type, "text");
    for (int i = 0; i < PROP_COUNT; i++) node->enums[i] = DEFAULT_ENUM;
    node->yoga = YGNodeNewWithConfig(yoga_config);
    YGNodeSetContext(node->yoga, node);
    if (node->text_node || node->container) YGNodeSetMeasureFunc(node->yoga, measure);
    return node;
}

static void release_image(struct node *node) {
    if (node->image) node->image->users--;
    node->image = NULL;
}

static void detach(struct node *child) {
    struct node *parent = child->parent;
    if (!parent) return;
    for (int i = 0; i < parent->count; i++) {
        if (parent->children[i] != child) continue;
        memmove(&parent->children[i], &parent->children[i + 1], (parent->count - i - 1) * sizeof *parent->children);
        parent->count--;
        break;
    }
    if (YGNodeGetParent(child->yoga)) YGNodeRemoveChild(parent->yoga, child->yoga);
    child->parent = NULL;
    mark_text_dirty(parent);
}

void scene_free(struct node *node) {
    detach(node);
    release_image(node);
    for (int i = 0; i < node->count; i++) {
        node->children[i]->parent = NULL;
        YGNodeRemoveChild(node->yoga, node->children[i]->yoga);
    }
    if (node == root) root = NULL;
    YGNodeFree(node->yoga);
    for (int i = 0; i < PROP_COUNT; i++) free(node->strings[i]);
    free(node->children);
    free(node->text);
    free(node->shaped_text);
    free(node->glyphs);
    free(node->lines);
    free(node);
}

static int yoga_index(struct node *parent, int index) {
    int position = 0;
    for (int i = 0; i < index && i < parent->count; i++) position += YGNodeGetParent(parent->children[i]->yoga) == parent->yoga;
    return position;
}

void scene_insert(struct node *parent, struct node *child, int index) {
    detach(child);
    if (parent->count == parent->capacity) {
        parent->capacity = parent->capacity ? parent->capacity * 2 : 4;
        parent->children = realloc(parent->children, parent->capacity * sizeof *parent->children);
    }
    if (index > parent->count) index = parent->count;
    memmove(&parent->children[index + 1], &parent->children[index], (parent->count - index) * sizeof *parent->children);
    parent->children[index] = child;
    parent->count++;
    child->parent = parent;
    if (!YGNodeHasMeasureFunc(parent->yoga)) YGNodeInsertChild(parent->yoga, child->yoga, yoga_index(parent, index));
    mark_subtree_text(child);
    mark_text_dirty(parent);
}

void scene_remove(struct node *child) { detach(child); }

void scene_text(struct node *node, const char *text) {
    free(node->text);
    node->text = strdup(text);
    mark_text_dirty(node);
    mark_text_dirty(node->parent);
}

struct node *scene_root(void) {
    root = scene_node("root");
    layout_dirty = 1;
    return root;
}

static struct image *find_image(const char *key) {
    for (int i = 0; i < IMAGES_MAX; i++)
        if (!strcmp(images[i].key, key)) return &images[i];
    return NULL;
}

static struct image *least_recently_drawn(void) {
    struct image *oldest = NULL;
    for (int i = 0; i < IMAGES_MAX; i++)
        if (!images[i].users && !images[i].pending && (!oldest || images[i].drawn < oldest->drawn)) oldest = &images[i];
    return oldest;
}

static struct image *image_entry(const char *key) {
    struct image *image = find_image(key);
    if (image || !(image = least_recently_drawn())) return image;
    glDeleteTextures(1, &image->texture);
    if (image->surface) cairo_surface_destroy(image->surface);
    *image = (struct image){0};
    snprintf(image->key, sizeof image->key, "%s", key);
    return image;
}

void scene_image(const char *name, cairo_surface_t *surface) {
    struct image *image = image_entry(name);
    if (!image) return;
    if (image->surface) cairo_surface_destroy(image->surface);
    image->surface = cairo_surface_reference(surface);
    image->named = 1;
    image->loaded = 0;
    image->users += !image->users;
}

static cairo_surface_t *uploadable(cairo_surface_t *surface) {
    cairo_format_t format = cairo_image_surface_get_format(surface);
    if (format == CAIRO_FORMAT_ARGB32 || format == CAIRO_FORMAT_RGB24) return cairo_surface_reference(surface);
    cairo_surface_t *converted = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cairo_image_surface_get_width(surface), cairo_image_surface_get_height(surface));
    cairo_t *cr = cairo_create(converted);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    return converted;
}

static void upload(struct image *image, cairo_surface_t *source) {
    _Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "cairo's native-endian ARGB32 is uploaded as BGRA bytes");
    cairo_surface_t *surface = uploadable(source);
    cairo_surface_flush(surface);
    image->width = cairo_image_surface_get_width(surface);
    image->height = cairo_image_surface_get_height(surface);
    if (!image->texture) glGenTextures(1, &image->texture);
    glBindTexture(GL_TEXTURE_2D, image->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, CHANNELS);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, cairo_image_surface_get_stride(surface) / CHANNELS);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->width, image->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surface));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, cairo_image_surface_get_format(surface) == CAIRO_FORMAT_RGB24 ? GL_ONE : GL_ALPHA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    cairo_surface_destroy(surface);
}

static unsigned char *read_whole(const char *path, long *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    unsigned char *data = NULL;
    if (!fseek(file, 0, SEEK_END) && (*size = ftell(file)) > 0 && !fseek(file, 0, SEEK_SET) && (data = malloc(*size)) &&
        fread(data, 1, *size, file) != (size_t)*size) {
        free(data);
        data = NULL;
    }
    fclose(file);
    return data;
}

static tjhandle jpeg_header(const unsigned char *data, long size) {
    tjhandle decoder = tj3Init(TJINIT_DECOMPRESS);
    if (!data || tj3DecompressHeader(decoder, data, size)) {
        tj3Destroy(decoder);
        return NULL;
    }
    return decoder;
}

static cairo_surface_t *load_jpeg(const char *path) {
    long size;
    unsigned char *data = read_whole(path, &size);
    cairo_surface_t *surface = NULL;
    tjhandle decoder = jpeg_header(data, size);
    if (decoder) {
        surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, tj3Get(decoder, TJPARAM_JPEGWIDTH), tj3Get(decoder, TJPARAM_JPEGHEIGHT));
        if (tj3Decompress8(decoder, data, size, cairo_image_surface_get_data(surface), cairo_image_surface_get_stride(surface), TJPF_BGRX)) {
            cairo_surface_destroy(surface);
            surface = NULL;
        } else {
            cairo_surface_mark_dirty(surface);
        }
        tj3Destroy(decoder);
    }
    free(data);
    return surface;
}

static cairo_surface_t *load_picture(const char *path) {
    cairo_surface_t *png = cairo_image_surface_create_from_png(path);
    if (!cairo_surface_status(png)) return png;
    cairo_surface_t *jpeg = load_jpeg(path);
    if (!jpeg) {
        fprintf(stderr, "%s: not a PNG or JPEG image\n", path);
        return png;
    }
    cairo_surface_destroy(png);
    return jpeg;
}

static cairo_surface_t *resample(cairo_surface_t *source, int width, int height, pixman_kernel_t reconstruct, pixman_kernel_t sample) {
    cairo_surface_flush(source);
    int source_width = cairo_image_surface_get_width(source), source_height = cairo_image_surface_get_height(source), count;
    pixman_image_t *from = pixman_image_create_bits(cairo_image_surface_get_format(source) == CAIRO_FORMAT_RGB24 ? PIXMAN_x8r8g8b8 : PIXMAN_a8r8g8b8,
                                                    source_width, source_height, (uint32_t *)cairo_image_surface_get_data(source),
                                                    cairo_image_surface_get_stride(source));
    cairo_surface_t *target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    pixman_image_t *to = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, (uint32_t *)cairo_image_surface_get_data(target),
                                                  cairo_image_surface_get_stride(target));
    pixman_fixed_t scale_x = pixman_double_to_fixed((double)source_width / width), scale_y = pixman_double_to_fixed((double)source_height / height);
    pixman_transform_t transform;
    pixman_transform_init_scale(&transform, scale_x, scale_y);
    pixman_fixed_t *filter = pixman_filter_create_separable_convolution(&count, scale_x, scale_y, reconstruct, reconstruct, sample, sample,
                                                                        FILTER_SUBSAMPLE_BITS, FILTER_SUBSAMPLE_BITS);
    pixman_image_set_transform(from, &transform);
    pixman_image_set_filter(from, PIXMAN_FILTER_SEPARABLE_CONVOLUTION, filter, count);
    pixman_image_set_repeat(from, PIXMAN_REPEAT_PAD);
    pixman_image_composite32(PIXMAN_OP_SRC, from, NULL, to, 0, 0, 0, 0, 0, 0, width, height);
    free(filter);
    pixman_image_unref(from);
    pixman_image_unref(to);
    cairo_surface_mark_dirty(target);
    return target;
}

static cairo_surface_t *blurred(cairo_surface_t *picture, float blur) {
    if (cairo_surface_status(picture)) return picture;
    int width = cairo_image_surface_get_width(picture), height = cairo_image_surface_get_height(picture);
    float texels = 1 / (blur * fmaxf(width, height));
    int small_width = fmaxf(1, roundf(width * texels)), small_height = fmaxf(1, roundf(height * texels));
    cairo_surface_t *small = resample(picture, small_width, small_height, PIXMAN_KERNEL_BOX, PIXMAN_KERNEL_GAUSSIAN);
    cairo_surface_t *smooth = resample(small, small_width * BLUR_SMOOTHING, small_height * BLUR_SMOOTHING, PIXMAN_KERNEL_GAUSSIAN, PIXMAN_KERNEL_IMPULSE);
    cairo_surface_destroy(small);
    cairo_surface_destroy(picture);
    return smooth;
}

double scene_aspect(const char *path) {
    png_image png = {.version = PNG_IMAGE_VERSION};
    if (png_image_begin_read_from_file(&png, path)) {
        double aspect = (double)png.width / png.height;
        png_image_free(&png);
        return aspect;
    }
    double aspect = 0;
    long size;
    unsigned char *data = read_whole(path, &size);
    tjhandle decoder = jpeg_header(data, size);
    if (decoder) {
        aspect = (double)tj3Get(decoder, TJPARAM_JPEGWIDTH) / tj3Get(decoder, TJPARAM_JPEGHEIGHT);
        tj3Destroy(decoder);
    }
    free(data);
    return aspect;
}

static struct decode *take_path(const char *path) {
    struct decode *taken = NULL, **link = &requested;
    while (*link) {
        struct decode *job = *link;
        if (strcmp(job->path, path)) {
            link = &job->next;
            continue;
        }
        *link = job->next;
        job->next = taken;
        taken = job;
    }
    return taken;
}

static void *decode_requests(void *unused) {
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&decode_lock);
        while (!requested) pthread_cond_wait(&decode_wanted, &decode_lock);
        struct decode *batch = take_path(requested->path), *last = batch;
        pthread_mutex_unlock(&decode_lock);
        cairo_surface_t *picture = load_picture(batch->path);
        for (struct decode *job = batch; job; last = job, job = job->next)
            job->picture = job->blur > 0 ? blurred(cairo_surface_reference(picture), job->blur) : cairo_surface_reference(picture);
        cairo_surface_destroy(picture);
        pthread_mutex_lock(&decode_lock);
        last->next = decoded;
        decoded = batch;
        pthread_mutex_unlock(&decode_lock);
        eventfd_write(decoded_fd, 1);
    }
    return NULL;
}

static void request_decode(struct image *image, const char *path, float blur, time_t mtime) {
    struct decode *job = calloc(1, sizeof *job);
    job->image = image;
    snprintf(job->path, sizeof job->path, "%s", path);
    job->blur = blur;
    job->mtime = mtime;
    image->pending = 1;
    pthread_mutex_lock(&decode_lock);
    job->next = requested;
    requested = job;
    pthread_cond_signal(&decode_wanted);
    pthread_mutex_unlock(&decode_lock);
}

static void upload_decoded(void) {
    for (; loads_left; loads_left--) {
        pthread_mutex_lock(&decode_lock);
        struct decode *job = decoded;
        if (job) decoded = job->next;
        pthread_mutex_unlock(&decode_lock);
        if (!job) return;
        if (!cairo_surface_status(job->picture)) upload(job->image, job->picture);
        job->image->mtime = job->mtime;
        job->image->loaded = 1;
        job->image->pending = 0;
        cairo_surface_destroy(job->picture);
        free(job);
    }
    pthread_mutex_lock(&decode_lock);
    moving |= decoded != NULL;
    pthread_mutex_unlock(&decode_lock);
}

int scene_start_decoder(void) {
    pthread_t decoder;
    decoded_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    pthread_create(&decoder, NULL, decode_requests, NULL);
    pthread_detach(decoder);
    return decoded_fd;
}

static struct image *node_image(struct node *node) {
    const char *source = node->strings[SOURCE];
    char resolved[PATH_MAX], baked[PATH_MAX];
    struct stat status;
    float blur = own(node, BLUR, 0);
    if (!source) {
        release_image(node);
        return NULL;
    }
    struct image *named = find_image(source);
    int registered = named && named->named, baking = blur > 0 && !registered;
    const char *path = registered ? source : scene_resolve(source, resolved), *key = path;
    if (!path) return NULL;
    if (baking) {
        if (snprintf(baked, sizeof baked, "%s\n%g", path, blur) >= (int)sizeof baked) return NULL;
        key = baked;
    }
    if (!node->image || strcmp(node->image->key, key)) {
        release_image(node);
        node->image = image_entry(key);
        if (node->image) node->image->users++;
    }
    struct image *image = node->image;
    if (!image) return NULL;
    image->drawn = frame;
    if (!image->named && stat(path, &status)) {
        skin_missing(path);
        return NULL;
    }
    if (!image->named && status.st_mtime != image->mtime) image->loaded = 0;
    if (image->loaded) return image;
    if (!image->named) {
        if (!image->pending) request_decode(image, path, baking ? blur : 0, status.st_mtime);
        return image->texture ? image : NULL;
    }
    if (!loads_left) {
        moving = 1;
        return image->texture ? image : NULL;
    }
    loads_left--;
    image->loaded = 1;
    if (!cairo_surface_status(image->surface)) upload(image, image->surface);
    cairo_surface_destroy(image->surface);
    image->surface = NULL;
    return image->texture ? image : NULL;
}

static int visible_color(const float color[CHANNELS], float opacity) { return color[CHANNELS - 1] * opacity > VISIBLE_ALPHA; }

static void premultiply(struct node *node, int prop, float color[CHANNELS]) {
    for (int i = 0; i < CHANNELS; i++) color[i] = prop == COLOR_PROP ? inherited(node, prop, i) : own(node, prop, i);
    for (int i = 0; i < CHANNELS - 1; i++) color[i] *= color[CHANNELS - 1];
}

static void use(struct program *program, GLuint buffer, int floats) {
    if (bound == program) return;
    bound = program;
    glUseProgram(program->id);
    glUniform2f(program->viewport, surface_width, surface_height);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glVertexAttribPointer(0, floats, GL_FLOAT, GL_FALSE, 0, NULL);
}

static void use_rect(struct program *program, float x, float y, float width, float height) {
    use(program, quad_buffer, AXES);
    glUniform4f(program->rect, x, y, width, height);
}

static void intersect(const int clip[RECT_FLOATS], float x, float y, float width, float height, int area[RECT_FLOATS]) {
    int left = floorf(fmaxf(x, clip[0])), top = floorf(fmaxf(y, clip[1]));
    int right = ceilf(fminf(x + width, clip[0] + clip[2])), bottom = ceilf(fminf(y + height, clip[1] + clip[3]));
    area[0] = left, area[1] = top, area[2] = right - left, area[3] = bottom - top;
}

static void scissor(const int clip[RECT_FLOATS]) {
    glScissor(clip[0], surface_height - clip[1] - clip[3], clip[2] > 0 ? clip[2] : 0, clip[3] > 0 ? clip[3] : 0);
}

static float corner_radius(struct node *node, struct transform *at, float width, float height) {
    return fminf(own(node, BORDER_RADIUS, 0) * at->scale, fminf(width, height) / 2);
}

static void box_shape(float x, float y, float width, float height, float radius, const float region[RECT_FLOATS], float opacity) {
    use_rect(&box_program, x, y, width, height);
    glUniform2f(box_program.half, fabsf(width) / 2, fabsf(height) / 2);
    glUniform1f(box_program.radius, radius);
    glUniform4fv(box_program.region, 1, region);
    glUniform1f(box_program.opacity, opacity);
}

static void draw_texture(GLuint texture, float x, float y, float width, float height, float radius, const float region[RECT_FLOATS], float opacity) {
    float clear[CHANNELS] = {0};
    box_shape(x, y, width, height, radius, region, opacity);
    glUniform1f(box_program.border, 0);
    glUniform4fv(box_program.fill, 1, clear);
    glUniform1i(box_program.textured, 1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, CORNERS);
}

static void prepare_blur_targets(void) {
    if (blur_width == surface_width && blur_height == surface_height) return;
    if (!blur_textures[0]) {
        glGenTextures(BLUR_LEVELS + 1, blur_textures);
        glGenFramebuffers(BLUR_LEVELS + 1, blur_framebuffers);
    }
    for (int level = 0; level <= BLUR_LEVELS; level++) {
        attach_texture(blur_textures[level], blur_framebuffers[level], surface_width >> level, surface_height >> level, GL_RGBA8, GL_UNSIGNED_BYTE, GL_LINEAR,
            GL_CLAMP_TO_EDGE);
    }
    blur_width = surface_width;
    blur_height = surface_height;
}

static void blur_pass(int from, int to, const int area[RECT_FLOATS]) {
    static const float upright[RECT_FLOATS] = {0, 1, 1, -1};
    int width = surface_width >> to, height = surface_height >> to, round = (1 << to) - 1, bottom = surface_height - area[1] - area[3];
    glBindFramebuffer(GL_FRAMEBUFFER, blur_framebuffers[to]);
    glViewport(0, 0, width, height);
    glScissor(area[0] >> to, bottom >> to, ((area[0] + area[2] + round) >> to) - (area[0] >> to), ((bottom + area[3] + round) >> to) - (bottom >> to));
    bound = NULL;
    use(&box_program, quad_buffer, AXES);
    glUniform2f(box_program.viewport, width, height);
    draw_texture(blur_textures[from], 0, 0, width, height, 0, upright, 1);
}

static void draw_backdrop(struct node *node, struct transform *at, float width, float height) {
    GLint target;
    int surface[RECT_FLOATS] = {0, 0, surface_width, surface_height}, area[RECT_FLOATS];
    float region[RECT_FLOATS] = {at->x / surface_width, 1 - at->y / surface_height, width / surface_width, -height / surface_height};
    intersect(surface, at->x - BLUR_REACH, at->y - BLUR_REACH, width + 2 * BLUR_REACH, height + 2 * BLUR_REACH, area);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &target);
    prepare_blur_targets();
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, blur_textures[0]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, area[0], surface_height - area[1] - area[3], area[0], surface_height - area[1] - area[3], area[2], area[3]);
    for (int level = 1; level <= BLUR_LEVELS; level++) blur_pass(level - 1, level, area);
    for (int level = BLUR_LEVELS - 1; level >= 0; level--) blur_pass(level + 1, level, area);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glViewport(0, 0, surface_width, surface_height);
    glEnable(GL_BLEND);
    scissor(at->clip);
    bound = NULL;
    draw_texture(blur_textures[0], at->x, at->y, width, height, corner_radius(node, at, width, height), region, at->opacity);
}

static void draw_shadow(struct node *node, struct transform *at, float width, float height) {
    float color[CHANNELS], blur = own(node, SHADOW_BLUR, 0) * at->scale;
    premultiply(node, SHADOW_COLOR, color);
    if (!visible_color(color, at->opacity)) return;
    float reach = 2 * blur, offset_x = own(node, SHADOW_OFFSET_X, 0) * at->scale, offset_y = own(node, SHADOW_OFFSET_Y, 0) * at->scale;
    use_rect(&shadow_program, at->x + offset_x - reach, at->y + offset_y - reach, width + 2 * reach, height + 2 * reach);
    glUniform2f(shadow_program.half, width / 2, height / 2);
    glUniform2f(shadow_program.offset, offset_x, offset_y);
    glUniform1f(shadow_program.radius, corner_radius(node, at, width, height));
    glUniform1f(shadow_program.blur, fmaxf(blur, 1));
    glUniform4fv(shadow_program.fill, 1, color);
    glUniform1f(shadow_program.opacity, at->opacity);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, CORNERS);
}

static void draw_box(struct node *node, struct transform *at, float width, float height) {
    float fill[CHANNELS], edge[CHANNELS], border = own(node, BORDER_WIDTH, 0) * at->scale;
    float x = at->x, y = at->y, region[RECT_FLOATS] = {0, 0, 1, 1};
    struct image *image = !strcmp(node->type, "image") ? node_image(node) : NULL;
    premultiply(node, BACKGROUND_COLOR, fill);
    premultiply(node, BORDER_COLOR, edge);
    if (!image && !visible_color(fill, at->opacity) && !(border > 0 && visible_color(edge, at->opacity))) return;
    if (image && image->texture && image->width && image->height) {
        int fit = node->enums[OBJECT_FIT] < 0 ? CONTAIN : node->enums[OBJECT_FIT];
        float picture = (float)image->width / image->height, frame = width / height;
        if (fit == CONTAIN && picture > frame) y += (height - width / picture) / 2, height = width / picture;
        else if (fit == CONTAIN) x += (width - height * picture) / 2, width = height * picture;
        else if (fit == COVER && picture > frame) region[0] = (1 - frame / picture) / 2, region[2] = frame / picture;
        else if (fit == COVER) region[1] = (1 - picture / frame) / 2, region[3] = picture / frame;
    }
    box_shape(x, y, width, height, corner_radius(node, at, width, height), region, at->opacity);
    glUniform1f(box_program.border, border);
    glUniform4fv(box_program.fill, 1, fill);
    glUniform4fv(box_program.edge, 1, edge);
    glUniform1i(box_program.textured, image && image->texture);
    if (image && image->texture) glBindTexture(GL_TEXTURE_2D, image->texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, CORNERS);
}

static float marquee_offset(struct node *node, float overflow) {
    if (overflow <= 0) {
        node->marquee_since = 0;
        return 0;
    }
    if (!node->marquee_since) node->marquee_since = now_seconds;
    moving = 1;
    double travel = overflow / MARQUEE_SPEED, period = 2 * (MARQUEE_PAUSE + travel), phase = fmod(now_seconds - node->marquee_since, period);
    if (phase < MARQUEE_PAUSE) return 0;
    if (phase < MARQUEE_PAUSE + travel) return overflow * eased(EASE_IN_OUT, (phase - MARQUEE_PAUSE) / travel);
    if (phase < 2 * MARQUEE_PAUSE + travel) return overflow;
    return overflow * (1 - eased(EASE_IN_OUT, (phase - 2 * MARQUEE_PAUSE - travel) / travel));
}

static void draw_text(struct node *node, struct transform *at, float width) {
    float color[CHANNELS];
    struct face *face = node->shaped_face;
    premultiply(node, COLOR_PROP, color);
    if (!face || !node->glyph_count || !visible_color(color, at->opacity)) return;
    int overflow_mode = inherited_enum(node, TEXT_OVERFLOW), align = inherited_enum(node, TEXT_ALIGN);
    float content_width = width / at->scale - YGNodeLayoutGetPadding(node->yoga, YGEdgeLeft) - YGNodeLayoutGetPadding(node->yoga, YGEdgeRight);
    break_lines(node, overflow_mode == WRAP ? content_width : INFINITY);
    float ratio = ratio_of(node), height = line_height(node), ascent = face->ft->size->metrics.ascender / POSITION_ONE * ratio;
    float descent = -face->ft->size->metrics.descender / POSITION_ONE * ratio, scale = at->scale;
    float shift = overflow_mode == MARQUEE && node->line_count ? marquee_offset(node, node->lines[0].width - content_width) : 0;
    size_t needed = (size_t)node->glyph_count * VERTICES_PER_GLYPH * FLOATS_PER_GLYPH_VERTEX;
    if (needed > glyph_vertex_capacity) glyph_vertices = realloc(glyph_vertices, (glyph_vertex_capacity = needed) * sizeof *glyph_vertices);
    float left = YGNodeLayoutGetPadding(node->yoga, YGEdgeLeft), top = YGNodeLayoutGetPadding(node->yoga, YGEdgeTop);
    use(&text_program, glyph_buffer, FLOATS_PER_GLYPH_VERTEX);
    glUniform4fv(text_program.color, 1, color);
    glUniform1f(text_program.ratio, BYTE_MAX / SDF_EDGE * SDF_SPREAD * ratio * scale);
    for (int pass = 0; pass <= page_count; pass++) {
        size_t used = 0;
        for (int l = 0; l < node->line_count; l++) {
            struct line *line = &node->lines[l];
            float pen = left + (align == ALIGN_CENTER ? (content_width - line->width) / 2 : align == ALIGN_RIGHT ? content_width - line->width : 0) - shift;
            if (overflow_mode != WRAP && line->width > content_width) pen = left - shift;
            float baseline = top + l * height + (height - ascent - descent) / 2 + ascent;
            for (int i = line->start; i < line->end; i++) {
                struct shaped *shaped = &node->glyphs[i];
                struct glyph *entry = glyph(face, shaped->id);
                if (entry->page - 1 == pass && entry->width) {
                    float x0 = at->x + (pen + (entry->left + shaped->x_offset) * ratio) * scale;
                    float y0 = at->y + (baseline - (entry->top + shaped->y_offset) * ratio) * scale;
                    float x1 = x0 + entry->width * ratio * scale, y1 = y0 + entry->height * ratio * scale;
                    float u0 = (float)entry->x / ATLAS_SIZE, v0 = (float)entry->y / ATLAS_SIZE;
                    float u1 = (float)(entry->x + entry->width) / ATLAS_SIZE, v1 = (float)(entry->y + entry->height) / ATLAS_SIZE;
                    float quad[] = {x0, y0, u0, v0, x1, y0, u1, v0, x0, y1, u0, v1, x1, y0, u1, v0, x1, y1, u1, v1, x0, y1, u0, v1};
                    memcpy(glyph_vertices + used, quad, sizeof quad);
                    used += sizeof quad / sizeof *quad;
                }
                pen += shaped->advance * ratio;
            }
        }
        if (!used) continue;
        glBindTexture(GL_TEXTURE_2D, pages[pass].texture);
        glBufferData(GL_ARRAY_BUFFER, used * sizeof *glyph_vertices, glyph_vertices, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, used / FLOATS_PER_GLYPH_VERTEX);
    }
}

static int index_value(struct node *node, int prop) { return node->values[prop][0].set ? node->values[prop][0].target : -1; }

static struct node *content_child(struct node *parent, int index) {
    for (int i = 0; parent && index >= 0 && i < parent->count; i++)
        if (!parent->children[i]->values[TRACK][0].set && !index--) return parent->children[i];
    return NULL;
}

static void follow_motion(struct node *node, int prop, struct value *values, int count, const float goals[]) {
    struct motion *motion = node->motions[prop].kind == JUMP ? &(struct motion){.kind = SPRING, .stiffness = DEFAULT_STIFFNESS} : &node->motions[prop];
    for (int i = 0; i < count; i++) {
        target(&values[i], goals[i], 0);
        step_value(&values[i], motion, PIXEL_PRECISION);
        moving |= !settled(&values[i]);
    }
}

static void track(struct node *node) {
    struct node *sibling = content_child(node->parent, index_value(node, TRACK));
    node->tracking = sibling != NULL;
    if (!sibling) return;
    YGNodeRef yoga = sibling->yoga;
    float left = YGNodeLayoutGetMargin(node->yoga, YGEdgeLeft), top = YGNodeLayoutGetMargin(node->yoga, YGEdgeTop);
    float goals[RECT_FLOATS] = {YGNodeLayoutGetLeft(yoga) + left, YGNodeLayoutGetTop(yoga) + top,
                                YGNodeLayoutGetWidth(yoga) - left - YGNodeLayoutGetMargin(node->yoga, YGEdgeRight),
                                YGNodeLayoutGetHeight(yoga) - top - YGNodeLayoutGetMargin(node->yoga, YGEdgeBottom)};
    follow_motion(node, TRACK, node->tracked, RECT_FLOATS, goals);
}

static float leading(YGNodeRef yoga, int vertical) { return vertical ? YGNodeLayoutGetTop(yoga) : YGNodeLayoutGetLeft(yoga); }

static float extent(YGNodeRef yoga, int vertical) { return vertical ? YGNodeLayoutGetHeight(yoga) : YGNodeLayoutGetWidth(yoga); }

static float trailing(YGNodeRef yoga, int vertical) { return leading(yoga, vertical) + extent(yoga, vertical); }

static float row_start(struct node *node, int axis, float earliest) {
    float padding = YGNodeLayoutGetPadding(node->yoga, axis ? YGEdgeTop : YGEdgeLeft);
    for (int i = 0; i < node->count; i++) {
        struct node *child = node->children[i];
        float start = leading(child->yoga, axis) - padding;
        if (!child->values[TRACK][0].set && start >= earliest) return start;
    }
    return earliest;
}

static void follow(struct node *node) {
    struct node *followed = content_child(node, index_value(node, FOLLOW));
    if (!followed) return;
    YGNodeRef child = followed->yoga, yoga = node->yoga;
    float view_width = YGNodeLayoutGetWidth(yoga), view_height = YGNodeLayoutGetHeight(yoga);
    float padding_x = YGNodeLayoutGetPadding(yoga, YGEdgeLeft), padding_y = YGNodeLayoutGetPadding(yoga, YGEdgeTop);
    float start[] = {YGNodeLayoutGetLeft(child) - padding_x, YGNodeLayoutGetTop(child) - padding_y};
    float end[] = {start[0] + YGNodeLayoutGetWidth(child) + 2 * padding_x, start[1] + YGNodeLayoutGetHeight(child) + 2 * padding_y};
    float view[] = {view_width, view_height}, goals[AXES];
    for (int axis = 0; axis < AXES; axis++) {
        goals[axis] = node->scroll[axis].set ? node->scroll[axis].target : 0;
        if (start[axis] < goals[axis]) goals[axis] = start[axis];
        if (end[axis] > goals[axis] + view[axis]) goals[axis] = row_start(node, axis, end[axis] - view[axis]);
    }
    follow_motion(node, FOLLOW, node->scroll, AXES, goals);
}

int scene_page(struct node *list, int selected, int direction) {
    struct node *from = content_child(list, selected);
    if (!from) return -1;
    YGFlexDirection flow = YGNodeStyleGetFlexDirection(list->yoga);
    int rows_flow = flow == YGFlexDirectionRow || flow == YGFlexDirectionRowReverse;
    int wraps = YGNodeStyleGetFlexWrap(list->yoga) != YGWrapNoWrap;
    int vertical = rows_flow == wraps;
    float reach = extent(list->yoga, vertical) - YGNodeLayoutGetPadding(list->yoga, vertical ? YGEdgeTop : YGEdgeLeft) -
                  YGNodeLayoutGetPadding(list->yoga, vertical ? YGEdgeBottom : YGEdgeRight);
    int position = 0, index = selected, target = selected, neighbour = -1;
    while (list->children[position] != from) position++;
    for (position += direction; position >= 0 && position < list->count; position += direction) {
        YGNodeRef child = list->children[position]->yoga;
        if (list->children[position]->values[TRACK][0].set) continue;
        index += direction;
        if (leading(child, !vertical) >= trailing(from->yoga, !vertical) || trailing(child, !vertical) <= leading(from->yoga, !vertical)) continue;
        if (neighbour < 0) neighbour = index;
        if (fmaxf(trailing(child, vertical), trailing(from->yoga, vertical)) - fminf(leading(child, vertical), leading(from->yoga, vertical)) > reach) break;
        target = index;
    }
    return target == selected && neighbour >= 0 ? neighbour : target;
}

static void place_node(struct node *node) {
    if (YGNodeStyleGetOverflow(node->yoga) != YGOverflowVisible) follow(node);
    track(node);
    for (int i = 0; i < node->count; i++) place_node(node->children[i]);
}

static void add_blur(float x, float y, float width, float height) {
    if (blur_count == blur_capacity) blur_boxes = realloc(blur_boxes, (blur_capacity = blur_capacity ? blur_capacity * 2 : 4) * sizeof *blur_boxes);
    blur_boxes[blur_count++] = (struct box){x, y, width, height};
}

static void draw_node(struct node *node, struct transform parent, float scroll_x, float scroll_y) {
    YGNodeRef yoga = node->yoga;
    if (YGNodeStyleGetDisplay(yoga) == YGDisplayNone || (node->text_node && node->parent && node->parent->container)) return;
    float width = YGNodeLayoutGetWidth(yoga), height = YGNodeLayoutGetHeight(yoga), scale = own(node, SCALE, 0);
    float left = YGNodeLayoutGetLeft(yoga), top = YGNodeLayoutGetTop(yoga);
    if (node->tracking) {
        left = node->tracked[0].now, top = node->tracked[1].now;
        if (!node->values[WIDTH][0].set) width = node->tracked[2].now;
        if (!node->values[HEIGHT][0].set) height = node->tracked[3].now;
    }
    float x0 = left + own(node, TRANSLATE_X, 0) - scroll_x, y0 = top + own(node, TRANSLATE_Y, 0) - scroll_y;
    struct transform at = {.scale = parent.scale * scale, .x = parent.x + parent.scale * (x0 + width / 2 - scale * width / 2),
                           .y = parent.y + parent.scale * (y0 + height / 2 - scale * height / 2), .opacity = parent.opacity * own(node, OPACITY, 0)};
    memcpy(at.clip, parent.clip, sizeof at.clip);
    if (at.opacity <= VISIBLE_ALPHA) return;
    float pixel_width = width * at.scale, pixel_height = height * at.scale;
    int shown[RECT_FLOATS];
    intersect(at.clip, at.x, at.y, pixel_width, pixel_height, shown);
    int visible = shown[2] > 0 && shown[3] > 0;
    draw_shadow(node, &at, pixel_width, pixel_height);
    if (visible) {
        if (own(node, BACKDROP_BLUR, 0) > 0) {
            add_blur(at.x, at.y, pixel_width, pixel_height);
            draw_backdrop(node, &at, pixel_width, pixel_height);
        }
        draw_box(node, &at, pixel_width, pixel_height);
    }
    if (visible && (node->text_node || node->container)) {
        scissor(inherited_enum(node, TEXT_OVERFLOW) != WRAP ? shown : at.clip);
        draw_text(node, &at, pixel_width);
        scissor(at.clip);
    }
    if (YGNodeStyleGetOverflow(yoga) != YGOverflowVisible) {
        intersect(parent.clip, at.x, at.y, pixel_width, pixel_height, at.clip);
        scissor(at.clip);
    }
    for (int i = 0; i < node->count; i++) draw_node(node->children[i], at, node->scroll[0].now, node->scroll[1].now);
    scissor(parent.clip);
}

static void draw_snapshot(float opacity) {
    static const float whole[RECT_FLOATS] = {0, 0, 1, 1};
    draw_texture(snapshot_texture, 0, surface_height, surface_width, -surface_height, 0, whole, opacity);
}

static void render(double now) {
    struct transform base = {unit, 0, 0, 1, {0, 0, surface_width, surface_height}};
    now_seconds = now;
    loads_left = IMAGE_LOADS_PER_FRAME;
    moving = 0;
    frame++;
    bound = NULL;
    blur_count = 0;
    glViewport(0, 0, surface_width, surface_height);
    glClearColor(0, 0, 0, 0);
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    if (!root) return;
    upload_decoded();
    step_node(root);
    if (layout_dirty) YGNodeCalculateLayout(root->yoga, surface_width / unit, DESIGN_HEIGHT, YGDirectionLTR);
    layout_dirty = 0;
    place_node(root);
    draw_node(root, base, 0, 0);
}

int scene_paint(double now) {
    if (!egl_window) return 0;
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    render(now);
    if (crossfade_started >= 0) {
        float progress = (now - crossfade_started) / CROSSFADE_SECONDS;
        glDisable(GL_SCISSOR_TEST);
        if (progress < 1) draw_snapshot(1 - eased(EASE_IN_OUT, progress));
        else crossfade_started = -1;
        moving |= progress < 1;
    }
    return moving;
}

void scene_present(void) {
    if (egl_window) eglSwapBuffers(egl_display, egl_surface);
}

int scene_blur(struct box *boxes, int max) {
    int count = blur_count < max ? blur_count : max;
    memcpy(boxes, blur_boxes, count * sizeof *boxes);
    return count;
}

void scene_crossfade(double now) {
    if (!egl_window || !root) return;
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);
    if (!snapshot_texture) {
        glGenTextures(1, &snapshot_texture);
        glGenFramebuffers(1, &snapshot_framebuffer);
    }
    attach_texture(snapshot_texture, snapshot_framebuffer, surface_width, surface_height, GL_RGBA8, GL_UNSIGNED_BYTE, GL_LINEAR, GL_CLAMP_TO_EDGE);
    render(now);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    crossfade_started = now;
}
