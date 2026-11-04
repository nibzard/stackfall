cmake_minimum_required(VERSION 3.16)

project(stackfall
  VERSION 1.0.0
  DESCRIPTION "STACKFALL: an offline deterministic tetromino game for the Anbernic RG503"
  LANGUAGES CXX)

# Keep the project version in sync with include/stackfall/version.hpp.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# stackfall_core: portable rules library. It has no SDL2 or other platform
# linkage, so rule tests run headless on any development machine.
# Source files from milestones M1 and later join this list. storage.cpp holds
# the SDL-free filesystem half of the platform layer (task T012); it uses
# std::filesystem alone, so the persistence suite runs headless too.
# fixture.cpp and observe.cpp carry the developer fixture loader and the
# checkpoint record formatter of the unattended harness (task T023,
# docs/AGENT_TESTING.md); both are SDL-free so the headless reference replay
# shares them with the game binary. script.cpp holds the bounded count rule
# the two mirrored script parsers share (task T025's fuzz repair).
# music.cpp, music_tracks.cpp, and music_director.cpp hold the generated
# soundtrack (MUSIC.md, task T062): engine, catalogue, and scene policy,
# all SDL-free so the music suite and the offline preview tool run headless.
# ---------------------------------------------------------------------------
add_library(stackfall_core STATIC
  src/board.cpp
  src/fixture.cpp
  src/game.cpp
  src/music.cpp
  src/music_director.cpp
  src/music_tracks.cpp
  src/observe.cpp
  src/pieces.cpp
  src/randomizer.cpp
  src/scoring.cpp
  src/script.cpp
  src/storage.cpp
  src/version.cpp
)
target_include_directories(stackfall_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(stackfall_core PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall_input: the SDL input adapter (task T009). It owns the section 5
# controller and keyboard mapping tables and feeds the SDL-free InputAdapter
# of stackfall/input.hpp. The rules core stays SDL-free; this library carries
# the SDL dependency so both the game and its input tests share one table.
# ---------------------------------------------------------------------------
find_package(SDL2 REQUIRED)

# Resolve SDL2 once, before the first consumer. Current package configs
# export the SDL2::SDL2 imported target with their public dependencies.
# Legacy configs set only the SDL2_INCLUDE_DIRS and SDL2_LIBRARIES
# variables, so wrap those in one interface target for that case. Every SDL
# consumer below links the STACKFALL_SDL variable alone; stackfall_core and
# its headless suites keep no SDL dependency.
if(TARGET SDL2::SDL2)
  set(STACKFALL_SDL SDL2::SDL2)
else()
  add_library(stackfall_sdl2 INTERFACE)
  target_include_directories(stackfall_sdl2 INTERFACE ${SDL2_INCLUDE_DIRS})
  target_link_libraries(stackfall_sdl2 INTERFACE ${SDL2_LIBRARIES})
  set(STACKFALL_SDL stackfall_sdl2)
endif()

add_library(stackfall_input STATIC
  src/input_sdl.cpp
)
target_link_libraries(stackfall_input PUBLIC stackfall_core ${STACKFALL_SDL})
target_compile_features(stackfall_input PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall_render: the procedural SDL renderer (task T010). It owns the
# section 6.1 layout, the seven-color palette, and the 5x7 glyph drawing of
# section 6.2, and it reads the game through const queries only. The rules
# core stays SDL-free; this library carries the SDL dependency so the game
# and its pixel-level suite share one implementation.
# ---------------------------------------------------------------------------
add_library(stackfall_render STATIC
  src/renderer_sdl.cpp
)
target_link_libraries(stackfall_render PUBLIC stackfall_core ${STACKFALL_SDL})
target_compile_features(stackfall_render PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall_audio: the synthesized sound effects (task T013). It owns the cue
# table, the integer waveform synthesis, the cue detector that reads a round
# through const queries, and the SDL audio device with its mute toggle. It
# links the core for the Game the detector observes, and it carries the SDL
# dependency so the game and its audio suite share one implementation.
# ---------------------------------------------------------------------------
add_library(stackfall_audio STATIC
  src/audio_sdl.cpp
)
target_link_libraries(stackfall_audio PUBLIC stackfall_core ${STACKFALL_SDL})
target_compile_features(stackfall_audio PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall_developer: the developer automation of one session (task T048,
# docs/AGENT_TESTING.md). It owns the script driver, the virtual gamepad, the
# checkpoint capture, and the evidence streams behind the harness options, so
# src/main.cpp keeps only the timing, the simulation, the rendering, and the
# startup order. It carries the SDL dependency for the same reason the input,
# render, and audio libraries do: the game binary and its future suites share
# one implementation. The mirrored parser of tests/harness stays independent
# of this library on purpose.
# ---------------------------------------------------------------------------
add_library(stackfall_developer STATIC
  src/developer_session.cpp
)
target_link_libraries(stackfall_developer PUBLIC
  stackfall_core stackfall_input stackfall_render stackfall_audio ${STACKFALL_SDL})
target_compile_features(stackfall_developer PUBLIC cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall: the game executable. It reaches SDL2 through the public
# dependencies of the platform libraries above, so it states no SDL link of
# its own.
# ---------------------------------------------------------------------------
add_executable(stackfall
  src/main.cpp
)
target_link_libraries(stackfall PRIVATE stackfall_core stackfall_input stackfall_render stackfall_audio stackfall_developer)

# ---------------------------------------------------------------------------
# stackfall_tests: headless rule tests. It links stackfall_core only. Do not
# add SDL2 here; the suite must run on machines without a display.
# Add one source file per suite and one call in tests/test_main.cpp.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(stackfall_tests
  tests/test_main.cpp
  tests/test_board.cpp
  tests/test_fixture.cpp
  tests/test_font.cpp
  tests/test_game.cpp
  tests/test_input.cpp
  tests/test_music.cpp
  tests/test_pieces.cpp
  tests/test_randomizer.cpp
  tests/test_regression.cpp
  tests/test_sanity.cpp
  tests/test_screens.cpp
  tests/test_script.cpp
  tests/test_storage.cpp
)
target_link_libraries(stackfall_tests PRIVATE stackfall_core)

add_test(NAME stackfall_tests COMMAND stackfall_tests)
# The developer test CLI of section 4.3: --seed <integer> also replays every
# pinned scenario at that seed and requires the same hash twice
# (specification section 7, section 11 item 18 desktop half).
add_test(NAME stackfall_tests_seed COMMAND stackfall_tests --seed 987654321)

# ---------------------------------------------------------------------------
# stackfall_music_preview: the offline developer renderer of MUSIC.md phase
# M2. Not a test and not shipped: it renders any track, duration, sample
# rate, and intensity through the same engine the audio callback runs, and
# writes review WAVs into an ignored build directory.
# ---------------------------------------------------------------------------
add_executable(stackfall_music_preview
  tools/music/render_previews.cpp
)
target_link_libraries(stackfall_music_preview PRIVATE stackfall_core)
target_compile_features(stackfall_music_preview PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall_reference: the headless reference replay of the unattended harness
# (task T023, docs/AGENT_TESTING.md). It links the rules core alone - no SDL,
# no display - and recomputes what a session script must produce, so the
# harness can compare that against the real SDL application's checkpoint
# records. Not a ctest suite: tests/harness/run.sh drives it beside the game
# binary and owns the comparison.
# ---------------------------------------------------------------------------
add_executable(stackfall_reference
  tests/harness/reference_replay.cpp
)
target_link_libraries(stackfall_reference PRIVATE stackfall_core)
target_compile_features(stackfall_reference PRIVATE cxx_std_17)

# ---------------------------------------------------------------------------
# stackfall_input_tests: the SDL event path above the SDL-free adapter
# (task T009). Keyboard events come from the SDL queue and controller events
# from an SDL virtual controller mapped through SDL_GAMECONTROLLERCONFIG, so
# the suite needs no display and no physical device. It stays separate from
# stackfall_tests, which must link the core only (specification section 8).
# ---------------------------------------------------------------------------
add_executable(stackfall_input_tests
  tests/test_input_sdl.cpp
)
target_link_libraries(stackfall_input_tests PRIVATE stackfall_input)

add_test(NAME stackfall_input_tests COMMAND stackfall_input_tests)

# ---------------------------------------------------------------------------
# stackfall_render_tests: the SDL render path of task T010. It draws real
# frames into an offscreen software renderer and asserts exact pixels against
# the game state, so the renderer is checked as strictly as the rules. No
# window, no video subsystem, and no asset file are involved, so it runs
# everywhere the SDL2 library itself builds.
# ---------------------------------------------------------------------------
add_executable(stackfall_render_tests
  tests/test_renderer_sdl.cpp
)
target_link_libraries(stackfall_render_tests PRIVATE stackfall_render)

add_test(NAME stackfall_render_tests COMMAND stackfall_render_tests)

# ---------------------------------------------------------------------------
# stackfall_audio_tests: the sound path of task T013 (specification section
# 6.3). The synthesis and the cue detector are pinned exactly and need no
# device; the device half runs against the SDL dummy audio driver, so CTest
# needs no sound hardware and no display, exactly like the other SDL suites.
# ---------------------------------------------------------------------------
add_executable(stackfall_audio_tests
  tests/test_audio_sdl.cpp
)
target_link_libraries(stackfall_audio_tests PRIVATE stackfall_audio)

add_test(NAME stackfall_audio_tests COMMAND stackfall_audio_tests)
