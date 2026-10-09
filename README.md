# anukrta

<center>
<img width=200 src="./etc/assets/anukrta_pengu.webp" alt="Drawing of a penguin sitting on VHS tapes." />
</center>

## Overview

**Anukṛta** is a Sanskrit word meaning 'copied' or 'imitated'.

It is a tool designed to detect similar videos.

Behind the scenes, it uses perceptual hashing to generate fingerprints for video frames.
The hashes from different videos are then compared and similar ones are grouped and reported.

## Features

- Dynamic [letter-boxing](<https://en.wikipedia.org/wiki/Letterboxing_(filming)>) detection.
- Skip videos by specifying a minimum duration length.
- Multi-threaded.
- Not heavy on dependencies.

## Building

### Requirements

- Clang/GCC with C23 support (you probably have this).
- Meson (>= 1.3) and Ninja (you probably already have this).
- pkg-config (you probably have this).
- `FFmpeg` libraries (*libav\**) with development headers
- A *\*Nix* environment (not tested on Windows).
- *Optional*: `Criterion` (unit tests), plus `cppcheck`, `clang-tidy`,
  `clang-format` and `Valgrind` for development tooling.

`SQLite3` and `yyjson` are vendored under `vendor/` - no system packages are
needed for them.

### Quick start

```shell
make release # release build -> build/release/anukrta
```

## Usage

The CLI is basic enough to use, you can specify directories (it will default to the current path if none specified).

The directories you specify should have video files you want to analyse and hash to detect similarity.

`anukrta` will search for videos recursively, so it does not matter if files are nested.

A report will be printed containing groups of similar video files.

### Basics

```shell
anukrta [OPTIONS...] -- [PATH]...
```

- Preceding the paths with `--` is recommended, so filenames starting with `-` are never mistaken for options.
- Not specifying a path is fine, it will just scan the current directory.
- Boolean options accept `yes/no`, `true/false`, `on/off` or `1/0` (e.g. `--cache=no`).
  - If a boolean option (e.g. `--cache`) is used bare, it defaults to **true**.

### Options and Flags

**General**

- `-h | --help` : Print help for the CLI.
- `--version` : Print version information.
- `-v, --verbose[=N]` : Enable verbose output.
  - `-v` is repeatable: `-vvv` is the same as `--verbose=3`.
  - `--verbose=N` sets an explicit level, where N is 0–4.
  - `--verbose=0` is the same as if you had not specified the argument at all (it will run in quiet mode).
  - Currently verbosity settings affect `libav` logging too. We will decouple the two soon.
  - `-v | --verbose=1` Serious error messages.
  - `-vv | --verbose=2`: Warnings/informational messages.
  - `-vvv | --verbose=3`: Verbose messages.
  - `-vvvv | --verbose=4`: Trace level messages.
- `--dry-run` : **TODO** Does not do anything yet.

**Algorithm & Tuning**

- `-s, --segments=N` : Number of frames to hash from each video.
  - The default number of segments is 3.
  - Warning: Do not make this number too high or it will slow operation down significantly.
- `-t, --threshold=N` : Threshold required to consider files *similar*.
  - Values range from 0 to 64, where 0 is most similar.
  - Default is 15.
- `--skip-duration=N` : Videos shorter than this duration (in seconds) will be skipped.

**Detection**

- `--detect-bars[=bool (default true)]` : Detect window boxing and automatically crop frames (before hashing) to exclude the bars.
- `--detect-black[=bool (default true)]` : Detect black frames and skip hashing them.
- `--detect-rotation[=bool (default true)]` : Read metadata from video and apply rotation if it is specified.
  - Quite useful for iPhone videos where the video rotation is specified in the metadata.

**Report**

- `--format=(text|json)` : Format of the report. Currently `text` and `json` are supported (default: `text`).
- `--print-hashes[=bool (default false)]` : Print hashes produced by video in the report.
- `--print-unique[=bool (default true)]` : Print unique file list in the report.
- `--human-size[=bool (default true)]` : Print file sizing in human readable format (e.g 1024 => '1 KiB').
- `--human-time[=bool (default true)]` : Print times in human readable format (rather than UNIX timestamps).

**Execution & Storage**

- `--threads=N` : Number of threads to run in parallel.
  - 0 (the default) uses all available cores; values above the core count are capped.
- `--cache[=bool (default true)]` : Save results to a local database so it can be reused on another run.
- `--progress-bar[=bool (default true)]` : Will display a progress bar.
  - Warning: If verbosity level is above 0, then the progress bar is disabled automatically.
  - This limitation will be solved once logging to a file is implemented.

## Developer Notes

On the first build the vendored libraries are compiled once into
`build/vendor/` and are then shared by every variant, so they are not
rebuilt unless you run `make clean-all`.

Build output is quiet unless a tool has something to say.

Pass `V=1` to any target for full compiler output, e.g. `make release V=1`.

### Makefile Targets/Variants

| Variant | Command        | Description                                            |
| ------- | -------------- | ------------------------------------------------------ |
| debug   | `make debug`   | Default, `-O0`, many warnings, `-DAK_DEBUG`            |
| release | `make release` | `-O3`, `-DNDEBUG`                                      |
| profile | `make profile` | `-O3` with debug info and frame pointers; no dev flags |
| asan    | `make asan`    | AddressSanitizer, dev warnings, `AK_DEBUG`             |
| tsan    | `make tsan`    | ThreadSanitizer, dev warnings, `AK_DEBUG`              |

Each variant gets its own directory under `build/`.

`make all` builds every variant.

### Tests

Tests use `Criterion` and are disabled by default.

Enable them on a build dir and run:

```shell
meson configure build/debug -Dbuild_tests=true
make test VARIANT=debug
```

- (`-Dbuild_tests=true` can also be passed to `meson setup` directly.)
- Tests are skipped entirely for the release variant, and no test binary is built if `Criterion` is not installed.

### Build options

Defined in `meson.options`; set at setup with `-Doption=value` or afterwards
with `meson configure`:

| Option             | Default | Description                                              |
| ------------------ | ------- | -------------------------------------------------------- |
| `fast_math`        | `true`  | Adds `-ffast-math` in optimised builds (release/profile) |
| `build_tests`      | `false` | Build the `Criterion` test binary                        |
| `local_ffmpeg_dir` | `''`    | Prefix of a locally-built FFmpeg; adds an rpath.         |
|                    |         | Pair with `-Dpkg_config_path=<dir>/lib/pkgconfig`        |

### Cleaning

```shell
make clean                  # remove build/<VARIANT>   (default: debug)
make clean-all              # remove the whole build/ tree
```

### Tooling

All static-analysis targets read the per-variant compile database
(`build/<variant>/compile_commands.json`), so they see exactly the flags the
compiler sees. Run a build first so the database exists.

- `make format`: clang-format all project sources in place.
- `make lint [VARIANT=...]`: clang-tidy via the compile database.
- `make cppcheck [VARIANT=...]`: `cppcheck` via the compile database.
- `make memcheck [ARGS="..."]`: valgrind memcheck on the debug binary
  (always the debug variant; sanitizers and `valgrind` do not mix).
- `make bear`: symbolically link the debug compile database to the repo root, for
  `clangd` and other tools that expect `compile_commands.json` there.

## License

anukrta is free software licensed under the GNU General Public License v3 -
see [LICENSE](LICENSE) for the full text.
