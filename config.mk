# Verbosity of makefile

COMPILER_ID := $(shell $(CC) --version 2>/dev/null | grep -qi clang && echo clang || echo gcc)

PREPROC_DEFS :=

# --- C Flags ---
# Development
DEV_FLAGS := -Og -g3 \
-Wformat=2 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wnull-dereference \
-Wstack-protector -fstack-protector-strong -fstack-clash-protection -fcf-protection \
-Wmisleading-indentation -fstrict-aliasing -Wstrict-aliasing -Wstrict-overflow \
-Wparentheses -Warray-parameter -Wunused -Wimplicit-fallthrough

# Release Build
RELEASE_FLAGS := -O3 -ffast-math -Winline

# Profiling Build
PROFILE_FLAGS := $(RELEASE_FLAGS) -g3 -fno-omit-frame-pointer

# Compiler Specific
COMPILER_CFLAGS :=
COMPILER_LDFLAGS :=
COMPILER_RELEASE_LDFLAGS :=
CLANG_EXTRA_SANS :=
TEST_COMPILER_CFLAGS :=

# Clang
ifeq ($(COMPILER_ID), clang)
	DEV_FLAGS += -fextend-variable-liveness -Wthread-safety \
	-Wcast-qual -Warray-bounds-pointer-arithmetic -Wassign-enum -Warray-parameter
	CLANG_EXTRA_SANS := -fsanitize=integer,implicit-conversion,local-bounds
	COMPILER_RELEASE_LDFLAGS += -flto=thin
	RELEASE_FLAGS += -flto=thin
	TEST_COMPILER_CFLAGS += -fmacro-backtrace-limit=1
endif

# GCC
ifeq ($(COMPILER_ID), gcc)
	DEV_FLAGS += -fanalyzer --param analyzer-bb-explosion-factor=50 \
--param analyzer-max-enodes-per-program-point=200 -Wno-analyzer-too-complex \
-Wuseless-cast -Wuse-after-free=3
# -Wno-discarded-qualifiers
  COMPILER_RELEASE_LDFLAGS += -flto
  RELEASE_FLAGS += -flto
	TEST_COMPILER_CFLAGS += -ftrack-macro-expansion=0
endif

# --- DEVELOPMENT TOGGLES ---
# Appended to CPP_FLAGS:

# - Use toggles here to customise build -

# 1 to use recursive find_set, 0 for iterative (default)
USE_RECURSIVE_FIND_SET = 0

ifeq ($(USE_RECURSIVE_FIND_SET), 1)
    PREPROC_DEFS += -DANU__USE_RECURSIVE_SET_FIND
endif
