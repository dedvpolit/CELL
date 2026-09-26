#pragma once
#include <string>

// Reads GLSL shader source from disk. Shaders are plain text files under assets/shaders/ (not
// embedded in .cpp) and are copied next to the .exe by the same .cbp ExtraCommands step that copies
// textures/audio. Deliberate: ease of editing shaders matters more than hiding the source.
namespace ShaderLoader {

// relativePath is resolved via AssetPath::Resolve(), e.g. "assets/shaders/scene.frag". Throws
// std::runtime_error if the file is missing: a missing shader is a fatal startup error that main()
// reports.
std::string LoadSource(const std::string& relativePath);

} // namespace ShaderLoader
