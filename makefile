# Compiler and flags
CC = clang
CFLAGS = -O2 -march=native -flto -DNDEBUG -Wall -pthread -I/opt/homebrew/include/SDL2 -I$(SRC_DIR)
LDFLAGS = -L/opt/homebrew/lib -lSDL2 -lSDL2main -lprofiler

# Directories
SRC_DIR = src
BUILD_DIR = build
PLATFORM_DIR = sdl

# Source files
SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC_FILES))

# Platform main file
MAIN_OBJ = $(BUILD_DIR)/main.o

# Target binary
TARGET = $(PLATFORM_DIR)/gbemu

.PHONY: all clean

# Default target
all: $(TARGET)

# Final binary
$(TARGET): $(OBJ_FILES) $(MAIN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Compile src/*.c files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile platform main.c
$(MAIN_OBJ): $(PLATFORM_DIR)/main.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
