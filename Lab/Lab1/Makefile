# Compiler settings
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -fno-inline-small-functions -Wall -Iinclude -MMD -MP

# Source and object files
SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst src/%.cpp, build/%.o, $(SRC))
DEP = $(OBJ:.o=.d)

# Target
TARGET = bin/sop

# Default target
all: $(TARGET)

# Link target
$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compile source files with dependency generation
build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Include dependency files
-include $(DEP)

# Clean
clean:
	rm -rf build bin

.PHONY: all clean