# vendor

project(
    'vendored-deps',
    'c',
    default_options: ['buildtype=plain', 'warning_level=0', 'werror=false'],
)

cc = meson.get_compiler('c')

vendor_cflags = cc.get_supported_arguments(
    '-O3',
    '-g',
    '-w',
    '-DNDEBUG',
)

# SQLITE Build

sqlite3_custom_flags = [
    '-DSQLITE_THREADSAFE=1',
    '-DSQLITE_DEFAULT_MEMSTATUS=0',
    '-DSQLITE_OMIT_LOAD_EXTENSION=1',
    '-DSQLITE_LIKE_DOESNT_MATCH_BLOBS=0',
    '-DSQLITE_MAX_EXPR_DEPTH=0',
    '-DSQLITE_OMIT_DECLTYPE=1',
    '-DSQLITE_OMIT_DEPRECATED=1',
    '-DSQLITE_OMIT_SHARED_CACHE=1',
    '-DSQLITE_OMIT_AUTOINIT=1',
    '-DSQLITE_STRICT_SUBTYPE=1',
    '-DSQLITE_ENABLE_FTS5=1',
]

static_library(
    'sqlite3',
    'sqlite3/sqlite3.c',
    c_args: vendor_cflags,
    include_directories: include_directories('sqlite3'),
    pic: true,
    override_options: ['b_sanitize=none'],
    dependencies: cc.find_library('dl', required: true),
    install: false,
)

static_library(
    'yyjson',
    'yyjson/yyjson.c',
    c_args: vendor_cflags,
    include_directories: include_directories('yyjson'),
    pic: true,
    install: false,
)

