cmake_minimum_required(VERSION 3.10)

# Project name
project(h2g_decode)
# Attempt to extract -std=c++XX from root-config --cflags, default to 20 if not found
execute_process(
    COMMAND root-config --cflags
    OUTPUT_VARIABLE ROOT_CFLAGS_STD
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "-std=c\\+\\+[0-9]+" STD_FLAG "${ROOT_CFLAGS_STD}")
if(STD_FLAG)
    string(REGEX REPLACE "-std=c\\+\\+" "" CXX_STANDARD "${STD_FLAG}")
    set(CMAKE_CXX_STANDARD ${CXX_STANDARD})
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Using C++ standard: ${CMAKE_CXX_STANDARD}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")


option(USE_ROOT "Enable TTree writer" ON)
if (USE_ROOT)
# Find ROOT package
find_package(ROOT)
endif()

if(ROOT_FOUND)
    # Compiler flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Werror -fPIC")
    execute_process(COMMAND root-config --cflags OUTPUT_VARIABLE ROOT_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND root-config --ldflags OUTPUT_VARIABLE ROOT_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND root-config --glibs OUTPUT_VARIABLE ROOT_GLIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ROOT_CFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${ROOT_LDFLAGS} ${ROOT_GLIBS}")

    # Define a flag for ROOT usage
    add_definitions(-DUSE_ROOT)
else()
    message(WARNING "ROOT not found. Some features may be disabled.")
endif()

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