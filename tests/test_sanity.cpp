// Initial headless suite for the M0 skeleton (task T001).
// It verifies that the core library links and that its public constants
// hold. Milestone M1 adds suites for pieces, board, randomizer, game rules,
// and regression hashes; each one joins CMakeLists.txt and test_main.cpp.

#include <cstdint>
#include <cstring>

#include "stackfall/version.hpp"
#include "test_util.hpp"

// The board stores each row in a uint16_t mask and each cell color in a
// uint8_t. The M1 bitmask logic depends on these exact sizes.
static_assert(sizeof(std::uint16_t) == 2, "board rows need exact 16-bit masks");
static_assert(sizeof(std::uint8_t) == 1, "cell colors need exact bytes");

namespace {

void check_version_constants() {
  SC_CHECK(stackfall::kVersionMajor >= 0);
  SC_CHECK(stackfall::kVersionMinor >= 0);
  SC_CHECK(stackfall::kVersionPatch >= 0);
}

void check_version_string() {
  // The out-of-line call proves that the stackfall_core archive links.
  const char* version = stackfall::versionString();
  SC_CHECK(version != nullptr);
  SC_CHECK(std::strcmp(version, "0.1.0") == 0);
  SC_CHECK(std::strcmp(version, stackfall::kVersionString) == 0);
}

}  // namespace

void run_sanity_tests() {
  check_version_constants();
  check_version_string();
}
