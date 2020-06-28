cmake_minimum_required (VERSION 3.9.6)
project(zipsync CXX C)

option(ZIPSYNC_OPTION_BUILD_TESTS "build executable with unit tests" ON)
option(ZIPSYNC_OPTION_BUILD_TOOL "build command-line zipsync tool" ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "" FORCE)

set(lib_sources
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

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W2")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Ob2")
endif()


find_package(zlib REQUIRED)
find_package(libcurl REQUIRED)
find_package(BLAKE2 REQUIRED)
add_library(libzipsync STATIC ${lib_sources})
set_property(TARGET libzipsync PROPERTY CXX_STANDARD 11)
target_compile_definitions(libzipsync PUBLIC CURL_STATICLIB)
target_include_directories(libzipsync PUBLIC ${zlib_INCLUDE_DIRS} ${BLAKE2_INCLUDE_DIRS} ${libcurl_INCLUDE_DIRS})
target_link_libraries(libzipsync PUBLIC ${zlib_LIBRARIES} ${BLAKE2_LIBRARIES} ${libcurl_LIBRARIES})

if(ZIPSYNC_OPTION_BUILD_TESTS)
    find_package(doctest REQUIRED)
    find_package(libmicrohttpd REQUIRED)
    add_executable(tests ${tests_sources})
    set_property(TARGET tests PROPERTY CXX_STANDARD 17)
    target_include_directories(tests PRIVATE ${doctest_INCLUDE_DIRS} ${libmicrohttpd_INCLUDE_DIRS})
    target_link_libraries(tests PRIVATE libzipsync ${doctest_LIBRARIES} ${libmicrohttpd_LIBRARIES})
    if(NOT MSVC)
        target_link_libraries(tests PRIVATE stdc++fs)
    endif()
endif()

if(ZIPSYNC_OPTION_BUILD_TOOL)
    find_package(args REQUIRED)
    add_executable(zipsync ${zipsynccmd_sources})
    set_property(TARGET zipsync PROPERTY CXX_STANDARD 17)
    target_include_directories(zipsync PRIVATE ${args_INCLUDE_DIRS})
    target_link_libraries(zipsync PRIVATE libzipsync ${args_LIBRARIES})
    if(NOT MSVC)
        target_link_libraries(zipsync PRIVATE stdc++fs)
    endif()
endif()
