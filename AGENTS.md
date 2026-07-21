# how2scudo contributor and agent guide

## Project mission

`how2scudo` is an experimental Android security-research reference for the
Scudo hardened allocator, inspired by
[`how2heap`](https://github.com/shellphish/how2heap). It should make allocator
behavior and exploitation techniques reproducible, explain which initial
memory-corruption primitives they require, and show what stronger primitive
each technique actually produces.

The long-term goal is a centralized, version-aware knowledge base that can also
support LLM-assisted translation of glibc heap techniques into Scudo research
hypotheses and minimal Android proofs of concept. Current work is focused on:

- porting or adapting relevant `how2heap` ideas to Scudo;
- reproducing published Scudo research, including the WOOT '24 techniques;
- documenting why a technique works, fails, or changes between Android builds;
- composing demonstrated primitives into stronger primitives or, where a full
  chain is actually proven, code execution in a controlled research target.

This is not a generic Android application. The apps deliberately exercise
memory-unsafe allocator behavior and may crash by design.

## Research scope and safety

Work only with owned, disposable lab targets such as Android emulators, Cuttlefish
instances, or explicitly authorized test devices. Keep demonstrations
self-contained: they should prove allocator behavior against the sample app or a
purpose-built local harness, not deploy against third-party apps, services, or
devices. Do not add persistence, stealth, credential access, exfiltration,
lateral movement, or unrelated post-exploitation behavior.

Treat a crash, allocator abort, controlled allocation, overlapping allocation,
arbitrary read, arbitrary write, control-flow influence, and code execution as
different results. Report only the strongest primitive the evidence proves.

## Repository map

There is no root Gradle build. The repository currently contains legacy
independent Android Studio projects plus an additive native-only,
build-ID-specific layout:

```text
android_12/
  forged_commit_base/   API 31 sample
  safe_unlink/          API 31 sample; currently known broken/unverified
android_13/
  forged_commit_base/   API 33 sample
  safe_unlink/          API 33 sample; currently known broken/unverified
android_14/
  forged_commit_base/   API 34 sample
  house_of_spirit/      API 34 sample
  <libc-build-id>/      exact-libc native PoCs, target constants, and evidence
utils/
  find_cookie_offset.py minimal ELF parser for locating _ZL9Allocator
README.md               short project overview and known cookie offsets
```

Inside a legacy Android Studio sample:

- `app/src/main/cpp/native-lib.cpp` contains the allocator experiment;
- `app/src/main/java/.../MainActivity.kt` loads the native library and runs or
  displays the experiment;
- `app/src/main/cpp/CMakeLists.txt` names and links the JNI library;
- `app/build.gradle.kts` pins the minimum Android API and native build;
- `app/src/test` and `app/src/androidTest` currently contain Android Studio
  placeholder tests, not meaningful exploit validation.

Inside a build-ID target:

- `README.md` records exact binary identity, derivations, assumptions, and
  runtime verification status;
- `target.h` contains constants derived from that one libc binary;
- each `<technique>.c` or `<technique>.cpp` is the canonical, native-only PoC;
- an optional `CMakeLists.txt` builds the PoC as an Android executable without
  requiring an APK; and
- generated native build output belongs in the ignored `out/` directory.

The root `libc.so`, when present, is a local analysis artifact. The ignored
`android_14/scudo/` directory, when present, is a local upstream source checkout.
Neither is canonical project source. Build outputs (`build/`, `.cxx/`, `.gradle/`),
IDE state, APKs, pulled system binaries, crash dumps, and upstream source checkouts
must not be committed. Some IDE files are already tracked historically; avoid
changing them unless the task specifically requires it.

## Current technique model

The native samples reconstruct Scudo metadata, calculate the header checksum
from the allocator cookie, and deliberately feed forged state back to Scudo:

- `house_of_spirit` forges a valid primary chunk at a controlled address, frees
  it, and demonstrates `malloc` returning that address.
- `forged_commit_base` makes a neighboring primary chunk appear to be a
  secondary allocation, forges its secondary header, frees it, and demonstrates
  a later secondary allocation at a chosen aligned location. Adjacency is
  probabilistic, so a run may legitimately ask for a retry.
- `safe_unlink` attempts to link a fake secondary chunk with a thread-local
  `PerClass` freelist so a small allocation returns the `PerClass` address. The
  root README says the safe-unlink exploits need fixing; do not describe them as
  working without new runtime evidence.

The known cookie offsets currently documented by the repository are:

| Runtime | API | `_ZL9Allocator` / cookie offset |
| --- | ---: | ---: |
| Android 12 emulator image used by the PoCs | 31 | `0xd06c0` |
| Android 13 emulator image used by the PoCs | 33 | `0xcc440` |
| Android 14 emulator image used by the PoCs | 34 | `0xf7480` |

These are build-specific offsets, not Android-version APIs. The compile/target
SDK is currently 36, but that does not make a sample valid on every runtime with
the same API level. OEM, release, emulator, APEX, architecture, and patch-level
differences can change Scudo configuration, globals, structure layouts, size
classes, TLS organization, and symbol availability.

The existing implementations also assume AArch64 details, including top-byte
tagged pointers; `safe_unlink` reads `tpidr_el0` directly and hard-codes a TLS
slot offset of `0x30` and a `PerClass` stride of `0x78`. Its comments additionally
require memory tagging to be disabled and a compatible compact-pointer
configuration. Treat all such constants and requirements as hypotheses tied to
the named build until independently derived and observed.

Android 14's represented secondary header is larger and has different fields
than the Android 12/13 representation. This is exactly why version directories
are intentional: do not blindly propagate structure definitions or offsets
between them.

## Evidence rules

Never invent or extrapolate an allocator offset, cookie, tag, size class,
structure field, TLS offset, checksum rule, or success condition. Derive it from
the exact Scudo source revision, an exact matching binary, debugger observations,
or a small measurement harness. If the necessary artifact is unavailable, state
what is unknown and leave a clearly labeled hypothesis or TODO.

Every new or materially changed technique should record:

- technique name and source/inspiration;
- exact Android API, ABI, image fingerprint or release, and libc build ID;
- exact Scudo/LLVM or AOSP revision when known;
- relevant runtime configuration, including MTE/HWASan and compact pointers;
- starting memory-corruption primitive and attacker-controlled inputs;
- required heap state, allocation sizes, alignment, leaks, and retry conditions;
- the mitigation being crossed and why each forged field passes validation;
- the resulting primitive and a precise, observable success condition;
- reliability observations and known failure/abort modes;
- commands used to build and reproduce the result.

Keep `observed`, `derived`, `assumed`, and `unverified` facts visibly distinct.
An allocator abort is useful evidence about a failed hypothesis, but it is not a
successful primitive. A controlled allocation or arbitrary write does not by
itself prove code execution.

Prefer `static_assert`, `sizeof`, `alignof`, and `offsetof` checks near copied
metadata layouts. Note when a C++ bit-field layout is compiler/ABI-dependent.
If code is adapted from Scudo, a paper, blog, or another repository, cite the
exact URL and revision where possible and preserve applicable license notices.

## LLM-assisted glibc-to-Scudo translation workflow

Translate the exploit idea, not glibc's metadata bytes:

1. Express the source technique as preconditions, allocator operations, the
   invariant it violates, and the resulting primitive.
2. Separate allocator-agnostic capabilities from glibc-specific machinery such
   as bins, tcache, safe-linking, chunk flags, hooks, or arena internals.
3. Map the required behavior onto Scudo components: primary size classes,
   thread caches/TSD, transfer batches, quarantine, secondary allocations,
   checksummed headers, compact pointers, randomization, and memory tagging.
4. Identify which Scudo mitigation blocks a direct port and propose the minimum
   additional leak, corruption, free, allocation, retry, or layout condition
   needed to cross it.
5. Derive all build-specific constants from matching source/binaries and add
   runtime assertions or diagnostics before attempting corruption.
6. Build the smallest self-contained PoC that proves one primitive transition.
7. Validate on a clean matching target, preserve logs/debugger evidence, and
   document unsuccessful variants as well as the working one.
8. Only then investigate composition with other demonstrated primitives.

An LLM-generated port is a research hypothesis until it builds and succeeds on
the declared runtime. Ask for the matching libc, symbols, build fingerprint, or
Scudo revision when they are required; do not fill gaps with plausible-looking
constants.

## Implementation conventions

- Preserve the existing independent, versioned projects while the build-ID
  layout is piloted. Cross-build duplication is preferable to a misleading
  shared abstraction when allocator layouts differ.
- Scope changes to the requested technique/runtime. Do not update sibling
  versions merely because their code looks similar.
- Keep the exploitation sequence linear and readable. Use stage-oriented logs
  that explain allocations, forged metadata, frees, expected retries, and the
  final comparison or write.
- Use a technique-specific log tag. Keep JNI package/class/function names,
  CMake's library name, and `System.loadLibrary` synchronized.
- Use `uintptr_t` and fixed-width integers for address/metadata arithmetic.
  Name magic values and put their derivation, build ID, and units beside them.
- Make alignment, chunk/user-header deltas, tagged versus untagged pointers, and
  requested versus rounded allocation sizes explicit.
- Preserve deliberate memory corruption needed by a PoC, but comment the exact
  out-of-bounds/free behavior and expected optimizer/ABI assumptions. Do not
  silently turn a demonstration into unrelated undefined behavior.
- Fail early with a useful log when a prerequisite is not met. Probabilistic
  failure must be distinguishable from allocator rejection or a crash.
- Avoid unrelated UI work and new dependencies. The native experiment and its
  explanation are the product.

When adding a sample, copy the nearest compatible version only as a scaffold.
Rename the Gradle root, namespace/application ID, Kotlin package, CMake project,
native library, `System.loadLibrary` call, JNI exports, log tag, labels, and test
packages. Set `minSdk` to the runtime API being demonstrated. Then re-derive the
allocator-specific implementation rather than inheriting it from the scaffold.

For a build-ID-native sample, use
`android_<version>/<full-libc-build-id>/`. Keep target-specific constants in
`target.h`, keep the exploitation sequence readable in one source file, and
record whether each fact is binary-derived or runtime-observed in the adjacent
README. Give native PoCs a stable `extern "C" int how2scudo_run()` entry point;
an optional `main()` guarded by `HOW2SCUDO_STANDALONE` lets the same source work
with a future shared APK harness.

## Build and validation

Run Gradle from the individual sample directory, never from the repository root:

```sh
cd android_14/house_of_spirit
./gradlew :app:assembleDebug
./gradlew :app:testDebugUnitTest
```

Build-ID-native PoCs instead use the Android NDK toolchain and their local
CMake project. Configure them for the exact documented API and ABI, and keep
the build tree under that target's ignored `out/` directory. The resulting
dynamic executable must run on the matching Android target so it resolves the
device's real libc; never link or package the extracted analysis `libc.so`.

The local unit and instrumentation tests are still template smoke tests. A
successful Gradle build proves only that the app compiled; it does not prove the
allocator technique.

Use a fresh, disposable AArch64 target matching the documented API and libc
build. Confirm the target identity and allocator prerequisites before running.
A typical manual cycle is:

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb logcat -c
adb shell am force-stop xyz.cygnusx.house_of_spirit
adb shell am start -n xyz.cygnusx.house_of_spirit/.MainActivity
adb logcat -d
```

Narrow logcat by the sample's actual tag or process when recording evidence.
`house_of_spirit` reports its address comparison in the UI; the other current
samples primarily log from native code. For probabilistic heap layouts, record
the number of trials and distinguish a clean “retry” from an abort.

The cookie helper has no third-party dependencies:

```sh
python3 utils/find_cookie_offset.py /path/to/matching/libc.so
```

It looks for the local `_ZL9Allocator` symbol and reports its value as the cookie
offset used by these Android builds. Stripped release libraries may not contain
that local symbol; “not found” is not evidence that the offset is zero or that
the nearest version's offset is valid. Record the libc build ID separately and
cross-check the result against the sample before use.

Validation should be proportional to the change:

- documentation-only: check paths, commands, terminology, and Markdown;
- utility change: exercise success, not-found, malformed, truncated, and relevant
  32/64-bit or endian cases;
- native/Kotlin/build change: assemble the touched project and run it on the
  matching target when one is available;
- layout or version port: validate the exact ABI/build and the observable
  primitive, not merely compilation;
- shared or repeated change: build every project actually touched.

If a matching runtime is unavailable, say so in the handoff and report the
strongest completed static/build validation. Never call an unrun exploit
“working.”

## Working-tree hygiene

Inspect `git status --short` before editing and preserve unrelated user changes.
Do not overwrite a modified PoC, generated local analysis, or IDE state. Do not
commit, push, or rewrite history unless explicitly asked. Do not add local SDK
paths, device identifiers, pulled binaries, symbols, APKs, build output, crash
dumps, or secrets.

Before finishing, summarize the files changed, the exact checks run, the target
runtime if any, observed results, and anything that remains unverified.

## Definition of done for a technique

A technique is ready to be presented as working only when:

- its starting and resulting primitives are precise;
- all layout/configuration assumptions are tied to an exact runtime;
- the code builds from a clean sample project;
- the success condition is observed on the declared target;
- expected logs/output and retry or failure behavior are documented;
- sources and derivations are cited; and
- no generated or local target artifacts are included in the change.
