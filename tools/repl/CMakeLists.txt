add_executable(swift-repl
  swift-repl.cpp)
target_link_libraries(swift-repl PRIVATE
  REPL)
