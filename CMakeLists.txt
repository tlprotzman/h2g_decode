cmake_minimum_required(VERSION 3.10)

# Project name
project(h2g_decode)

# Find ROOT package
find_package(ROOT REQUIRED)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Werror -fPIC")
execute_process(COMMAND root-config --cflags OUTPUT_VARIABLE ROOT_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND root-config --ldflags OUTPUT_VARIABLE ROOT_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND root-config --glibs OUTPUT_VARIABLE ROOT_GLIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ROOT_CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ROOT_LDFLAGS} ${ROOT_GLIBS}")

# Source files
file(GLOB SRCS src/*.cxx)

# Executable
add_executable(h2g_run ${SRCS})

# Shared library
add_library(h2g_decode SHARED ${SRCS})

# Include ROOT
include_directories(${ROOT_INCLUDE_DIRS})

# Linker flags
target_link_libraries(h2g_run ${ROOT_LIBRARIES})
target_link_libraries(h2g_decode ${ROOT_LIBRARIES})