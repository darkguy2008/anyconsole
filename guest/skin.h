#include <cairo.h>
#include <limits.h>
#include <wayland-client.h>

struct node;
struct box { float x, y, width, height; };
#define DEFAULT_STIFFNESS 150.0f
#define SKIN_ENTRY "skin.js"

void scene_init(struct wl_display *display);
void scene_attach(struct wl_surface *surface, int width, int height);
void scene_detach(void);
int scene_paint(double now);
void scene_present(void);
int scene_blur(struct box *boxes, int max);
void scene_image(const char *name, cairo_surface_t *surface);
void scene_crossfade(double now);
struct node *scene_root(void);
struct node *scene_node(const char *type);
void scene_free(struct node *node);
void scene_insert(struct node *parent, struct node *child, int index);
void scene_remove(struct node *child);
void scene_text(struct node *node, const char *text);
void scene_number(struct node *node, const char *name, double value);
void scene_string(struct node *node, const char *name, const char *value);
void scene_unset(struct node *node, const char *name);
void scene_transition(struct node *node, const char *name, double milliseconds, const char *easing, double stiffness);
void scene_from(struct node *node, const char *name, const char *text, double value);
int scene_page(struct node *list, int selected, int direction);
void scene_folder(const char *folder);
const char *scene_resolve(const char *name, char path[PATH_MAX]);

int script_load(const char *folder);
void script_state(const char *json);
int script_command(const char *name);
double script_next_timer(void);
void script_run_timers(double now);

void sound_init(void);
void sound_play(const char *path);

void skin_select(int index);
double skin_now(void);
