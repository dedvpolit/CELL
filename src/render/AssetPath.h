#pragma once
#include <string>

// Single lookup point for asset files (shaders, textures, fonts) relative to the executable. The
// audio code (AssetLocate.h, EnemyAudio.h, FootstepAudio.h) still has its own copies of the same
// search.
namespace AssetPath {

// Looks for relativePath next to the .exe first, then from the current working directory, walking
// up to kMaxLevelsUp levels either way: this makes the lookup independent of where the .exe was
// launched from (bin/Debug, bin/Release, an IDE's working dir, etc.). Returns an empty string if
// nothing is found.
std::string Resolve(const std::string& relativePath);

} // namespace AssetPath
