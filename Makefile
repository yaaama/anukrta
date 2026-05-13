MAKEFLAGS += --no-print-directory

VARIANT ?= debug
SANITIZER ?= none
DEBUG ?= 1
SKIP_TESTS ?= 0

USE_CCACHE ?= 0
USE_MOLD ?= 0
USE_LOCAL_FFMPEG ?= 0

V ?= 0
ifeq ($(V),1)
		Q :=
		ECHO_V := @echo
else
		Q := @
		ECHO_V := @true
endif

CC ?= clang
LDFLAGS ?=

# ==========================================
#   Toolchain Setup
# ==========================================
ifeq ($(USE_MOLD), 1)
	LDFLAGS += -fuse-ld=mold
endif

ifeq ($(USE_CCACHE), 1)
	CCACHE_BIN := $(shell command -v ccache 2> /dev/null)
	ifneq ($(CCACHE_BIN),)
		CC := $(CCACHE_BIN) $(CC)
	endif
endif

# Load config
include config.mk

# ==========================================
#   Project Settings
# ==========================================
TARGET_NAME := anukrta
TEST_TARGET_NAME := test

SRC_DIR := src
BUILD_ROOT := build
BUILD_DIR := $(BUILD_ROOT)/$(VARIANT)
OBJ_DIR := $(BUILD_DIR)/objs
VENDOR_DIR := vendor
TEST_DIR := tests

# DEFAULT C FLAGS
CFLAGS := -std=gnu23 \
-Wall -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wvla \
-Wconversion -Wsign-conversion -Wdouble-promotion -Wmissing-include-dirs \
-Wnested-externs -Wredundant-decls -Wold-style-definition \
-Wunused-function -Wunused-parameter -Wunused-variable -Wmissing-prototypes \
-Wjump-misses-init -Wuninitialized -Warray-parameter -Winit-self -Wundef

# Inject Compiler-specific flags from config.mk
CFLAGS += $(COMPILER_CFLAGS)
LDFLAGS += $(COMPILER_LDFLAGS)
INCLUDES := $(addprefix -I,$(VENDOR_DIR) $(SRC_DIR))
CPPFLAGS := $(INCLUDES) -MMD -MP $(PREPROC_DEFS)

# Release or Profile
ifneq ($(filter profile release,$(VARIANT)),)
	PREPROC_DEFS += -DNDEBUG
	LDFLAGS += $(COMPILER_RELEASE_LDFLAGS)

	ifeq ($(VARIANT), release)
		CFLAGS += $(RELEASE_FLAGS)
	else ifeq ($(VARIANT), profile)
		CFLAGS += $(PROFILE_FLAGS)
	endif

else ifeq ($(VARIANT), asan)
	CFLAGS += $(DEV_FLAGS) -fno-optimize-sibling-calls -fno-omit-frame-pointer
	CPPFLAGS += -DANU_DEBUG
	SAN_FLAGS := -fsanitize=address,undefined,unreachable -fsanitize-address-use-after-scope $(CLANG_EXTRA_SANS)
else ifeq ($(VARIANT), tsan)
	CFLAGS += $(DEV_FLAGS) -fno-omit-frame-pointer
	CPPFLAGS += -DANU_DEBUG
	SAN_FLAGS := -fsanitize=thread,undefined,unreachable $(CLANG_EXTRA_SANS)
# Debug Build
else
	CFLAGS += $(DEV_FLAGS)
	PREPROC_DEFS += -DANU_DEBUG
endif

# ==========================================
# Sanitisers
# ==========================================

ifneq ($(SAN_FLAGS),)
	CFLAGS += $(SAN_FLAGS)
	LDFLAGS += $(SAN_FLAGS)

  # Sanitizer runtime options
  ASAN_LOG_FILE := etc/asan.log
  export ASAN_OPTIONS := detect_leaks=1:abort_on_error=1:halt_on_error=1:log_path=$(ASAN_LOG_FILE):strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1:strict_init_order=1:$(ASAN_OPTIONS)
  export LSAN_OPTIONS := log_threads=1:$(LSAN_OPTIONS)
  export UBSAN_OPTIONS := abort_on_error=1:halt_on_error=1:print_stacktrace=1:$(UBSAN_OPTIONS)
  export TSAN_OPTIONS := enable_adaptive_delay=1:adaptive_delay_aggressiveness=50:halt_on_error=1:$(TSAN_OPTIONS)
