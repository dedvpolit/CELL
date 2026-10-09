#pragma once
#include <string>

// Reads GLSL source from assets/shaders/
namespace ShaderLoader {

// Resolved through AssetPath
// Throws if missing; main() reports it
std::string LoadSource(const std::string& relativePath);

} // namespace ShaderLoader
