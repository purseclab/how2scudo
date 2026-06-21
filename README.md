# how2scudo

This repo is inspired by [how2heap](https://github.com/shellphish/how2heap)

The goal is to have a list of common techniques for the Android Scudo Allocator given certain primitives.

Each folder represents an android version and each subfolder contains an android studio project with a proof of concept detailing the exploit.


# Known cookie offsets

| Version | Offset |
| -------- | -------- |
| Android Studio 12 latest | 0xd06c0 |
| Android Studio 13 latest | 0xcc440 |
| Android Studio 14 latest | 0xf7480 |
