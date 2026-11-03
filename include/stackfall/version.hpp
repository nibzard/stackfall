#pragma once

// Version constants for STACKFALL.
// Keep these values in sync with project(... VERSION ...) in CMakeLists.txt.
// Milestone M5 raises the version to 1.0.0 for the first release.

namespace stackfall {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// Dotted decimal form of the constants above, for example "0.1.0".
inline constexpr char kVersionString[] = "0.1.0";

// Out-of-line copy, defined in version.cpp. Tests use it to prove that the
// stackfall_core archive links into an executable.
const char* versionString();

}  // namespace stackfall
