project(
    'anukrta',
    'c',
    version: '0.1.0',
    default_options: [
        'c_std=gnu23',  # TODO use 'gnu2x' on Meson < 1.3
        'buildtype=debugoptimized',
        'warning_level=2',  # Warnings also managed below
    ],
    license: 'GPL-3.0-or-later',
    meson_version: '>=1.3',  # need for c23
)

cc = meson.get_compiler('c')
cc_id = cc.get_id()
is_gcc = cc_id == 'gcc'
is_clang = cc_id == 'clang'

fs = import('fs')

buildtype = get_option('buildtype')

main_file = 'src/main.c'

core_sources = files(
    'src/cache.c',
    'src/cache.h',
    'src/cli.c',
    'src/cli.h',
    'src/config.c',
    'src/config.h',
    'src/defs.h',
    'src/explore.c',
    'src/explore.h',
    'src/fourcc.h',
    'src/hash.c',
    'src/hash.h',
    'src/mem.h',
    'src/report.c',
    'src/report.h',
    'src/signals.c',
    'src/signals.h',
    'src/term.c',
    'src/term.h',
    'src/tree.c',
    'src/tree.h',
    'src/ui.c',
    'src/ui.h',
    'src/util.c',
    'src/util.h',
    'src/video.c',
    'src/video.h',
    'src/kvec.h',
    'src/log.c',
    'src/log.h',
)

# Criterion tests. `files()` with no args is a valid empty list.
test_sources = files(
    'tests/test_bktree.c',
    'tests/test_cli.c',
    'tests/test_files.c',
    'tests/test_hashing.c',
    'tests/test_report.c',
    'tests/test_utils.c',
)

# ============================================================
#   Flags
# ============================================================
warning_flags = cc.get_supported_arguments(
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wshadow',
    '-Wvla',
    '-Wconversion',
    '-Wsign-conversion',
    '-Wdouble-promotion',
    '-Wmissing-include-dirs',
    '-Wnested-externs',
    '-Wredundant-decls',
    '-Wold-style-definition',
    '-Wunused-function',
    '-Wunused-parameter',
    '-Wunused-variable',
    '-Wuninitialized',
    '-Warray-parameter',
    '-Winit-self',
    '-Wundef',
    '-Wattributes',
    '-Wignored-attributes',
    '-Wnonnull',
    '-Wunused-result',
    '-Wmissing-noreturn',
    '-Wmissing-format-attribute',
    checked: 'require',
)

dev_flags = cc.get_supported_arguments(
    '-g3',
    '-Walloc-size',
    '-Wcast-qual',
    '-Wformat=2',
    '-Wimplicit-fallthrough',
    '-Wmisleading-indentation',
    '-Wmissing-field-initializers',
    '-Wnull-dereference',
    '-Wparentheses',
    '-Wstack-protector',
    '-Wstrict-aliasing',
    '-Wstrict-overflow',
    '-Warray-bounds-pointer-arithmetic',
    '-Wthread-safety',
    '-Wpedantic-macros',
    '-Wassign-enum',
    '-Wunused',
    '-Wsuggest-attribute=const',
    '-Wsuggest-attribute=format',
    '-Wsuggest-attribute=malloc',
    '-Wsuggest-attribute=noreturn',
    '-Wsuggest-attribute=pure',
    '-Wsuggest-final-types',
    '-Wuse-after-free=3',
    '-fstrict-overflow',
    '-fstrict-aliasing',
    '-fcf-protection',
    '-fno-omit-frame-pointer',
    '-fno-optimize-sibling-calls',
    '-fstack-clash-protection',
    '-fstack-protector-strong',
    '-ftrapv',
    '-fextend-variable-liveness',
)

add_project_arguments(warning_flags, language: 'c')

# debug / asan / tsan builds
if buildtype in ['debug']
    add_project_arguments(dev_flags, language: 'c')
    add_project_arguments('-DAK_DEBUG', language: 'c')
endif

# release / profile (-O3 + -ffast-math; profile keeps debug info + frame pointers)
if get_option('optimization') == '3'
    if get_option('fast_math')
        add_project_arguments('-ffast-math', language: 'c')
    endif
    add_project_arguments('-DNDEBUG', language: 'c')

    if get_option('debug')
        add_project_arguments('-g3', '-fno-omit-frame-pointer', language: 'c')
    endif
