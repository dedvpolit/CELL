#pragma once
#include <GL/glew.h>

// Shared GLSL compile/link, used by AsciiEffect, DungeonScene (scene, debug map) and the compass.
// The only per-caller differences are the error-message prefix (debugLabel) and the shared
// 2048-byte log buffer.
namespace ShaderProgram {

// Both functions throw std::runtime_error with the GL info log on failure; main() reports it.
// debugLabel (e.g. "AsciiEffect") only prefixes that message.
GLuint CompileShader(GLenum type, const char* src, const char* debugLabel);

// Links shaders into a program and deletes the intermediate shader objects.
GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader, const char* debugLabel);

} // namespace ShaderProgram
