# anukrta

<center>
<img width=200 src="./etc/assets/anukrta_pengu.webp" alt="Drawing of a penguin sitting on VHS tapes." />
</center>

## Overview

__Anukṛta__ is a Sanskrit word meaning 'copied' or 'imitated'.

It is a tool designed to detect similar videos.

Behind the scenes, it uses perceptual hashing to generate fingerprints for video frames.
The hashes from different videos are then compared and similar ones are grouped and reported.

## Features

- Dynamic [letter-boxing](https://en.wikipedia.org/wiki/Letterboxing_(filming)) detection.
- Skip videos by specifying a minimum duration length.
- Multi-threaded.
- Not heavy on dependencies.

## Building

### Requirements

- Clang/GCC to compile the project
- `FFmpeg` libraries (_libav\*_)
- Criterion (for unit testing)
- A \*Nix environment (not tested on Windows).

### Commands

- Run `make release` and it'll produce a binary in `build/release`.
- Note that you can look into the Makefile and check out the other build types.

## Usage

The CLI is basic enough to use, you can specify directories (it will default to the current path if none specified).

The directories you specify should have video files you want to analyse and hash to detect similarity.

`anukrta` will search for videos recursively, so it does not matter if files are nested.

A report will be printed containing groups of similar video files.

### Basics

```shell
anukrta [arguments...] [paths...]
```

- Not specifying a path is fine, it will just scan the current directory.

### Example Output:

```
[+] Group #1 (3 items):
  /home/user/videos/video1.mov
    -> [...] | size: ... KiB | time: ... | duration: ...
    -> Hashes: [ ... ]
  /home/user/videos/video2.mp4
    -> [...] | size: ... KiB | time: ... | duration: ...
    -> Hashes: [ ... ]
  /home/user/videos/video3.mp4
    -> [...] | size: ... MiB   | time: ... | duration: ...
    -> Hashes: [ ... ]

[+] Group #2 (2 items):
  /home/user/videos/video4.mp4
    -> [...] | size: ... MiB   | time: ... | duration: ...
    -> Hashes: [ ... ]
  /home/user/videos/video5.mov
    -> [...] | size: ... MiB   | time: ... | duration: ...
    -> Hashes: [ ... ]
```

### Options and Flags

- `-s [int] | --segments [int]` : Number of frames to hash from each video.
  - Default is 3.
  - Warning: Do not make this number too high.
- `-t [int] | --threshold [int]` : Threshold of similarity required to group files together.
  - Values range from 0 to 64, where 0 is most similar.
  - Default is 15.
- `--threads [int]` : Number of threads to run in parallel.
  - Defaults to using all available cores.
- `--skip-duration [int]` : Videos shorter than this duration (in seconds) will be skipped.
- `-v | --verbose` : Enable verbose output.
- `-h | --help` : Print help for the CLI.
- `--version` : Print version information.
- `--dry-run` : Does not do anything yet.
- `--detect-bars` : Detect window boxing.
- `--detect-rotation` : Does not do anything yet.
