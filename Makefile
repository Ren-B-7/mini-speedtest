# Project Configuration
TARGET_NAME = speedtest
BUILD_DIR = build/$(PROFILE)
BIN_DIR = bin
PROFILE ?= dev
HARDENED ?= 0

# --- OS / Platform Detection ---
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    EXE_SUFFIX := .exe
    INSTALL_DIR ?= $(USERPROFILE)/.local/bin
else
    UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := darwin
    else
        PLATFORM := linux
    endif
    EXE_SUFFIX :=
    INSTALL_DIR ?= $(HOME)/.local/bin
endif

# --- Compiler Detection ---
CC ?= gcc

IS_CLANG := $(shell $(CC) -E -dM - < /dev/null | grep -q "__clang__" && echo 1 || echo 0)
IS_GCC := $(shell $(CC) -E -dM - < /dev/null | grep -q "__GNUC__" && [ $(IS_CLANG) -eq 0 ] && echo 1 || echo 0)

# --- Compilation Flags ---
POSIX_FLAGS = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
ifeq ($(PLATFORM),darwin)
    DARWIN_FLAGS = -DDARWIN
else
    DARWIN_FLAGS =
endif

# Strict compilation flags
COMMON_WARNINGS = -Wformat=2 -Wformat-security -Wnull-dereference -Wstack-protector \
                  -Wvla -Wcast-qual -Wconversion -Wsign-conversion -Wredundant-decls \
                  -Wshadow -Wwrite-strings -Wfloat-equal -Wpointer-arith \
                  -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
                  -Winline -Wundef
CFLAGS = -std=c99 $(POSIX_FLAGS) -pedantic -Wall -Wextra -Isrc -Isrc/include $(DARWIN_FLAGS) $(COMMON_WARNINGS)

# GCC-specific warnings
ifeq ($(IS_GCC),1)
    GCC_FLAGS = -Wtrampolines -Walloca -Warray-bounds=2 -Wimplicit-fallthrough=3 \
                -Wshift-overflow=2 -Wcast-align=strict -Wlogical-op -Wduplicated-cond \
                -Wduplicated-branches -Wrestrict -Wnested-externs -Wbad-function-cast \
                -Wold-style-definition
    CFLAGS += $(GCC_FLAGS)
endif

# Clang-specific warnings
ifeq ($(IS_CLANG),1)
    CLANG_FLAGS = -Wimplicit-fallthrough -Wcast-align -Wextra-semi \
                  -Wcovered-switch-default -Wswitch-enum -Wpacked -Wpadded
    CFLAGS += $(CLANG_FLAGS)
endif

# Hardening flags (Linux only)
ifeq ($(PLATFORM),linux)
    HARDENING_C = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE -fstack-clash-protection -fcf-protection
    HARDENING_L = -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code -pie
else
    HARDENING_C =
    HARDENING_L =
endif

ifeq ($(HARDENED),1)
    CFLAGS += $(HARDENING_C)
    LDFLAGS += $(HARDENING_L)
endif

# --- Optimization Profiles ---
ifeq ($(PROFILE),release)
    OPTFLAGS = -Os -march=x86-64 -flto
    LDFLAGS += -s
    STRIP_BINARY = 1
else ifeq ($(PROFILE),fast)
    OPTFLAGS = -O3 -march=native -flto
    LDFLAGS += -s
    STRIP_BINARY = 1
else ifeq ($(PROFILE),tiny)
    OPTFLAGS = -Os -flto
    LDFLAGS += -s
    STRIP_BINARY = 1
else ifeq ($(PROFILE),minimal)
    OPTFLAGS = -Os -DMINIMAL
    LDFLAGS += -s
    STRIP_BINARY = 1
else
    # dev (default)
    OPTFLAGS = -O0 -g
    STRIP_BINARY = 0
endif

CFLAGS += $(OPTFLAGS)

# --- PGO ---
PGO_GEN ?= 0
PGO_USE ?= 0
ifeq ($(PGO_GEN),1)
    CFLAGS += -fprofile-generate
    LDFLAGS += -fprofile-generate
endif
ifeq ($(PGO_USE),1)
    CFLAGS += -fprofile-use
    LDFLAGS += -fprofile-use
endif

