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

GLuint link_shaders(const char *vertex, const char *const *fragments, int count) {
    GLuint program = glCreateProgram(), shaders[] = {compile_shader(GL_VERTEX_SHADER, &vertex, 1), compile_shader(GL_FRAGMENT_SHADER, fragments, count)};
    GLint linked;
    for (unsigned i = 0; i < sizeof shaders / sizeof *shaders; i++) glAttachShader(program, shaders[i]);
    glLinkProgram(program);
    for (unsigned i = 0; i < sizeof shaders / sizeof *shaders; i++) glDeleteShader(shaders[i]);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[BUFSIZ];
        glGetProgramInfoLog(program, sizeof log, NULL, log);
        fprintf(stderr, "%s: shader: %s\n", program_invocation_short_name, log);
    }
    return program;
}

void attach_texture(GLuint texture, GLuint framebuffer, GLsizei width, GLsizei height, GLint format, GLenum type, GLint filter, GLint wrap) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
}
