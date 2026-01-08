# CMakeLists.txt for typemeta_extract Clang tool
#
# Build Instructions:
#   mkdir -p build_utils && cd build_utils
#   cmake ../utils -DCMAKE_BUILD_TYPE=Release
#   make
#
# Prerequisites (macOS):
#   brew install llvm
#   export LLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm
#   export Clang_DIR=$(brew --prefix llvm)/lib/cmake/clang
#
# Prerequisites (Linux):
#   apt install llvm-dev libclang-dev clang
#   or install from llvm.org releases

cmake_minimum_required(VERSION 3.16)
project(typemeta_extract CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find LLVM and Clang
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

# Include directories
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})

# Add definitions
add_definitions(${LLVM_DEFINITIONS})

# Get LLVM libraries for basic usage
llvm_map_components_to_libnames(LLVM_LIBS
    support
    core
    option
)

# Create executable
add_executable(typemeta_extract
    typemeta_extract.cpp
)

# Link Clang libraries
target_link_libraries(typemeta_extract
    clangTooling
    clangFrontend
    clangSerialization
    clangDriver
    clangParse
    clangSema
    clangAnalysis
    clangAST
    clangASTMatchers
    clangBasic
    clangEdit
    clangLex
    clangRewrite
    ${LLVM_LIBS}
)

# Set RPATH for installed binary
set_target_properties(typemeta_extract PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# Install
install(TARGETS typemeta_extract RUNTIME DESTINATION bin)
