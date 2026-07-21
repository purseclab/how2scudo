# how2scudo

** TODO **
 - Fix safe_unlink exploits

This repo is inspired by [how2heap](https://github.com/shellphish/how2heap)

The goal is to have a list of common techniques for the Android Scudo Allocator given certain primitives.

Each folder represents an android version and each subfolder contains an android studio project with a proof of concept detailing the exploit.

The repository is also piloting a native-only, build-ID-specific layout without
removing the existing Android Studio projects:

```text
android_<version>/<libc-build-id>/
  target.h
  <technique>.c
  CMakeLists.txt
```

The current build-ID targets are:

- [`d6dbe2c18b0def7e9ee1655171c8af09`](android_14/d6dbe2c18b0def7e9ee1655171c8af09/)
- [`c74277f481a383c87215b672f6465e24`](android_14/c74277f481a383c87215b672f6465e24/)
- [`c001f2c6f6eddca1f38c99bd7c52019d`](android_14/c001f2c6f6eddca1f38c99bd7c52019d/)
- [`a017f07431ff6692304a0cae225962fb`](android_14/a017f07431ff6692304a0cae225962fb/)
- [`3d07239ca249ec10f6b9ffcbac96d553`](android_14/3d07239ca249ec10f6b9ffcbac96d553/)
- [`33ad5959e2b38fc822cda3c642e16c94`](android_14/33ad5959e2b38fc822cda3c642e16c94/)
- [`1d36f8ae6e0af6158793abea7d4f4f2b`](android_14/1d36f8ae6e0af6158793abea7d4f4f2b/)
- [`19c32900d9d702c303d2b4164fbba76c`](android_14/19c32900d9d702c303d2b4164fbba76c/)
- [`10f6580e623cb20b4044f6e6a4103b34`](android_14/10f6580e623cb20b4044f6e6a4103b34/)

Each builds as a standalone NDK executable and has been runtime-observed with
its exact private libc on an API 34, 4 KB emulator.
