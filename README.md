# how2scudo

## Overview

This repo is inspired by [how2heap](https://github.com/shellphish/how2heap)

The goal is to have a list of common techniques for the Android Scudo Allocator given certain primitives. In the future LLMs can use this repository as a set of skills for exploitation generation.

Each folder represents an android major version. Each subfolder represents a 64 bit build_id for that version. 32 bit PoCs are not tracked.

The layout of each subfolder is as follows:

```text
android_<version>/<libc-build-id>/
  target.h        # constants derived from that one exact libc (offsets, sizes, masks)
  <technique>.c   # a self-contained, native-only proof of concept
  CMakeLists.txt  # builds each technique as a standalone Android executable
```

> ⚠️ **Research use only.** These PoCs deliberately corrupt allocator metadata and
> may crash or abort by design. Run them only against disposable, self-owned
> targets such as an Android emulator or a Cuttlefish instance.

## Techniques

Each PoC assumes a set of starting primitives and demonstrates a stronger one. The build-id targets under `android_14/` currently ship:

- **`house_of_spirit`** — forges a valid primary chunk at a controlled address, frees it, and shows `malloc` handing that address back, creating overlapping allocations.
- **`forged_commit_base`** — makes a neighbouring primary chunk look like a secondary allocation, forges its secondary header, frees it, and shows a later secondary allocation landing at a chosen aligned address. Adjacency is probabilistic, so a run may legitimately ask you to retry.

A `safe_unlink` variant also exists as a legacy sample under earlier versions, but it is currently **known-broken/unverified** — do not treat it as working without fresh runtime evidence.

## Prerequisites

- **Android NDK** (r25+) — provides the CMake toolchain file and the AArch64 clang.
- **CMake** ≥ 3.22.1 with a build backend (Ninja or Make).
- **adb** (platform-tools) and an **AArch64** Android target you can reset — an emulator system image or a Cuttlefish virtual device — whose libc build ID matches the folder you build.
- **Python 3** for `utils/find_cookie_offset.py` (standard library only; no third-party packages).

## Picking the right target

A PoC's constants live in `target.h` and are derived from **one exact libc binary**, identified by its GNU build ID. Reusing them against a different libc will not work. To read your target's build ID:

```sh
# path can vary by release; on recent Android bionic lives under the runtime APEX
adb pull /apex/com.android.runtime/lib64/bionic/libc.so ./libc.so
readelf -n ./libc.so | grep -i 'build id'
```

Then use the matching `android_<version>/<libc-build-id>/` directory. A `build_id` → API-level map is maintained at <https://bionicdb.neilhommes.xyz/>.

## Building

Build a target from **inside its build-ID directory**. Replace `$ANDROID_NDK` with the path to your local NDK and `$API` with the target's platform API level (e.g. `34` for Android 14):

```sh
cd android_14/<libc-build-id>

cmake -S . -B out \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-$API \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build out
```

Each technique in that directory builds as its own standalone executable in `out/` (compiled with `HOW2SCUDO_STANDALONE`, so it runs directly from a shell without an APK).

## Running a PoC

Push the built binary to the matching target and run it from an `adb` shell:

```sh
adb push out/house_of_spirit /data/local/tmp/
adb shell chmod +x /data/local/tmp/house_of_spirit
adb shell /data/local/tmp/house_of_spirit
```

Each PoC logs its stages — the allocations it makes, the metadata it forges, the frees it performs, and the final check that demonstrates the resulting primitive. Because some heap layouts are probabilistic, a run may report a benign *retry* instead of success; re-run it a few times and note how often it succeeds versus aborts.

## Finding the Scudo cookie offset

Scudo checksums its chunk headers with a per-process cookie. For the Android builds targeted here, the cookie sits at the start of the file-static `_ZL9Allocator` object. `utils/find_cookie_offset.py` locates that symbol in an unstripped libc and prints its offset:

```sh
python3 utils/find_cookie_offset.py /path/to/matching/libc.so
```

Release images often strip that local symbol; a *not found* result means the symbol is absent, **not** that the offset is zero. Always cross-check the reported value against the `target.h` for your build ID before relying on it.
