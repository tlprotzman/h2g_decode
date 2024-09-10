# Compiler
CXX := g++

# Compiler flags
CXXFLAGS := -std=c++17 -O3 -g 

# Source files
SRCS := $(wildcard *.cxx)

# Object files
OBJS := $(SRCS:.cxx=.o)

# Executable name
TARGET := h2g_decode

# Default target
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# Rule to build object files
%.o: %.cxx
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(OBJS) $(TARGET)