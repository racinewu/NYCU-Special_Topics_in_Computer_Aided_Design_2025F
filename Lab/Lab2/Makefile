# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude -MMD -MP

# Source and object files
SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst src/%.cpp, build/%.o, $(SRC))
DEP = $(OBJ:.o=.d)

# Target
TARGET = sta

# Default target
all: $(TARGET)

# Link target
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Compile source files with dependency generation
build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Include dependency files
-include $(DEP)

# Clean all generated files
clean:
	rm -rf build $(TARGET) *.txt

.PHONY: all clean
