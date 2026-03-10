# Verbosity of makefile
V ?= 0
ifeq ($(V),1)
		Q :=
		ECHO_V := @echo
else
		Q := @
		ECHO_V := @true
endif

CC ?= clang

# --- C Flags ---
# Development
DEV_FLAGS := -ggdb3 -Og \
-Wformat=2 \
-Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-missing-prototypes \
-fno-omit-frame-pointer -fno-optimize-sibling-calls \
-Wnull-dereference \
-Wstack-protector -fstack-protector-strong \
-fstack-clash-protection -fcf-protection \
-Wuninitialized -Winit-self \
-Wmisleading-indentation \
-fstrict-aliasing -Wstrict-aliasing \
-Wstrict-overflow -Winline -Wparentheses


# Compiler Specific
COMPILER_CFLAGS :=
COMPILER_LDFLAGS :=
CLANG_EXTRA_SANS :=

# Clang
ifeq (${CC}, clang)
	DEV_FLAGS += -fextend-variable-liveness -Wno-incompatible-pointer-types-discards-qualifiers -Wthread-safety
	DEV_FLAGS += -Wcast-qual -Warray-bounds-pointer-arithmetic -Wassign-enum
	DEV_FLAGS += -D_FORTIFY_SOURCE=3 -flto=thin
	COMPILER_LDFLAGS += -flto=thin
# Clang extra sanitizers (Applied in Makefile if ASAN=1)
	CLANG_EXTRA_SANS := -fsanitize=integer,implicit-conversion,local-bounds
endif
ifeq (${CC}, gcc)
	DEV_FLAGS += -Wno-discarded-qualifiers -fanalyzer
	DEV_FLAGS += --param analyzer-bb-explosion-factor=50
	DEV_FLAGS += --param analyzer-max-enodes-per-program-point=200
	DEV_FLAGS += -Wanalyzer-too-complex
	DEV_FLAGS += -D_FORTIFY_SOURCE=3 -flto
	COMPILER_LDFLAGS += -flto
endif

# Release Build
RELEASE_FLAGS := -O2 -Wmissing-prototypes
# Profile Build
PROFILE_FLAGS := -O2 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls


# --- DEVELOPMENT TOGGLES ---
# Appended to CPP_FLAGS:
PREPROC_DEFS :=

# - Use toggles here to customise build -

# 1 to use recursive find_set, 0 for iterative (default)
USE_RECURSIVE_FIND_SET = 0

ifeq ($(USE_RECURSIVE_FIND_SET), 1)
    PREPROC_DEFS += -DANU__USE_RECURSIVE_SET_FIND
endif

ifeq (${DEBUG}, 1)
	PREPROC_DEFS += -DANU_DEBUG
endif
