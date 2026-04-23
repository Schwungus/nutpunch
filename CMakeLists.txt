cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(NutPunch LANGUAGES C CXX)

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(enet
    GIT_REPOSITORY https://github.com/lsalzman/enet.git
    GIT_TAG 8be2368a8001f28db44e81d5939de5e613025023)
FetchContent_MakeAvailable(enet)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(NutPunch
    ${CMAKE_CURRENT_SOURCE_DIR}/include/NutPunch.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/NutPunch.c)
target_include_directories(NutPunch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${enet_SOURCE_DIR}/include) # HACK: enet's CMakeLists.txt is borked
target_link_libraries(NutPunch PUBLIC enet)

if(WIN32)
    # pthreads required for the `timespec_get` polyfill
    target_link_libraries(NutPunch PUBLIC winpthread)
endif()

option(NUTPUNCH_BUILD_TEST "Build NutPunch test binary?")
if(NUTPUNCH_BUILD_TEST)
    FetchContent_Declare(poormans
        GIT_REPOSITORY https://github.com/nonk123/poormans.git
        GIT_TAG fb0e069d4357a59848bec6c217999beb5ad8435e)
    FetchContent_MakeAvailable(poormans)

    add_executable(NutPunchTest ${SRC_DIR}/Test.c)
    target_link_libraries(NutPunchTest PRIVATE NutPunch poormans)
endif()

option(NUTPUNCH_BUILD_SERVER "Build NutPuncher?")
if(NUTPUNCH_BUILD_SERVER)
    add_executable(NutPuncher ${SRC_DIR}/NutPuncher.cpp)
    set_target_properties(NutPuncher PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_link_libraries(NutPuncher PRIVATE NutPunch)
    target_link_options(NutPuncher PRIVATE -static)
endif()

option(NUTPUNCH_BUILD_LOBBYIST "Build lobbyist?")
if(NUTPUNCH_BUILD_LOBBYIST)
    add_executable(NutPunchLobbyist ${SRC_DIR}/Lobbyist.c)
    target_link_libraries(NutPunchLobbyist PRIVATE NutPunch)
endif()

option(NUTPUNCH_BUILD_ZALOOPER "Build zalooper test binary?")
if(NUTPUNCH_BUILD_ZALOOPER)
    add_executable(NutPunchZalooper ${SRC_DIR}/Zalooper.c)
    target_link_libraries(NutPunchZalooper PRIVATE NutPunch)
endif()