# --- Files and Targets ---
SRCS = $(wildcard src/*.c)
HDRS = $(wildcard src/*.h) $(wildcard src/include/*.h)
SRCS_ALL = $(SRCS) $(HDRS)
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/$(TARGET_NAME)-$(PROFILE)$(EXE_SUFFIX)
LDFLAGS += -lcurl -lcjson -pthread

# Strip tool
STRIP ?= strip

.PHONY: all clean directories format format-c format-makefile format-ci format-makefile-ci \
	format-c-ci lint lint-c lint-makefile pgo-gen pgo-use install uninstall compile-all

all: directories $(TARGET)

# Compile all profiles
compile-all:
	@echo "Compiling all profiles..."
	@$(MAKE) PROFILE=dev directories $(BIN_DIR)/$(TARGET_NAME)-dev$(EXE_SUFFIX)
	@$(MAKE) PROFILE=release directories $(BIN_DIR)/$(TARGET_NAME)-release$(EXE_SUFFIX)
	@$(MAKE) PROFILE=fast directories $(BIN_DIR)/$(TARGET_NAME)-fast$(EXE_SUFFIX)
	@$(MAKE) PROFILE=tiny directories $(BIN_DIR)/$(TARGET_NAME)-tiny$(EXE_SUFFIX)
	@$(MAKE) PROFILE=minimal directories $(BIN_DIR)/$(TARGET_NAME)-minimal$(EXE_SUFFIX)
	@echo "All profiles compiled in $(BIN_DIR)/"

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@if [ "$(STRIP_BINARY)" = "1" ]; then \
		echo "Stripping $@"; \
		$(STRIP) $@; \
	fi

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

check-tools:
	@command -v clang-format > /dev/null 2>&1 || \
	    { echo "ERROR: clang-format not found. Install LLVM and ensure it is on PATH."; exit 1; }
	@command -v clang-tidy > /dev/null 2>&1 || \
	    { echo "ERROR: clang-tidy not found. Install LLVM and ensure it is on PATH."; exit 1; }
	@command -v mbake > /dev/null 2>&1 || \
	    { echo "ERROR: mbake not found. Install mbake and ensure it is on PATH."; exit 1; }

clean:
	rm -rf build bin

format: format-c format-makefile
format-ci: format-c-ci format-makefile-ci

format-c:
	@echo "Formatting C source files"
	clang-format -style=file -i $(SRCS_ALL)

format-c-ci:
	@echo "Checking C source file formats"
	clang-format --dry-run -style=file -Werror $(SRCS_ALL)

format-makefile:
	@echo "Formatting Makefile"
	mbake format --config ./.bake.toml Makefile

format-makefile-ci:
	@echo "Checking Makefile format"
	mbake format --config ./.bake.toml --check Makefile

lint: lint-c lint-makefile

lint-c:
	@echo "Running clang-tidy analysis"
	clang-tidy -checks=-*,bugprone-*,clang-analyzer-*,performance-* \
	$(SRCS) -- $(CFLAGS)

lint-makefile:
	@echo "Running Makefile analysis"
	mbake validate --config ./.bake.toml Makefile

# PGO targets
pgo-gen:
	@$(MAKE) PGO_GEN=1 clean all

pgo-use:
	@$(MAKE) PGO_USE=1 clean all

# Installation
# Interactive install target that detects available binaries (based on Makefile-temp pattern)
install:
	@binaries=$$(ls $(BIN_DIR)/$(TARGET_NAME)-* 2>/dev/null | xargs -n1 basename 2>/dev/null); \
	if [ -z "$$binaries" ]; then \
		echo "ERROR: No binaries found in $(BIN_DIR)/. Run 'make' or 'make compile-all' first."; \
		exit 1; \
	fi; \
	echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"; \
	echo "Available Binaries to Install:"; \
	echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"; \
	i=1; \
	for b in $$binaries; do \
		echo "$$i. $$b"; \
		i=$$(($$i + 1)); \
	done; \
	echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"; \
	printf "Select a binary to install (1-$$(( $$i - 1 ))): "; \
	read choice; \
	selected=$$(echo "$$binaries" | sed -n "$${choice}p"); \
	if [ -z "$$selected" ]; then \
		echo "Invalid choice '$$choice'. Installation cancelled."; exit 1; \
	fi; \
	mkdir -p $(INSTALL_DIR); \
	install -m 755 $(BIN_DIR)/$$selected $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX); \
	echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"; \
	echo "Installed $$selected as $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)"; \
	echo "Installation complete."

uninstall:
	rm -f $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)
	@echo "Uninstalled $(TARGET_NAME) from $(INSTALL_DIR)"
