cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

project(nutpunch LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(BUILD_SHARED_LIBS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

add_library(nutpunch INTERFACE)
target_include_directories(nutpunch INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(nutpunch INTERFACE _WINSOCK_DEPRECATED_NO_WARNINGS)
if(WIN32)
    target_link_options(nutpunch INTERFACE -static)
    target_link_libraries(nutpunch INTERFACE ws2_32)
endif()

option(NUTPUNCH_BUILD_TESTS "Build nutpunch test binary?" ON)
if(NUTPUNCH_BUILD_TESTS)
    FetchContent_Declare(
        poormans
        GIT_REPOSITORY https://github.com/nonk123/poormans.git
        GIT_TAG 732eb2d7a34524f4cb6ccaba593a7ae09dd6762e)
    FetchContent_MakeAvailable(poormans)

    add_executable(nutpunchTest ${CMAKE_CURRENT_SOURCE_DIR}/src/test.c ${CMAKE_CURRENT_SOURCE_DIR}/src/static.c)
    target_link_libraries(nutpunchTest PRIVATE nutpunch poormans)
endif()

option(NUTPUNCH_BUILD_SERVER "Build nutpuncher?" ON)
if(NUTPUNCH_BUILD_SERVER)
    add_executable(nutpuncher ${CMAKE_CURRENT_SOURCE_DIR}/src/nutpuncher.cpp)
    set_target_properties(nutpuncher PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(nutpuncher PRIVATE nutpunch)
    target_link_options(nutpuncher PRIVATE -static)
endif()

option(NUTPUNCH_BUILD_LOBBYIST "Build lobbyist?" ON)
if(NUTPUNCH_BUILD_LOBBYIST)
    add_executable(nutpunchLobbyist ${CMAKE_CURRENT_SOURCE_DIR}/src/lobbyist.c ${CMAKE_CURRENT_SOURCE_DIR}/src/static.c)
    target_link_libraries(nutpunchLobbyist PRIVATE nutpunch)
endif()
