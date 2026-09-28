cmake_minimum_required(VERSION 3.15)
project(FakeNames VERSION 1.0.0 LANGUAGES CXX)

# This is a placeholder - the actual Geode SDK setup will be handled by geode CLI
# For now, we'll create a minimal CMake setup that can be used with the Geode CLI

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(FakeNames SHARED
    src/main.cpp
)

target_include_directories(FakeNames PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
