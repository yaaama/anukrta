# anukrta

A tool designed to detect videos that are duplicates/similar to one another.

## Requirements

- Clang/GCC to compile the project
- FFmpeg libraries _(libav\*_)
- Criterion (for unit testing)
- A \*Nix environment (not tested on Windows).

## Building

- Run `make release` and it'll produce a binary in `build/release`.

## Usage

The CLI is basic enough to use, you can specify directories (it will default to the current path if none specified).

The directories you specify should have video files you want to analyse and hash to detect similarity.

`anukrta` will search for videos recursively, so it does not matter if files are nested.

A report will be printed containing groups of similar video files.

### Basic Usage

```shell
anukrta [paths...]
```

Example Output:

```
=== Duplicate Report: ===
Found 2 duplicate groups from 36 files
--------------------------------------------------------------------------------

[+] Group #1 (2 items):
  ./etc/reference/cow_adjusted_color.mp4
    size: 3.80 MiB   | time: 14-02-2026 19:37 | duration: 20.00s
  ./etc/reference/cow.mov
    size: 6.11 MiB   | time: 16-02-2021 17:47 | duration: 20.00s

[+] Group #2 (3 items):
  ./etc/reference/video_bad.mov
    size: 485.47 KiB | time: 19-01-2026 21:30 | duration: 6.97s
  ./etc/reference/video_3.mp4
    size: 520.61 KiB | time: 02-02-2026 23:24 | duration: 6.97s
  ./etc/reference/video.mov
    size: 3.56 MiB   | time: 21-01-2026 11:47 | duration: 6.97s
```


### Options and Flags

- `-s [int] | --segments [int]` : Number of frames to hash from each video.
    - Default is 3.
    - Warning: Do not make this number too high.
- `-t [int] | --threshold [int]` : Threshold of similarity required to group files together.
    - Values range from 0 to 64, where 0 is most similar. Default is 15.
- `--threads [int]` : Number of threads to run in parallel.
- `--skip-duration [int]` : Videos shorter than this duration will be skipped.
- `-v | --verbose` : Enable verbose output.
- `-h | --help` : Print help for the CLI.
- `--version` : Print version information.
- `--dry-run` : Does not do anything yet.
- `--detect-black` : Does not do anything yet.
- `--detect-rotation` : Does not do anything yet.
