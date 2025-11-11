cmake_minimum_required(VERSION 3.15)
project(llvm-obfuscator)

set(OBF_LLVM_INSTALL_DIR "" CACHE PATH "llvm installation path")
set(OBF_LLVM_INCLUDE_DIR "${OBF_LLVM_INSTALL_DIR}/include/llvm")

if(NOT EXISTS "${OBF_LLVM_INCLUDE_DIR}")
    message(FATAL_ERROR
  " OBF_LLVM_INSTALL_DIR (${LT_LLVM_INCLUDE_DIR}) is invalid.")
endif()

list(APPEND CMAKE_PREFIX_PATH "${OBF_LLVM_INSTALL_DIR}/lib/cmake/llvm/")

find_package(LLVM CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LT_LLVM_INSTALL_DIR}")

message("LLVM STATUS:
  Definitions ${LLVM_DEFINITIONS}
  Includes    ${LLVM_INCLUDE_DIRS}
  Libraries   ${LLVM_LIBRARY_DIRS}
  Targets     ${LLVM_TARGETS_TO_BUILD}"
)

if("${LLVM_VERSION_MAJOR}" VERSION_LESS 15)
  message(FATAL_ERROR "Found LLVM ${LLVM_VERSION_MAJOR}, but need LLVM 15 or above")
endif()

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_subdirectory(libs)