cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

project(nutpunch LANGUAGES C)
include(FetchContent)

add_library(nutpunch INTERFACE)
target_include_directories(nutpunch INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(nutpunch INTERFACE Ws2_32)
endif()

option(NUTPUNCH_BUILD_TESTS "Build nutpunch test binary?" ON)
if(NUTPUNCH_BUILD_TESTS)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
        GIT_PROGRESS true)
    FetchContent_MakeAvailable(raylib)

    add_executable(nutpunchTest ${CMAKE_CURRENT_SOURCE_DIR}/src/nutpunchTest.c)
    target_link_libraries(nutpunchTest PRIVATE nutpunch raylib)
endif()

option(NUTPUNCH_BUILD_SERVER "Build nutpunch server binary?" ON)
if(NUTPUNCH_BUILD_SERVER)
    add_executable(nutpunchServer ${CMAKE_CURRENT_SOURCE_DIR}/src/nutpunchServer.c)
    target_link_libraries(nutpunchServer PRIVATE nutpunch)
endif()
