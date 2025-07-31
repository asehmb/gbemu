# Compiler and flags
CC = clang
# Use -g -O0 for debugging, -O2 for release builds
# BASE_CFLAGS = -O2 -march=native -flto -DNDEBUG -Wall -pthread -I$(SRC_DIR)
BASE_CFLAGS = -g -O0 -march=native -Wall -pthread -I$(SRC_DIR)

# Directories
SRC_DIR = src
BUILD_DIR = build

# Shared source files (always compiled the same way)
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC_FILES))

# SM83-specific object files with ALLOW_ROM_WRITES
SM83_OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%_sm83.o, $(SRC_FILES))

# Target configurations
# SDL target
SDL_DIR = sdl
SDL_TARGET = $(SDL_DIR)/gbemu
SDL_CFLAGS = $(BASE_CFLAGS) -I/opt/homebrew/include/SDL2
SDL_LDFLAGS = -L/opt/homebrew/lib -lSDL2 -lSDL2main -lprofiler
SDL_MAIN_OBJ = $(BUILD_DIR)/sdl_main.o

# Terminal/CLI target (example for future)
CLI_DIR = cli
CLI_TARGET = $(CLI_DIR)/gbemu
CLI_CFLAGS = $(BASE_CFLAGS)
CLI_LDFLAGS = -lprofiler
CLI_MAIN_OBJ = $(BUILD_DIR)/cli_main.o

SM83_DIR = sm83_tester
SM83_TARGET = $(SM83_DIR)/gbemu
SM83_CFLAGS = $(BASE_CFLAGS) -I/opt/homebrew/include/cjson
SM83_LDFLAGS = -lprofiler -L/opt/homebrew/lib -lcjson
SM83_MAIN_OBJ = $(BUILD_DIR)/sm83_tester.o

.PHONY: all clean sdl cli sm83 available-targets help

# Check what targets are available
AVAILABLE_TARGETS = sdl sm83
ifneq ($(wildcard $(CLI_DIR)/main.c),)
AVAILABLE_TARGETS += cli
endif
# Default target builds all available targets
all: $(AVAILABLE_TARGETS)

# Show available targets
help:
	@echo "Available targets:"
	@echo "  all     - Build all available targets: $(AVAILABLE_TARGETS)"
	@echo "  sdl     - Build SDL version"
	@echo "  cli     - Build CLI version (if cli/main.c exists)"
	@echo "  sm83    - Build SM83 Tester"
	@echo "  clean   - Clean build artifacts"
	@echo "  help    - Show this help message"

# SDL target
sdl: $(SDL_TARGET)

# CLI target (only if cli/main.c exists)
cli: $(CLI_TARGET)

sm83: $(SM83_TARGET)

# SDL binary
$(SDL_TARGET): $(OBJ_FILES) $(SDL_MAIN_OBJ)
	@mkdir -p $(SDL_DIR)
	$(CC) $(SDL_CFLAGS) $^ -o $@ $(SDL_LDFLAGS)

# CLI binary (if cli/main.c exists)
$(CLI_TARGET): $(OBJ_FILES) $(CLI_MAIN_OBJ)
	@mkdir -p $(CLI_DIR)
	$(CC) $(CLI_CFLAGS) $^ -o $@ $(CLI_LDFLAGS)

# CLI binary (if cli/main.c exists)
$(SM83_TARGET): $(SM83_OBJ_FILES) $(SM83_MAIN_OBJ)
	@mkdir -p $(SM83_DIR)
	$(CC) $(SM83_CFLAGS) $^ -o $@ $(SM83_LDFLAGS)


# Compile shared src/*.c files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BASE_CFLAGS) -c $< -o $@

# Compile SM83-specific src/*.c files with ALLOW_ROM_WRITES
$(BUILD_DIR)/%_sm83.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(BASE_CFLAGS) -DALLOW_ROM_WRITES -c $< -o $@

# Compile SDL main.c
$(SDL_MAIN_OBJ): $(SDL_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(SDL_CFLAGS) -c $< -o $@

# Compile CLI main.c (if it exists)
$(CLI_MAIN_OBJ): $(CLI_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CLI_CFLAGS) -c $< -o $@

# Compile CLI main.c (if it exists)
$(SM83_MAIN_OBJ): $(SM83_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(SM83_CFLAGS) -c $< -o $@


# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SDL_TARGET) $(CLI_TARGET) $(SM83_TARGET)
