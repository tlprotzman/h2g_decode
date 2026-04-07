cmake_minimum_required(VERSION 3.10)

# Project name
project(H2G_Decode VERSION 0.13)

#Path to cmake scripts
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

#-----For setting rpath correctly on MAC OS X
# use, i.e. don't skip the full RPATH for the build tree
set(CMAKE_SKIP_BUILD_RPATH FALSE)
# when building, don't use the install RPATH already
# (but later on when installing)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
# the RPATH to be used when installing, but only if it's not a system directory
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif("${isSystemDir}" STREQUAL "-1")
#-----

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

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -fPIC")
# set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -fPIC") # slow running, easier debugging
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O3 -fPIC") # faster running

option(USE_ROOT "Enable TTree writer" ON)
if (USE_ROOT)
# Find ROOT package
find_package(ROOT)
endif()

if(ROOT_FOUND)
    # Compiler flags
    # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Werror -fPIC")
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
target_link_libraries(h2g_run PUBLIC ${ROOT_LIBRARIES})
target_include_directories(h2g_run INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<INSTALL_INTERFACE:inc> )
install( TARGETS h2g_run EXPORT H2G_Decode DESTINATION ${CMAKE_INSTALL_PREFIX}/lib )

target_link_libraries(h2g_decode PUBLIC ${ROOT_LIBRARIES})
target_include_directories(h2g_decode INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src> $<INSTALL_INTERFACE:inc> )
install( TARGETS h2g_decode EXPORT H2G_Decode DESTINATION ${CMAKE_INSTALL_PREFIX}/lib )

install( DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_PREFIX}/inc FILES_MATCHING PATTERN "*.h" )

install( EXPORT H2G_Decode FILE H2G_DecodeTargets.cmake NAMESPACE H2G_Decode:: DESTINATION ${CMAKE_INSTALL_PREFIX} )
set( H2G_Decode_INSTALL_INCLUDE ${CMAKE_INSTALL_PREFIX}/inc )

include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/H2G_DecodeConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/H2G_DecodeConfig.cmake
  INSTALL_DESTINATION "${CMAKE_INSTALL_PREFIX}"
  PATH_VARS H2G_Decode_INSTALL_INCLUDE
  )

#This will create the config version file needed by 'find_package'
write_basic_package_version_file(
    H2G_DecodeConfigVersion.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install( FILES "${CMAKE_CURRENT_BINARY_DIR}/H2G_DecodeConfig.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/H2G_DecodeConfigVersion.cmake"
         DESTINATION ${CMAKE_INSTALL_PREFIX}
)

