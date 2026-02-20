# Verbosity of makefile
V ?= 0
ifeq ($(V),1)
		Q :=
else
		Q := @
endif

CC ?= clang


# --- C Flags ---
# Development
DEV_FLAGS := -ggdb \
-O0 \
-g3 \
-Wno-unused-function \
-Wno-unused-parameter \
-Wno-unused-variable \
-Wold-style-definition \
-fno-omit-frame-pointer \
-fno-optimize-sibling-calls \
-ftrapv \
-fstack-protector-all -Wstack-protector

ifeq (${CC}, clang)
	DEV_FLAGS += -fextend-variable-liveness -Wno-incompatible-pointer-types-discards-qualifiers
endif
ifeq (${CC}, gcc)
	DEV_FLAGS += -Wno-discarded-qualifiers
endif

# Release Build
RELEASE_FLAGS := -O2 -Wmissing-prototypes

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
