CC = clang
CFLAGS = -Wall -Wextra -Werror -std=c11 -pthread -g
OPTFLAGS = -O2
DEBUGFLAGS = -O0 -DDEBUG -fsanitize=address -fsanitize=undefined

SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

SOURCES = $(SRC_DIR)/channels.c
HEADERS = $(SRC_DIR)/channels.h
TEST_SOURCES = $(TEST_DIR)/tests.c

OBJECTS = $(BUILD_DIR)/channels.o
TEST_OBJECTS = $(BUILD_DIR)/tests.o

TEST_BIN = $(BIN_DIR)/test_channel
BENCHMARK_BIN = $(BIN_DIR)/benchmark

.DEFAULT_GOAL := all

# Create directories if they don't exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Compile source files
$(BUILD_DIR)/channels.o: $(SRC_DIR)/channels.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPTFLAGS) -c $< -o $@

# Compile test files
$(BUILD_DIR)/tests.o: $(TEST_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OPTFLAGS) -I$(SRC_DIR) -c $< -o $@

# Build test executable
$(TEST_BIN): $(OBJECTS) $(TEST_OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OPTFLAGS) $^ -o $@

# Build benchmark (if you have benchmarks/benchmark.c)
$(BENCHMARK_BIN): $(OBJECTS) benchmarks/benchmark.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OPTFLAGS) -I$(SRC_DIR) benchmarks/benchmark.c $(OBJECTS) -o $@

# Phony targets
.PHONY: all clean test benchmark debug run valgrind help

# Default: build tests
all: $(TEST_BIN)

# Build and run tests
test: $(TEST_BIN)
	@echo "Running tests..."
	@./$(TEST_BIN)

# Build benchmark
benchmark: $(BENCHMARK_BIN)
	@echo "Running benchmark..."
	@./$(BENCHMARK_BIN)

# Build with debug flags and sanitizers
debug: CFLAGS += $(DEBUGFLAGS)
debug: OPTFLAGS = -O0
debug: clean $(TEST_BIN)
	@echo "Built with debug flags and sanitizers"

# Run tests under valgrind
valgrind: $(TEST_BIN)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_BIN)

# Run with helgrind (thread race detection)
helgrind: $(TEST_BIN)
	valgrind --tool=helgrind ./$(TEST_BIN)

# Run with thread sanitizer
tsan: CFLAGS += -fsanitize=thread -g
tsan: clean $(TEST_BIN)
	@echo "Running with ThreadSanitizer..."
	@./$(TEST_BIN)

# Quick test run
run: test

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Show build information
info:
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS) $(OPTFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Headers: $(HEADERS)"
	@echo "Test sources: $(TEST_SOURCES)"

# Help message
help:
	@echo "Available targets:"
	@echo "  all        - Build test executable (default)"
	@echo "  test       - Build and run tests"
	@echo "  benchmark  - Build and run benchmarks"
	@echo "  debug      - Build with debug flags and sanitizers"
	@echo "  valgrind   - Run tests under valgrind (memory check)"
	@echo "  helgrind   - Run tests under helgrind (race detection)"
	@echo "  tsan       - Build and run with ThreadSanitizer"
	@echo "  clean      - Remove build artifacts"
	@echo "  info       - Show build configuration"
	@echo "  help       - Show this help message"
