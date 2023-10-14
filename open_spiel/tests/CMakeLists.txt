add_library (tests OBJECT
  basic_tests.h
  basic_tests.cc
  console_play_test.h
  console_play_test.cc
)
target_include_directories (tests PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

