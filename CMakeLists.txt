cmake_minimum_required(VERSION 3.15)
project(FakeNames VERSION 1.0.0 LANGUAGES CXX)

add_subdirectory(geode)

geode_mod(FakeNames
    ID vanishcoder.fakenames
    VERSION 1.0.0
    SOURCES
        src/main.cpp
)
