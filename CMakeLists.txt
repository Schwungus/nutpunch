cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

project(NutPunch LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(BUILD_SHARED_LIBS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

option(NUTPUNCH_STAGING
    "Staging-facing build? Uses a different port from the regular build."
    OFF)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(NutPunch INTERFACE)
target_include_directories(NutPunch INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(NutPunch INTERFACE _WINSOCK_DEPRECATED_NO_WARNINGS)
if(WIN32)
    target_link_options(NutPunch INTERFACE -static)
    target_link_libraries(NutPunch INTERFACE ws2_32)
endif()
if(NUTPUNCH_STAGING)
    target_compile_definitions(NutPunch INTERFACE NUTPUNCH_SERVER_PORT=29999)
endif()

option(NUTPUNCH_BUILD_TESTS "Build NutPunch test binary?" ON)
if(NUTPUNCH_BUILD_TESTS)
    set(BUILD_TOYS OFF CACHE INTERNAL "Fuck poormans")
    FetchContent_Declare(
        poormans
        GIT_REPOSITORY https://github.com/nonk123/poormans.git
        GIT_TAG 65217ce89bfd4bc272954a858fc496007a457bf4)
    FetchContent_MakeAvailable(poormans)

    add_executable(NutPunchTest ${SRC_DIR}/Test.c)
    target_link_libraries(NutPunchTest PRIVATE NutPunch poormans)
endif()

option(NUTPUNCH_BUILD_SERVER "Build NutPuncher?" ON)
if(NUTPUNCH_BUILD_SERVER)
    add_executable(NutPuncher ${SRC_DIR}/NutPuncher.cpp)
    set_target_properties(NutPuncher PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(NutPuncher PRIVATE NutPunch)
    target_link_options(NutPuncher PRIVATE -static)
endif()

option(NUTPUNCH_BUILD_LOBBYIST "Build lobbyist?" ON)
if(NUTPUNCH_BUILD_LOBBYIST)
    add_executable(NutPunchLobbyist ${SRC_DIR}/Lobbyist.c)
    target_link_libraries(NutPunchLobbyist PRIVATE NutPunch)
endif()
