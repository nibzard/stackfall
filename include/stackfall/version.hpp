#pragma once

// Version constants for STACKFALL.
// Keep these values in sync with project(... VERSION ...) in CMakeLists.txt.
// The first release of milestone M5 carries the version 1.0.0.

namespace stackfall {

inline constexpr int kVersionMajor = 1;
inline constexpr int kVersionMinor = 0;
inline constexpr int kVersionPatch = 0;

// Dotted decimal form of the constants above, for example "1.0.0".
inline constexpr char kVersionString[] = "1.0.0";

// Out-of-line copy, defined in version.cpp. Tests use it to prove that the
// stackfall_core archive links into an executable.
const char* versionString();

}  // namespace stackfall
