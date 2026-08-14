# Settings
TARGET := cxx
CFLAGS := -Wall -Wextra -Werror -std=c23 -fno-common
CFLAGS += -D_GNU_SOURCE
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
BUILD_DIR := ./build

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Generated headers (from tools scripts)
GENERATED_HDRS := src/width_property.h src/xid_property.h src/pow_table.h

# All headers (existing + generated)
HDRS := $(wildcard $(SRC_DIR)/*.h)

TEST_SRCS := $(wildcard test/*.c)
TESTS := $(TEST_SRCS:.c=.out)

ALL_SRCS := $(SRCS) $(HDRS)

DEPS := $(OBJS:.o=.d)
CPPFLAGS := -MMD -MP

# Main target
$(TARGET): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/lexer.o: | src/pow_table.h
$(BUILD_DIR)/unicode.o: | src/width_property.h src/xid_property.h

# C source compilation
define c_recipe
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
endef

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c ; $(c_recipe)

# Rules to generate headers from scripts
src/width_property.h: tools/extract_width.py
	python3 $< > $@

src/xid_property.h: tools/extract_xid.py
	python3 $< > $@

src/pow_table.h: tools/gen_pow_table.py
	python3 $< > $@

.PHONY: test clean linecnt fmt

test/%.out: $(TARGET) test/%.c
	./$(TARGET) -Itest -c -o test/$*.o test/$*.c
	$(CC) -o $@ test/$*.o -xc test/common

test: $(TARGET) $(TESTS)
	@for i in $(TESTS); do echo "Running $$i"; $$i || exit 1; echo; done
	@bash test/driver.sh ./cxx

clean:
	-rm -rf $(TARGET) $(BUILD_DIR) test/*.out test/*.ll test/*.o $(GENERATED_HDRS)

linecnt:
	@echo "Total lines of code (excluding blank lines and // comments):"
	@cat $(ALL_SRCS) | sed 's|//.*$$||' | grep -v '^[[:space:]]*$$' | wc -l

fmt:
	@which clang-format-21 > /dev/null || { echo "clang-format not found"; exit 1; }
	clang-format-21 -i $(ALL_SRCS) $(TEST_SRCS)
	@echo "Formatted $(words $(ALL_SRCS)) files."

-include $(DEPS)
