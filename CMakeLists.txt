cmake_minimum_required(VERSION 3.28 FATAL_ERROR)

project(NutPunch LANGUAGES C CXX)

set(CMAKE_COMPILE_WARNING_AS_ERROR OFF) # FIXME: this enet fork is a bitch
set(BUILD_SHARED_LIBS OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(NutPunch INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/NutPunch.h)
target_include_directories(NutPunch INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    target_link_libraries(NutPunch INTERFACE
        ws2_32 winpthread winmm) # pthreads required for the `timespec_get` polyfill
endif()

option(NUTPUNCH_BUNDLE_ENET "Download NutPunch's own copy of enet?" ON)

if(NUTPUNCH_BUNDLE_ENET)
    FetchContent_Declare(enet
        URL https://raw.githubusercontent.com/zpl-c/enet/refs/tags/v2.6.5/include/enet.h
        DOWNLOAD_NO_EXTRACT ON)
    FetchContent_MakeAvailable(enet)

    target_include_directories(NutPunch INTERFACE ${enet_SOURCE_DIR})
    target_compile_definitions(NutPunch INTERFACE NUTPUNCH_BUNDLED_ENET=1)
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
