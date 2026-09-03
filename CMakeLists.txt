cmake_minimum_required(VERSION 3.15)
project(LiteProcManager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /EHsc)
endif()

add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)

# Include directories available for subdirectories
include_directories(include)

# Subdirectories for application source and unit tests
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