endif


# ============================================================
#   Toggles / linker
# ============================================================
# if get_option('something defined in meson.options')
#     add_project_arguments('-DSOME_CPP_FLAG', language: 'c')
# endif


# ============================================================
#   FFmpeg
# ============================================================
local_ffmpeg = get_option('local_ffmpeg_dir')
ffmpeg_rpath = ''
ffmpeg_summary = 'system'
if local_ffmpeg != ''
    ffmpeg_rpath = local_ffmpeg / 'lib'
    ffmpeg_summary = local_ffmpeg
    message('Using LOCAL FFmpeg at: ' + local_ffmpeg)
    message(
        '  -> configure with: -Dpkg_config_path=' + local_ffmpeg / 'lib/pkgconfig',
    )
endif

ffmpeg_deps = []
foreach m : [
    'libavdevice',
    'libavfilter',
    'libavformat',
    'libavcodec',
    'libswresample',
    'libswscale',
    'libavutil',
]
    ffmpeg_deps += dependency(m)
endforeach

thread_dep = dependency('threads')
m_dep = cc.find_library('m', required: true)

# ============================================================
#   Includes
# ============================================================
src_inc = include_directories('src')


# ============================================================
#   Targets
# ============================================================
vendor_dir = meson.project_source_root() / 'build' / 'vendor'
if not fs.exists(vendor_dir / 'libsqlite3.a') or not fs.exists(
    vendor_dir / 'libyyjson.a',
)
    error(
        'vendored archives missing in ' + vendor_dir + ' — run `make vendor` first, then reconfigure.',
    )
endif

sqlite3_dep = declare_dependency(
    dependencies: cc.find_library(
        'sqlite3',
        dirs: vendor_dir,
        static: true,
        required: true,
    ),
    include_directories: include_directories('vendor/sqlite3'),
)

yyjson_dep = declare_dependency(
    dependencies: cc.find_library(
        'yyjson',
        dirs: vendor_dir,
        static: true,
        required: true,
    ),
    include_directories: include_directories('vendor/yyjson'),
)

anukrta_core = static_library(
    'anukrta_core',
    core_sources,
    include_directories: [src_inc],
    dependencies: [ffmpeg_deps, yyjson_dep, sqlite3_dep],
)

anukrta_dep = declare_dependency(
    link_with: anukrta_core,
    include_directories: [src_inc],
    dependencies: [
        ffmpeg_deps,
        thread_dep,
        m_dep,
    ],
)

anukrta_exe = executable(
    'anukrta',
    main_file,
    dependencies: [anukrta_dep],
    build_rpath: ffmpeg_rpath,
    install_rpath: ffmpeg_rpath,
    install: true,
)

sanitize = get_option('b_sanitize')
san_env = []
if sanitize == 'address'
    san_env = ['ASAN_OPTIONS=detect_leaks=1:halt_on_error=1']
elif sanitize == 'thread'
    san_env = ['TSAN_OPTIONS=halt_on_error=1']
endif

# ============================================================
#   Tests (criterion)
# ============================================================
if get_option('build_tests') and buildtype != 'release'
    criterion_dep = dependency('criterion', required: false)
    if criterion_dep.found() and test_sources.length() > 0
        test_cflags = cc.get_supported_arguments(
            ['-w', '-ftrack-macro-expansion=0', '-fmacro-backtrace-limit=1'],
        )
        nproc = run_command('nproc', check: false).stdout().strip()
        if nproc == ''
            nproc = '4'
        endif
        test_exe = executable(
            'unit_tests',
            test_sources,
            dependencies: [anukrta_dep, criterion_dep],
            c_args: test_cflags,
            build_rpath: ffmpeg_rpath,
        )
        test(
            'unit',
            test_exe,
            args: ['-j' + nproc],
            env: san_env,
            timeout: 120,
        )
    endif
endif

summary(
    {
        'Compiler': cc.get_id() + ' ' + cc.version(),
        'Buildtype': buildtype,
        'Sanitizers': sanitize,
        'FFmpeg': ffmpeg_summary,
    },
    section: 'anukrta',
)
