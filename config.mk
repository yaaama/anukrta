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


PREPROC_DEFS :=

# --- C Flags ---
# Development
DEV_FLAGS := -Og -g3 \
-Wformat=2 -Wuse-after-free=3 \
-fno-omit-frame-pointer -fno-optimize-sibling-calls \
-Wnull-dereference \
-Wstack-protector -fstack-protector-strong \
-fstack-clash-protection -fcf-protection \
-Wmisleading-indentation \
-fstrict-aliasing -Wstrict-aliasing \
-Wstrict-overflow -Wparentheses \
-Warray-parameter -Wunused -Wimplicit-fallthrough

# Release Build
RELEASE_FLAGS := -O3 -ffast-math -Winline

# Profiling Build
PROFILE_FLAGS := $(RELEASE_FLAGS) -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls

# Compiler Specific
COMPILER_CFLAGS :=
COMPILER_LDFLAGS :=
CLANG_EXTRA_SANS :=

# Clang
ifeq (${CC}, clang)
	DEV_FLAGS += -fextend-variable-liveness -Wthread-safety \
-Wcast-qual -Warray-bounds-pointer-arithmetic -Wassign-enum -Warray-parameter
# -Wno-incompatible-pointer-types-discards-qualifiers
	CLANG_EXTRA_SANS := -fsanitize=integer,implicit-conversion,local-bounds
endif
ifeq (${CC}, gcc)
	DEV_FLAGS += -fanalyzer --param analyzer-bb-explosion-factor=50 \
--param analyzer-max-enodes-per-program-point=200 -Wanalyzer-too-complex \
-Wuseless-cast
# -Wno-discarded-qualifiers
endif

ifeq (${CC}, clang)
    COMPILER_RELEASE_LDFLAGS += -flto=thin
    RELEASE_FLAGS += -flto=thin
endif
ifeq (${CC}, gcc)
    COMPILER_RELEASE_LDFLAGS += -flto
    RELEASE_FLAGS += -flto
endif

# --- DEVELOPMENT TOGGLES ---
# Appended to CPP_FLAGS:

# - Use toggles here to customise build -

# 1 to use recursive find_set, 0 for iterative (default)
USE_RECURSIVE_FIND_SET = 0

ifeq ($(USE_RECURSIVE_FIND_SET), 1)
    PREPROC_DEFS += -DANU__USE_RECURSIVE_SET_FIND
endif

# ANU_DEBUG
ifeq (${DEBUG}, 1)
	PREPROC_DEFS += -DANU_DEBUG
endif
