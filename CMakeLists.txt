cmake_minimum_required(VERSION 3.10)
project(FileCopyTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_library(FileCopy STATIC FileCopy.cpp FileCopy.h)
target_include_directories(FileCopy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(FileCopy PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(FileCopy PRIVATE Threads::Threads)

# test
add_executable(test test.cpp)
target_link_libraries(test PRIVATE FileCopy Threads::Threads)

if(MSVC)
    target_compile_options(FileCopy PRIVATE /W4 /permissive-)
    target_compile_options(test PRIVATE /W4 /permissive-)
endif()

set_target_properties(FileCopy PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)