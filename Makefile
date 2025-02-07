# Compiler
CXX := g++

# Compiler flags
CXXFLAGS := -O3 -Werror -fPIC $(shell root-config --cflags)

# Linker flags
LDFLAGS := $(shell root-config --ldflags) $(shell root-config --glibs)

# Source files
SRCS := $(wildcard *.cxx)

# Object files
OBJS := $(SRCS:.cxx=.o)

# Executable name
TARGET := h2g_decode

# Shared library name
SHARED_LIB := libh2g_decode.so

# Default target
all: $(TARGET)

# Rule to build the executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

# Rule to build the shared library
$(SHARED_LIB): $(OBJS)
	$(CXX) -shared $(CXXFLAGS) $(LDFLAGS) $^ -o $@

# Rule to build object files
%.o: %.cxx
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule
clean:
	rm -f $(OBJS) $(TARGET) $(SHARED_LIB)