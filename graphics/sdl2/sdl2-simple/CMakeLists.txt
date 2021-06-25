cmake_minimum_required(VERSION 3.0.0)

project(sdl2-simple)

include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)

file(GLOB SOURCES source/*.c*)

add_executable(${CMAKE_PROJECT_NAME} ${SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(${CMAKE_PROJECT_NAME} ${SDL2_LIBRARIES})

if (NINTENDO_SWITCH)
    nx_create_nro(${CMAKE_PROJECT_NAME})
endif ()