endif

# ==========================================
#   FFmpeg Configuration
# ==========================================
# --- Local FFmpeg Configuration ---
# Set to 1 to use a custom locally-built FFmpeg instead of the system one
FFMPEG_MODULES := libavdevice libavfilter libavformat libavcodec libswresample libswscale libavutil

ifeq ($(USE_LOCAL_FFMPEG), 1)
  # The path to the 'prefix' where your local FFmpeg was installed
  # (the directory containing 'include' and 'lib' folders for ffmpeg)
  LOCAL_FFMPEG_DIR ?=
  # Point pkg-config to our local FFmpeg installation
	PKG_CONFIG_CMD := PKG_CONFIG_PATH="$(LOCAL_FFMPEG_DIR)/lib/pkgconfig" pkg-config
  # Tell the resulting binary where to find the local shared libraries at runtime
	LDFLAGS += -Wl,-rpath=$(LOCAL_FFMPEG_DIR)/lib
  $(info [INFO] Using LOCAL FFmpeg located at: $(LOCAL_FFMPEG_DIR))
else
# Use standard system pkg-config
	PKG_CONFIG_CMD := pkg-config
endif
# Get the raw flags from pkg-config (e.g., "-I/usr/include -D_GNU_SOURCE")
FFMPEG_CFLAGS_RAW := $(shell $(PKG_CONFIG_CMD) --cflags $(FFMPEG_MODULES))
# Replace "-I/path" to "-isystem /path" to silence third-party warnings
FFMPEG_CFLAGS := $(patsubst -I%,-isystem %,$(FFMPEG_CFLAGS_RAW))
FFMPEG_LIBS := $(shell $(PKG_CONFIG_CMD) --libs $(FFMPEG_MODULES))

CFLAGS += $(FFMPEG_CFLAGS)
LDFLAGS += -Wl,--as-needed
LDLIBS := $(FFMPEG_LIBS) -lm -lpthread

# ==========================================
#   Vendor Compilation Flags
# ==========================================
# We want vendor code to be optimised + no warnings + no debugging
VENDOR_CFLAGS := -O3 -g -w -DNDEBUG $(COMPILER_CFLAGS) $(SAN_FLAGS)

TEST_CFLAGS := $(CFLAGS) -w $(TEST_COMPILER_CFLAGS)

# ==========================================
#   Programmatic File Discovery
# ==========================================

