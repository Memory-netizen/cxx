# Settings
TARGET := cxx
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
SRC_DIR := ./src
INC_DIR := ./include
TEST_DIR := ./test
BUILD_DIR := ./build

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TESTS := $(TEST_SRCS:.c=.out)

# Header directories:
INC_DIRS := $(INC_DIR) $(SRC_DIR)
INC_FLAGS := $(addprefix -I, $(INC_DIRS))

HDRS := $(foreach d,$(INC_DIRS),$(wildcard $(d)/*.h))
ALL_SRCS := $(SRCS) $(HDRS)
DEPS := $(OBJS:.o=.d)
CPPFLAGS := $(INC_FLAGS) -MMD -MP

# Main target
$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

# C source compilation
define c_recipe
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endef

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c ; $(c_recipe)

.PHONY: test clean linecnt fmt

$(TEST_DIR)/%.out: $(TARGET) $(TEST_DIR)/%.c
	$(CC) -o- -E -P -C $(TEST_DIR)/$*.c | ./$(TARGET) -o $(TEST_DIR)/$*.ll -
	$(CC) -o $@ $(TEST_DIR)/$*.ll -xc $(TEST_DIR)/common -Wno-override-module

test: $(TARGET) $(TESTS)
	@for i in $(TESTS); do echo "Running $$i"; $$i || exit 1; echo; done
	@bash $(TEST_DIR)/driver.sh

clean:
	-rm -rf $(TARGET) $(BUILD_DIR) $(TEST_DIR)/*.out $(TEST_DIR)/*.ll

linecnt:
	@echo "Total lines of code (excluding blank lines and // comments):"
	@cat $(ALL_SRCS) | sed 's|//.*$$||' | grep -v '^[[:space:]]*$$' | wc -l

fmt:
	@which clang-format > /dev/null || { echo "clang-format not found"; exit 1; }
	clang-format -i $(ALL_SRCS) $(TEST_SRCS)
	@echo "Formatted $(words $(ALL_SRCS)) files."

-include $(DEPS)
