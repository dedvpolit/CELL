#pragma once
#include <GL/glew.h>

// GLSL compile and link shared by all renderers
namespace ShaderProgram {

// Throw std::runtime_error with the info log, prefixed by debugLabel
GLuint CompileShader(GLenum type, const char* src, const char* debugLabel);

// Links shaders into a program and deletes the intermediate shader objects
GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader, const char* debugLabel);

} // namespace ShaderProgram
