cmake_minimum_required(VERSION 3.15.0)

project(swift-repl LANGUAGES CXX Swift)

set(CMAKE_INCLUDE_CURRENT_DIR YES)

find_package(LLVM CONFIG REQUIRED)
find_package(Swift CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)
find_package(SwiftWin32 CONFIG REQUIRED)
find_package(Python COMPONENTS Interpreter)

set(CMAKE_Swift_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/swift)
set(SwiftREPL_TESTS_DIR ${CMAKE_SOURCE_DIR}/tests)
set(LIT ${LLVM_TOOLS_BINARY_DIR}/llvm-lit.py)

configure_file(${SwiftREPL_TESTS_DIR}/lit.cfg.py.in ${SwiftREPL_TESTS_DIR}/lit.cfg.py)

if(NOT DEFINED LIT_ARGS_DEFAULT)
  set(LIT_ARGS_DEFAULT -v -vv)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(DYLIB_EXTENSION ".dll")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(DYLIB_EXTENSION ".dylib")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  set(DYLIB_EXTENSION ".so")
endif()

add_custom_target(test COMMAND ${Python_EXECUTABLE} ${LIT} ${SwiftREPL_TESTS_DIR} ${LIT_ARGS_DEFAULT})
configure_file(Config.h.in Config.h)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} ${SWIFT_INCLUDE_DIRS})

add_library(REPL STATIC
  REPL.cpp
  JIT.cpp
  TransformAST.cpp
  TransformIR.cpp
  CommandLineOptions.cpp
  Logging.cpp
  LibraryLoading.cpp)
target_include_directories(REPL PRIVATE ${ALL_INCLUDE_DIRS})
target_link_libraries(REPL PRIVATE
  LLVMExecutionEngine
  LLVMOrcJIT
  swiftAST
  swiftBasic
  swiftFrontend
  swiftImmediate
  swiftParse
  swiftParseSIL
  swiftSema
  swiftSIL
  swiftSyntax
  swiftSyntaxParse)

add_subdirectory(tools)

add_subdirectory(PlaygroundSupport)

add_dependencies(test swift-repl)
