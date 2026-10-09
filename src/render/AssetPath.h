#pragma once
#include <string>

// Asset lookup relative to the executable
// The audio code has its own copy (AssetLocate.h)
namespace AssetPath {

// Tries next to the executable, then the working directory, walking up to kMaxLevelsUp levels from each
std::string Resolve(const std::string& relativePath);

} // namespace AssetPath
