cmake_minimum_required(VERSION 3.20)
project(Shazam LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
qt_standard_project_setup()

qt_add_executable(Shazam
    MANUAL_FINALIZATION
    main.cpp
)

target_link_libraries(Shazam PRIVATE Qt6::Widgets Qt6::Concurrent)

if (MINGW)
  add_compile_options(-finput-charset=UTF-8)
endif()

add_compile_options(
  "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
  "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

add_custom_command(TARGET Shazam POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${CMAKE_SOURCE_DIR}/shazam_runner.py"
          "$<TARGET_FILE_DIR:Shazam>/shazam_runner.py"
)

qt_finalize_executable(Shazam)
