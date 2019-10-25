add_executable(swift-playground WIN32
  swift-playground.cpp)
set_target_properties(swift-playground PROPERTIES
  ENABLE_EXPORTS true)
target_link_libraries(swift-playground PRIVATE
  REPL
  Kernel32
  User32
  Gdi32)
