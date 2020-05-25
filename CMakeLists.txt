cmake_minimum_required (VERSION 3.9.6)
project(zipsync CXX C)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "" FORCE)

set(sources
    minizip_extra.c
    minizip_extra.h
    minizip_private.h

    ZSAssert.h
    ZSAssert.cpp

    StdFilesystem.cpp
    StdFilesystem.h
    StdString.cpp
    StdString.h

    Ini.h
    Ini.cpp
    Hash.h
    Hash.cpp
    Path.h
    Path.cpp
    Utils.h
    Utils.cpp
    ZipUtils.h
    ZipUtils.cpp
    Manifest.h
    Manifest.cpp
    LocalCache.h
    LocalCache.cpp
    ZipSync.h
    ZipSync.cpp
    Downloader.h
    Downloader.cpp

    ZipSync.natvis
)

set(tests_sources
    Fuzzer.cpp
    Fuzzer.h
    TestsMain.cpp
    HttpServer.cpp
    HttpServer.h
)

set(playground_sources
    PlaygroundMain.cpp
)

set(zipsynccmd_sources
    Wildcards.h
    Wildcards.cpp
    CommandLine.cpp
)


if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W2")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Ob2 /FAs")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()


add_library(libzipsync ${sources})
target_link_libraries(libzipsync PUBLIC CONAN_PKG::zlib CONAN_PKG::BLAKE2 CONAN_PKG::libcurl)

add_executable(tests ${tests_sources})
target_link_libraries(tests PRIVATE libzipsync CONAN_PKG::doctest CONAN_PKG::libmicrohttpd)

add_executable(playground ${playground_sources})
target_link_libraries(playground libzipsync)

add_executable(zipsync ${zipsynccmd_sources})
target_link_libraries(zipsync libzipsync CONAN_PKG::args)
