# how2scudo

## Overview

This repo is inspired by [how2heap](https://github.com/shellphish/how2heap)

The goal is to have a list of common techniques for the Android Scudo Allocator given certain primitives. In the future LLMs can use this repository as a set of skills for exploitation generation. 

Each folder represents an android major version. Each subfolder represents a 64 bit build_id for that version. 32 bit PoCs are not tracked.

The layout of each subfolder is as follows:

```text
android_<version>/<libc-build-id>/
  target.h
  <technique>.c
  CMakeLists.txt
```

## Building

Replace `$ANDROID_NDK` with the path to your local NDK. 

Replace `$VERSION` with the platform api version you are building for. A list of build_id to api version can be found [here](https://bionicdb.neilhommes.xyz/).

```sh
cmake -S . -B out \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-$VERSION \
  -DCMAKE_BUILD_TYPE=Debug
  
cmake --build out
```

This will generate binaries in `out`. Run the PoCs through adb shell.
