cmake_minimum_required (VERSION 3.9.6)
project(zipsync CXX C)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "" FORCE)

set(sources
    minizip_extra.c
    minizip_extra.h
    minizip_private.h
    StdString.cpp
    StdString.h
    Logging.h
    Logging.cpp
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
    StdFilesystem.cpp
    StdFilesystem.h
    TestCreator.cpp
    TestCreator.h
    Fuzzer.cpp
    Fuzzer.h
    TestsMain.cpp
    HttpServer.cpp
    HttpServer.h
)

set(zipsynccmd_sources
    StdFilesystem.cpp
    StdFilesystem.h
    Wildcards.h
    Wildcards.cpp
    CommandLine.cpp
)


add_library(libzipsync ${sources})
target_link_libraries(libzipsync PUBLIC CONAN_PKG::zlib CONAN_PKG::BLAKE2 CONAN_PKG::libcurl)
set_property(TARGET libzipsync PROPERTY CXX_STANDARD 11)

add_executable(tests ${tests_sources})
set_property(TARGET tests PROPERTY CXX_STANDARD 17)
target_link_libraries(tests PRIVATE libzipsync CONAN_PKG::doctest CONAN_PKG::libmicrohttpd)
target_link_libraries(tests PRIVATE stdc++fs)

add_executable(zipsync ${zipsynccmd_sources})
set_property(TARGET zipsync PROPERTY CXX_STANDARD 17)
target_link_libraries(zipsync PRIVATE libzipsync CONAN_PKG::args)
target_link_libraries(zipsync PRIVATE stdc++fs)
