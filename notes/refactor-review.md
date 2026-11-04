# Refactoring review

Review date: 2026-11-04.

Scope: the current working tree. This review proposes changes; it does not
change source code or the Looper backlog.

## Requirements and evidence

- [Specification](../docs/SPEC.md): preserve deterministic rules, the
  separation from SDL, save formats, controls, and frame pacing.
- [Testing guide](../docs/AGENT_TESTING.md): preserve independent expectations
  and the existing observations used by the harness.
- The preceding status check rebuilt Debug and Release. Both passed all
  five CTest suites. This review does not rerun the full desktop gate.
- Existing source changes remain uncommitted. Findings refer to that working
  tree rather than only the latest commit.

## Recommended changes

### 1. Share the file-loading procedure

Sources: [HighScoreStore::load](../src/storage.cpp#L315) and
[SettingsStore::load](../src/storage.cpp#L436).

Verified finding: both methods repeat the directory check, file status
check, regular-file check, bounded read, and exception handling. Atomic
writing already uses one shared function.

Proposal: add a private helper that returns a SaveStatus and file bytes.
Keep each format parser and its default values in its current store.
Avoid a templated store framework for these two formats.

Benefit: one place defines how missing, unreadable, and invalid files map
to status values. Risk is low if those mappings stay identical.

Validation: storage tests, including damaged files, absent directories,
legacy settings, and failed writes. Run the persistence harness sessions.

### 2. Give platform resources automatic cleanup

Source: [main and its exit paths](../src/main.cpp#L1542).

Verified finding: normal shutdown and script-load failure repeat file,
controller, joystick, renderer, window, SDL, and log cleanup. Earlier setup
failures contain partial copies. AudioDriver already has a destructor.

Proposal: use scoped owners for files, the window, and the renderer. Give
ControllerSet and the virtual controller explicit ownership. Keep the
session log alive until all shutdown messages are written.

Benefit: each resource has one cleanup rule. Future startup failures need
fewer manual edits. Risk is medium because shutdown order matters. The
script can detach the virtual controller, so its owner must support reset.

Validation: initialization failures, invalid scripts, controller detachment,
repeated launch and quit, sanitizer checks, and the clean-shutdown log.

### 3. Move developer automation out of main.cpp

Sources: [ScriptContext and InputScript](../src/main.cpp#L482),
[checkpoint capture](../src/main.cpp#L1096), and
[runMainLoop](../src/main.cpp#L1346).

Verified finding: main.cpp has 1,810 lines. It contains command-line parsing,
logging, controller discovery, script execution, capture, preferences,
timing, and startup. runMainLoop takes 18 parameters.

Proposal: first move the SDL script driver and checkpoint capture into a
developer module. Give it a small interface for input injection and capture.
Keep timing, simulation, rendering, and initialization visible in the loop.
Group recording options and streams where that removes repeated arguments;
avoid a context object that exposes every application resource.

Benefit: gameplay startup and timing become easier to inspect. This is a
medium-risk structural change, so perform it separately from behavior edits.

Validation: compare reference and application observations, replay keyboard
and controller sessions, and check captures, fixtures, stalls, and restarts.

### 4. Apply preferences through one path

Sources: [toggle functions](../src/main.cpp#L1178) and
[preference initialization](../src/main.cpp#L1708).

Verified finding: sound state lives in AudioDriver and Renderer. Ghost state
lives in a local variable and Renderer. Startup and toggle functions keep
these values synchronized and save the complete settings file.

Proposal: keep a small platform Preferences value. Use one function to
apply it to audio and rendering. Let both toggle routes update that value
and use one persistence path. Keep preferences outside the rules core.

Benefit: future settings require fewer synchronization edits. Risk is medium:
preserve input timing, immediate saves, missing-audio behavior, and log text.

Validation: options and mute sessions, persistence across processes, and
the case where selecting Options must not toggle its first row.

### 5. Resolve the SDL CMake target once

Source: [CMakeLists.txt](../CMakeLists.txt#L52).

Verified finding: the SDL imported-target check and legacy fallback repeat
for input, rendering, audio, and the executable.

Proposal: resolve SDL once into an interface target. Link the platform
libraries to it. The executable can receive SDL through their public
dependencies. Keep stackfall_core free of SDL linkage.

Benefit: fewer branches in the build definition. Risk is low, but verify
both package styles where those environments are available.

Validation: clean Debug and Release configuration, build, and CTest. Check
that the core test executable still has no SDL dependency.

### 6. Remove the duplicate scene-name switch

Sources: [sceneName](../src/main.cpp#L990) and
[observationSceneName](../src/observe.cpp#L31).

Verified finding: both switches return the same strings for every scene.

Proposal: reuse the existing formatter from main.cpp. A separate module is
unnecessary for this small change.

Benefit: a new scene needs one name update. Risk is low; log and observation
strings must remain byte-identical.

Validation: build and compare the scene strings in recorded sessions.

## Keep intentional independence

[reference_replay.cpp](../tests/harness/reference_replay.cpp#L18) explicitly
keeps its script parser and input mappings independent. They detect defects
in the application parser and SDL mappings. Do not merge these copies merely
to reduce line count.

Keep pixel expectations independent of rendering code. Keep pinned replay
hashes unchanged. The small menu state handlers already share cursor movement;
a generic menu framework offers little demonstrated benefit here.

## Suggested order and open questions

Start with storage loading and the scene-name switch. Then simplify CMake.
Do resource ownership before moving the developer driver. Handle preference
ownership as a separate change.

- Should structural work precede ARM64 packaging, or follow the first
  successful package? No packaging check requires a broad refactor.
- Can a legacy SDL CMake package be tested locally? Only the current host
  build is verified here.
- Shell drivers also repeat run-directory validation and process cleanup.
  A small shared helper could help, but Bash and POSIX shell differences
  need review before choosing its interface.

## Second validation pass

Review date: 2026-11-04. This pass rechecked the implementation, existing
storage tests, resource lifetimes, public formatter, and build dependencies.

- Storage loading remains a strong candidate. Existing tests cover missing,
  invalid, oversized, and legacy files. Keep parser exceptions inside the
  stores' recovery boundary; sharing only file reads must not remove that
  protection.
- Resource cleanup remains a strong candidate. AudioDriver already owns its
  device. Its explicit close currently stops audio before SDL_Quit. Preserve
  that order through declaration order or nested scopes; simply deleting
  explicit closes would be insufficient. This review identifies repeated
  cleanup, not a demonstrated resource leak.
- Script extraction remains useful, but moving code alone does not reduce
  complexity. Limit the first extraction to script execution and capture.
  Corrected the loop parameter count from 19 to 18.
- Preference centralization is downgraded to optional. Current startup and
  both toggle paths consistently update their consumers. No synchronization
  defect was found. For two preferences, another owner may add more machinery
  than it removes. Revisit when adding settings or extracting the platform
  session.
- The CMake repetition is confirmed. The platform libraries already export
  their SDL dependency publicly. Prefer resolving the target into one CMake
  variable, with a fallback interface target only when needed. A wrapper for
  every configuration is unnecessary.
- The two scene-name switch bodies are identical. The existing formatter is
  public, and main.cpp already includes its header. This is the smallest
  confirmed simplification.
- The independent reference parser and mappings remain deliberate. Preserve
  them as stated in the reference tool's source comment.

No source changes or new test runs were needed for this validation pass.
Recommended first batch: scene-name reuse and shared storage loading.
