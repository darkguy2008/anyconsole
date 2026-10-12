#define _GNU_SOURCE
#include <GLES2/gl2.h>
#include <errno.h>
#include <stdio.h>
#include "shader.h"

GLuint compile_shader(GLenum type, const char *const *sources, int count) {
    GLuint shader = glCreateShader(type);
    GLint compiled;
    glShaderSource(shader, count, sources, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[BUFSIZ];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "%s: shader: %s\n", program_invocation_short_name, log);
    }
    return shader;
}
