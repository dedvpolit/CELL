#include "ShaderProgram.h"
#include <stdexcept>
#include <string>

namespace ShaderProgram {

GLuint CompileShader(GLenum type, const char* src, const char* debugLabel)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string(debugLabel) + " shader failed to compile:\n" + log);
    }
    return shader;
}

GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader, const char* debugLabel)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        throw std::runtime_error(std::string(debugLabel) + " shader program failed to link:\n" + log);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

} // namespace ShaderProgram
