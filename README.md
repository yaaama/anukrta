# anukrta

A tool designed to detect videos that are duplicates/similar to one another.

## Requirements

- FFmpeg libraries _(libav\*_)
- Criterion (for unit testing)
- A \*Nix environment (not tested on Windows).

## Building

- Run `make` and it'll produce a binary in `build/`.

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

```text
=== Duplicate Report: ===
Found 2 duplicate groups from 8 files
----------------------------------------
[+] Group #1 (2 items):
    - etc/reference//cow_adjusted_color.mp4
    - etc/reference//cow.mov

[+] Group #2 (4 items):
    - etc/reference//video_bad.mov
    - etc/reference//video_3.mp4
    - etc/reference//video.mov
    - etc/reference//video_shortened.mkv
```

### Options and Flags

- `-s [int] | --segments [int]` : Number of frames to hash from each video (default is 3).
- `-v | --verbose` : Verbose output.
- `-h | --help` : Print help.
- `--version` : Print version information.
- `--dry-run` : Does not do anything yet.
- `--detect-black` : Does not do anything yet.
- `--detect-rotation` : Does not do anything yet.
