cmake_minimum_required(VERSION 3.27...4.0 FATAL_ERROR)

project(nutpunch LANGUAGES C)

include(FetchContent)
FetchContent_Declare(
    tinycsocket
    GIT_REPOSITORY https://github.com/Schwungus/tinycsocket.git
    GIT_TAG 1a5658c342040b9b5df8177504a08b0edc4992eb
    GIT_PROGRESS true)
FetchContent_MakeAvailable(tinycsocket)

add_library(nutpunch INTERFACE)
target_include_directories(nutpunch INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nutpunch INTERFACE tinycsocket_header)

option(NUTPUNCH_BUILD_TESTS "Build nutpunch test binary?" ON)
if(NUTPUNCH_BUILD_TESTS)
    add_executable(nutpunchTest ${CMAKE_CURRENT_SOURCE_DIR}/nutpunchTest.c)
    target_link_libraries(nutpunchTest PRIVATE nutpunch)
endif()

option(NUTPUNCH_BUILD_SERVER "Build nutpunch server binary?" ON)
if(NUTPUNCH_BUILD_SERVER)
    add_executable(nutpunchServer ${CMAKE_CURRENT_SOURCE_DIR}/nutpunchServer.c)
    target_link_libraries(nutpunchServer PRIVATE nutpunch)
endif()