SRC_SOURCES := $(shell find $(SRC_DIR) -name '*.c')
TEST_SOURCES := $(shell find $(TEST_DIR) -name '*.c' 2>/dev/null)
VENDOR_SOURCES := $(wildcard $(VENDOR_DIR)/*.c)

# Create object file names (src/main.c -> obj/main.o)
SRC_OBJECTS    := $(SRC_SOURCES:%.c=$(OBJ_DIR)/%.o)
VENDOR_OBJECTS := $(VENDOR_SOURCES:%.c=$(OBJ_DIR)/%.o)
TEST_OBJECTS := $(TEST_SOURCES:%.c=$(OBJ_DIR)/%.o)

OBJECTS := $(SRC_OBJECTS) $(VENDOR_OBJECTS)

# Helper for testing (filter out main.o)
MAIN_OBJ := $(OBJ_DIR)/$(SRC_DIR)/main.o
LIB_OBJECTS := $(filter-out $(MAIN_OBJ), $(OBJECTS))
# Dependency files
DEPS := $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)


# ==========================================
#   Build Rules
# ==========================================

# Determine which targets to build
TARGETS_TO_BUILD := $(BUILD_DIR)/$(TARGET_NAME)

# Skip building tests when refactoring etc
SKIP_TESTS ?= 0

#  Skip building tests for release/profile or if explicitly skipped
ifeq ($(filter release profile,$(VARIANT)),)
	ifneq ($(SKIP_TESTS), 1)
		TARGETS_TO_BUILD += $(BUILD_DIR)/$(TEST_TARGET_NAME)
	endif
endif

$(info [INFO] Compiler: $(COMPILER_ID))
$(info [INFO] Variant:  $(VARIANT))

.PHONY: all build-variant run test clean clean-debug bear analyze cppcheck format lint memcheck
.DEFAULT_GOAL := debug

# ALL
all: debug asan tsan profile release

debug:   ; $(Q)$(MAKE) VARIANT=debug build-variant
profile: ; $(Q)$(MAKE) VARIANT=profile build-variant
release: ; $(Q)$(MAKE) VARIANT=release build-variant
asan:    ; $(Q)$(MAKE) VARIANT=asan build-variant
tsan:    ; $(Q)$(MAKE) VARIANT=tsan build-variant

# Shorthands for running variants directly
run-asan:  ; $(MAKE) VARIANT=asan run
run-tsan:  ; $(MAKE) VARIANT=tsan run

build-variant: $(TARGETS_TO_BUILD)

$(BUILD_DIR)/$(TARGET_NAME): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(ECHO_V) "Linking $(VARIANT) -> $(TARGET_NAME)..."
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/$(TEST_TARGET_NAME): $(LIB_OBJECTS) $(TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(ECHO_V) "Linking $(VARIANT) -> $(TEST_TARGET_NAME)..."
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -lcriterion -o $@

$(OBJ_DIR)/$(TEST_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(ECHO_V) "Compiling Test [no-warnings] $<..."
	$(Q)$(CC) $(TEST_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/$(VENDOR_DIR)/%.o: $(VENDOR_DIR)/%.c
	@mkdir -p $(dir $@)
	$(ECHO_V) "Compiling Vendor [optimized] $<..."
	$(Q)$(CC) $(VENDOR_CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(ECHO_V) "Compiling [$(VARIANT)] $<..."
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

.PHONY: run test run-asan test-asan run-tsan test-tsan


run: build-variant
	@echo "--- Running $(TARGET_NAME) [$(VARIANT)] ---"
	@./$(BUILD_DIR)/$(TARGET_NAME)

test: build-variant
	@echo "--- Testing [$(VARIANT)] ---"
	./$(BUILD_DIR)/$(TEST_TARGET_NAME) -j$(shell nproc)

# Clean
clean:
	@echo "Cleaning up all build variants..."
	@rm -rf $(BUILD_ROOT)
	@rm -f etc/asan.log.*

clean-debug:
	@echo "Cleaning up debug build..."
	@rm -rf $(BUILD_ROOT)/debug

bear:
	@mkdir -p $(BUILD_ROOT)/debug
	@echo "Generating compile_commands.json..."
	$(Q)bear -- $(MAKE) -B CC=clang VARIANT=debug USE_CCACHE=0

analyze: clean-debug
	scan-build --use-cc=clang --force-analyze-debug-code -analyze-headers --exclude ./$(TEST_DIR) --exclude ./$(VENDOR_DIR) -o $(BUILD_DIR)/scan-reports $(MAKE) VARIANT=debug USE_CCACHE=0

cppcheck:
	@cppcheck -q --enable=all --disable=style,unusedFunction --check-level=exhaustive --language=c --inconclusive --std=c23 \
	--suppress=missingIncludeSystem \
	--template='{file}:{line}:{column}: {severity}: {message} [{id}]' \
	-I $(VENDOR_DIR) -i $(VENDOR_DIR) $(SRC_DIR)

format:
	@find $(SRC_DIR) $(TEST_DIR) -name '*.[ch]' -o -name '*.inl' 2>/dev/null | xargs -P $(shell nproc) clang-format -i --verbose

lint:
	@run-clang-tidy -quiet -hide-progress -config-file .clang-tidy $(SRC_DIR)

memcheck: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(BUILD_ROOT)/profile/$(TARGET_NAME)

print-%: ; @echo $*=$($*)

# Include Dependencies
-include $(DEPS)
