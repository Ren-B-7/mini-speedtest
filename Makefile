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
IS_GCC   := $(shell $(CC) -E -dM - < /dev/null | grep -q "__GNUC__" && [ $(IS_CLANG) -eq 0 ] && echo 1 || echo 0)


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
else ifeq ($(PROFILE),fast)
    OPTFLAGS = -O3 -march=native -flto
else ifeq ($(PROFILE),tiny)
    OPTFLAGS = -Os -flto
else
    # dev (default)
    OPTFLAGS = -O0 -g
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
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
TARGET = $(BIN_DIR)/$(TARGET_NAME)-$(PROFILE)$(EXE_SUFFIX)
LDFLAGS += -lcurl -lcjson -pthread

.PHONY: all clean directories format lint pgo-gen pgo-use install uninstall

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

# Installation
install: all
	mkdir -p $(INSTALL_DIR)
	install -m 755 $(TARGET) $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)
	@echo "Installed $(TARGET) to $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)"

uninstall:
	rm -f $(INSTALL_DIR)/$(TARGET_NAME)$(EXE_SUFFIX)
	@echo "Uninstalled $(TARGET_NAME) from $(INSTALL_DIR)"
