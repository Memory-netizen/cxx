# Settings
TARGET_EXEC := cxx
CFLAGS := -Wall -Wextra -Werror -std=c23 -fno-common
LDFLAGS :=

# Debug flags
DEBUG ?= 1
ifeq ($(DEBUG), 0)
CFLAGS += -O2 -DNDEBUG
else
CFLAGS += -g -O0
endif

# Compiler
CC := clang

# Directories
TOP_DIR := $(shell pwd)
SRC_DIR := $(TOP_DIR)/src
INC_DIR := $(TOP_DIR)/include $(TOP_DIR)/src
BUILD_DIR ?= $(TOP_DIR)/build

# Source files
SRCS := $(shell find $(SRC_DIR) -name "*.c")
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.c.o, $(SRCS))

# Header directories:
INC_DIRS := $(shell find $(INC_DIR) -type d)
INC_FLAGS := $(addprefix -I, $(INC_DIRS))

HDRS := $(shell find $(INC_DIRS) -name "*.h")
ALL_SRCS := $(SRCS) $(HDRS)
DEPS := $(OBJS:.o=.d)
CPPFLAGS := $(INC_FLAGS) -MMD -MP

all: $(BUILD_DIR)/$(TARGET_EXEC)

# Main target
$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

# C source compilation
define c_recipe
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endef
$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c ; $(c_recipe)


.PHONY: test clean linecnt fmt

test: $(BUILD_DIR)/$(TARGET_EXEC)
	$(SHELL) ./test.sh

clean:
	-rm -rf $(BUILD_DIR)

linecnt:
	@echo "Total lines of code (excluding blank lines and // comments):"
	@cat $(ALL_SRCS) | sed 's|//.*$$||' | grep -v '^[[:space:]]*$$' | wc -l

fmt:
	@which clang-format > /dev/null || { echo "clang-format not found"; exit 1; }
	clang-format -i $(ALL_SRCS)
	@echo "Formatted $(words $(ALL_SRCS)) files."

-include $(DEPS)
