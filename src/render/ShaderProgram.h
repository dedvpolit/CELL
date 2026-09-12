#pragma once
#include <GL/glew.h>

// ============================================================================
// ShaderProgram — общая компиляция/линковка GLSL-шейдеров.
//
// До рефакторинга одна и та же пара функций (компилировать шейдер, слинковать
// программу, вывести лог ошибки в stderr, удалить промежуточные шейдер-объекты
// после линковки) была продублирована ТРИ РАЗА почти дословно:
//   - AsciiEffect::compileShader() / linkProgram()
//   - DungeonScene::compileShader() / linkProgram()      (сцена + debug-карта)
//   - DungeonScene::compileCompassShader() / linkCompassProgram()
// Единственная содержательная разница между копиями — текст префикса в
// сообщении об ошибке (чтобы в консоли было видно, какая подсистема не
// скомпилировалась) и размер буфера лога (1024 vs 2048 байт). Оба этих
// отличия сохранены здесь как параметр debugLabel и увеличенный до 2048
// (безопасный супернабор) размер буфера — поведение при ошибке не меняется,
// только устраняется дублирование самого кода.
// ============================================================================
namespace ShaderProgram {

// debugLabel используется только в сообщении об ошибке компиляции
// (например, "AsciiEffect", "DungeonScene", "Compass") — не влияет на
// сам шейдер.
GLuint CompileShader(GLenum type, const char* src, const char* debugLabel);

// Линкует и линкует шейдеры в программу, удаляет промежуточные shader-объекты
// (glDeleteShader) после линковки — как и было в исходных копиях.
GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader, const char* debugLabel);

} // namespace ShaderProgram
