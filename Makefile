MAKEFLAGS += --no-print-directory --no-builtin-rules --no-builtin-variables --warn-undefined-variables --output-sync=target
.SUFFIXES:
.DELETE_ON_ERROR:
SHELL := /usr/bin/env bash
.SHELLFLAGS := -eu -o pipefail -c

# V=0 (default): silent unless a tool has something to say — compiler
# warnings/errors still show (they go to stderr); ninja progress doesn't.
# V=1: full commands via `meson compile -v`.
V ?= 0
ifeq ($(V),1)
	Q :=
	COMPILE = meson compile -v -C
	BUILD_OUT =
else
	Q := @
	COMPILE = ninja -C
	BUILD_OUT = > /dev/null
endif

# Default variant when running 'make'
VARIANT ?= debug

# ==========================================
#   Project Settings
# ==========================================
TARGET_NAME := anukrta

BUILD_ROOT := build
BUILD_DIR := $(BUILD_ROOT)/$(VARIANT)
SRC_DIR := src
VENDOR_DIR  := vendor
VENDOR_OUT  := $(BUILD_ROOT)/vendor
VENDOR_ARCS := $(VENDOR_OUT)/libsqlite3.a $(VENDOR_OUT)/libyyjson.a
TEST_DIR := tests

# ==========================================
#   Variants (Meson configurations)
# ==========================================
# Each variant is a build/<name> dir with its own setup options.

VARIANTS := debug release profile asan tsan

debug: MESON_OPTS := --buildtype=debug
release: MESON_OPTS := --buildtype=release
profile: MESON_OPTS := --buildtype=plain -Doptimization=3 -Ddebug=true
asan: MESON_OPTS := --buildtype=debugoptimized -Db_sanitize=address
tsan: MESON_OPTS := --buildtype=debugoptimized -Db_sanitize=thread

.PHONY: all $(VARIANTS) test clean clean-all bear cppcheck format lint memcheck help
.DEFAULT_GOAL := debug

# Records the options a variant dir was configured with.
SETUP_STAMP = $(BUILD_ROOT)/$@/.make-opts

define setup_once
	@if [ ! -f $(BUILD_ROOT)/$@/build.ninja ]; then \
		echo "[setup] $@ ($(MESON_OPTS))"; \
		meson setup $(BUILD_ROOT)/$@ $(MESON_OPTS) || exit 1; \
		echo '$(MESON_OPTS)' > $(SETUP_STAMP); \
	elif [ "$$(cat $(SETUP_STAMP) 2>/dev/null)" != '$(MESON_OPTS)' ]; then \
		echo "[reconfigure] $@ (options changed)"; \
		meson setup --reconfigure $(BUILD_ROOT)/$@ $(MESON_OPTS) > /dev/null || exit 1; \
		echo '$(MESON_OPTS)' > $(SETUP_STAMP); \
	fi
endef

# ==========================================
#   Build
# ==========================================
all: $(VARIANTS)

.PHONY: vendor
vendor: $(VENDOR_ARCS)

$(VENDOR_OUT)/build.ninja: $(VENDOR_DIR)/meson.build
	@if [ -f $@ ]; then \
		meson setup --reconfigure $(VENDOR_OUT) $(VENDOR_DIR); \
	else \
		meson setup $(VENDOR_OUT) $(VENDOR_DIR); \
	fi

# Grouped target (&: needs GNU make >= 4.3):
# one ninja run produces both archives, regardless of how many are out of date.
# A plain multi-target rule would run the recipe once per stale target.
$(VENDOR_ARCS) &: $(VENDOR_OUT)/build.ninja \
		$(VENDOR_DIR)/sqlite3/sqlite3.c $(VENDOR_DIR)/yyjson/yyjson.c
	ninja -C $(VENDOR_OUT)

$(VARIANTS): $(VENDOR_ARCS)
	$(setup_once)
	$(Q)$(COMPILE) $(BUILD_ROOT)/$@ $(BUILD_OUT)


# Requires -Dbuild_tests=true
test: $(VARIANT)
	@echo "--- Testing [$(VARIANT)] ---"
	$(Q)meson test -C $(BUILD_DIR) -q --no-rebuild --print-errorlogs

# ==========================================
#   Clean
# ==========================================

clean:
	@echo "Cleaning up build dir for $(VARIANT)"
	$(Q)rm -rf $(BUILD_DIR)

clean-all:
	@echo "Cleaning up all build files and object files"
	$(Q)rm -rf $(BUILD_ROOT)
	@echo "Removed build/"

# ==========================================
#   Tooling
# ==========================================

iwyu: bear
	@iwyu-tool -p compile_commands.json -e $(TEST_DIR) -e $(VENDOR_DIR)

bear: debug
	$(Q)ln -sf $(BUILD_ROOT)/debug/compile_commands.json compile_commands.json
	@echo "compile_commands.json -> $(BUILD_ROOT)/debug/compile_commands.json"

cppcheck:
	$(Q)./scripts/cppcheck.sh $(BUILD_DIR)/compile_commands.json

format:
	$(Q)./scripts/clang-format.sh

lint: $(VARIANT)
	$(Q)./scripts/clang-tidy.sh $(BUILD_DIR)

memcheck: debug
	$(Q)./scripts/valgrind.sh $(BUILD_ROOT)/debug $(ARGS)

# ==========================================
#   Misc
# ==========================================
print-%: ; @echo $*=$($*)

help:
	@echo "Build variants : $(VARIANTS)  (make asan, make release, ...)"
	@echo "Build libs     : make vendor"
	@echo "Clean          : clean | clean-all"
	@echo "Tooling        : bear | format | lint | memcheck"
