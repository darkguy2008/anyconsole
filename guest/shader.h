#define BLUR_LEVELS 4

GLuint compile_shader(GLenum type, const char *const *sources, int count);
GLuint link_shaders(const char *vertex, const char *const *fragments, int count);
void attach_texture(GLuint texture, GLuint framebuffer, GLsizei width, GLsizei height, GLint format, GLenum type, GLint filter, GLint wrap);
