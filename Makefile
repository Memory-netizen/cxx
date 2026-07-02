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
INC_DIR := $(TOP_DIR)/include
BUILD_DIR ?= $(TOP_DIR)/build

# Source files
SRCS := $(shell find $(SRC_DIR) -name "*.c")
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.c.o, $(SRCS))

# Header directories:
INC_DIRS := $(shell find $(INC_DIR) -type d)
INC_FLAGS := $(addprefix -I, $(INC_DIRS))
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

test: $(BUILD_DIR)/$(TARGET_EXEC)
	$(SHELL) ./test.sh

.PHONY: clean
clean:
	-rm -rf $(BUILD_DIR)

-include $(DEPS)
