# Project Configuration
TARGET_NAME = speedtest
BIN_DIR = bin
# Profiles: dev, release, fast, tiny, minimal
PROFILE ?= dev
BUILD_DIR = build/$(PROFILE)
# Hardening (Opt-in)
HARDENED ?= 0

# --- OS / Platform Detection ---
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    EXE_SUFFIX := .exe
else
    UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := darwin
    else
        PLATFORM := linux
    endif
    EXE_SUFFIX :=
endif

# --- Compiler Options ---
CC = gcc

# macOS-specific compiler flag
ifeq ($(PLATFORM),darwin)
    DARWIN_FLAGS = -DDARWIN
else
    DARWIN_FLAGS =
endif

# POSIX source macros
ifeq ($(PLATFORM),windows)
    POSIX_FLAGS =
else
    POSIX_FLAGS = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
endif

# Base CFLAGS
CFLAGS = -std=c99 $(POSIX_FLAGS) -pedantic -Wall -Wextra -Isrc -Isrc/include $(DARWIN_FLAGS)
LDFLAGS = -lcurl -lcjson -pthread

# Hardening flags (Linux only)
ifeq ($(PLATFORM),linux)
    HARDENING_C = -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
    HARDENING_L = -Wl,-z,relro -Wl,-z,now -pie
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
else ifeq ($(PROFILE),fast)
    OPTFLAGS = -O3 -march=native -flto
else ifeq ($(PROFILE),tiny)
    OPTFLAGS = -Os -flto
else
    # dev (default)
    OPTFLAGS = -O0 -g
endif

CFLAGS += $(OPTFLAGS)

# --- PGO (Profile Guided Optimization) ---
PGO_GEN ?= 0
ifeq ($(PGO_GEN),1)
    CFLAGS += -fprofile-generate
    LDFLAGS += -fprofile-generate
endif

PGO_USE ?= 0
ifeq ($(PGO_USE),1)
    CFLAGS += -fprofile-use
    LDFLAGS += -fprofile-use
endif

# --- Files and Targets ---
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/$(TARGET_NAME)-$(PROFILE)$(EXE_SUFFIX)

.PHONY: all clean directories format lint pgo-gen pgo-use

all: directories $(TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build bin

format:
	@echo "Formatting C source files"
	clang-format -style=file -i src/*.c src/include/*.h src/*.h

lint:
	@echo "Running clang-tidy analysis"
	clang-tidy -checks=-*,bugprone-*,clang-analyzer-*,performance-* $(SRCS) -- $(CFLAGS)

# PGO targets
pgo-gen:
	@$(MAKE) PGO_GEN=1 clean all

pgo-use:
	@$(MAKE) PGO_USE=1 clean all
