#include "ShaderLoader.h"
#include "AssetPath.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ShaderLoader {

std::string LoadSource(const std::string& relativePath)
{
    const std::string fullPath = AssetPath::Resolve(relativePath);
    if (fullPath.empty()) {
        throw std::runtime_error(
            "ShaderLoader: shader source not found: " + relativePath);
    }

    std::ifstream file(fullPath, std::ios::in | std::ios::binary);
    if (!file) {
        throw std::runtime_error(
            "ShaderLoader: failed to open shader source: " + fullPath);
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

} // namespace ShaderLoader
