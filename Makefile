# Project Configuration
TARGET_NAME = speedtest
BUILD_DIR = build
BIN_DIR = bin

# --- Build Profile Selection ---
# Usage: make PROFILE=release
# Profiles: dev (default), release, fast
PROFILE ?= dev

# --- Compiler Options ---
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Isrc -Isrc/include -pthread
LDFLAGS = -lcurl -lcjson

# --- OS/Platform Support ---
ifeq ($(OS),Windows_NT)
    EXE_SUFFIX := .exe
    CFLAGS += -D_WIN32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        CFLAGS += -DDARWIN
    else
        CFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
    endif
endif

# --- Hardening (Opt-in) ---
# Usage: make HARDENED=1
ifeq ($(HARDENED),1)
    CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
    LDFLAGS += -Wl,-z,relro -Wl,-z,now -pie
endif

# --- Optimization Profiles ---
ifeq ($(PROFILE),release)
    CFLAGS += -Os -march=x86-64 -flto
    LDFLAGS += -s
else ifeq ($(PROFILE),fast)
    CFLAGS += -O3 -march=native -flto
else
    # Default: dev
    CFLAGS += -O0 -g
endif

# --- Files and Targets ---
SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)

.PHONY: all clean directories

all: directories $(TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

format:
	@echo "Formatting C source files"
	clang-format -style=file -i src/*.c src/include/*.h src/*.h

lint:
	@echo "Running clang-tidy analysis"
	clang-tidy -checks=-*,bugprone-*,clang-analyzer-*,performance-* $(SRCS) -- $(CFLAGS)
