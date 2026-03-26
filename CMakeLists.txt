cmake_minimum_required (VERSION 3.9.6)
project(zipsync CXX C)

include(CheckCXXSourceCompiles)

# Check if C++17 filesystem is available
set(CMAKE_CXX_STANDARD 17)
check_cxx_source_compiles(
    "#include <filesystem>
     int main() { std::filesystem::path p; return 0; }"
    HAS_CXX17_FILESYSTEM)

if(NOT HAS_CXX17_FILESYSTEM)
    set(CMAKE_CXX_STANDARD 11)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ZIPSYNC_OPTION_BUILD_TESTS "build executable with unit tests" ON)
option(ZIPSYNC_OPTION_BUILD_TOOL "build command-line zipsync tool" ON)

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
    ChecksummedZip.h
    ChecksummedZip.cpp
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

set(extralib_sources
    StdFilesystem.cpp
    StdFilesystem.h
    Wildcards.cpp
    Wildcards.h
    CommandLine.cpp
    CommandLine.h
)

set(zipsynccmd_sources
    CommandLineMain.cpp
)


if(MSVC)
    add_compile_options("/W2")
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:/Ob2>")
    add_compile_options("$<$<CONFIG:Release>:/Zi>")
endif()

# minizip
find_package(minizip CONFIG QUIET)
if(NOT minizip_FOUND)
    find_path(MINIZIP_INCLUDE_DIR minizip/unzip.h)
    find_library(MINIZIP_LIBRARY minizip)
    if(NOT MINIZIP_INCLUDE_DIR OR NOT MINIZIP_LIBRARY)
        message(FATAL_ERROR "minizip not found")
    endif()
    add_library(minizip::minizip INTERFACE IMPORTED)
    set_target_properties(minizip::minizip PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MINIZIP_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${MINIZIP_LIBRARY}")
endif()

# CURL
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
    find_package(CURL REQUIRED)  # CMake has a built-in finder for this
endif()

# blake2
find_package(blake2 CONFIG QUIET)
if(NOT blake2_FOUND)
    find_path(BLAKE2_INCLUDE_DIR blake2.h)
    find_library(BLAKE2_LIBRARY NAMES blake2 b2)
    if(NOT BLAKE2_INCLUDE_DIR OR NOT BLAKE2_LIBRARY)
        message(FATAL_ERROR "blake2 not found")
    endif()
    add_library(blake2::blake2 INTERFACE IMPORTED)
    set_target_properties(blake2::blake2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${BLAKE2_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${BLAKE2_LIBRARY}")
endif()

add_library(libzipsync STATIC ${lib_sources})
target_link_libraries(libzipsync PUBLIC minizip::minizip blake2::blake2 PRIVATE CURL::libcurl)

add_library(libzipsyncextra STATIC ${extralib_sources})
target_link_libraries(libzipsyncextra PUBLIC libzipsync)
if(NOT MSVC AND NOT APPLE)
    target_link_libraries(libzipsyncextra PUBLIC stdc++fs)
endif()

if(ZIPSYNC_OPTION_BUILD_TESTS)
    find_package(doctest REQUIRED CONFIG)
	# libmicrohttpd
	find_package(libmicrohttpd CONFIG QUIET)
	if(NOT libmicrohttpd_FOUND)
	    find_path(MICROHTTPD_INCLUDE_DIR microhttpd.h)
	    find_library(MICROHTTPD_LIBRARY NAMES microhttpd libmicrohttpd)
	    if(NOT MICROHTTPD_INCLUDE_DIR OR NOT MICROHTTPD_LIBRARY)
	        message(FATAL_ERROR "libmicrohttpd not found")
	    endif()
	    add_library(libmicrohttpd::libmicrohttpd INTERFACE IMPORTED)
	    set_target_properties(libmicrohttpd::libmicrohttpd PROPERTIES
	        INTERFACE_INCLUDE_DIRECTORIES "${MICROHTTPD_INCLUDE_DIR}"
	        INTERFACE_LINK_LIBRARIES "${MICROHTTPD_LIBRARY}")
	endif()

    add_executable(tests ${tests_sources})
    target_link_libraries(tests libzipsync libzipsyncextra doctest::doctest libmicrohttpd::libmicrohttpd CURL::libcurl minizip::minizip blake2::blake2)
endif()

if(ZIPSYNC_OPTION_BUILD_TOOL)
	# args
	find_package(args CONFIG QUIET)
	if(NOT args_FOUND)
	    find_path(ARGS_INCLUDE_DIR args.hxx HINTS /opt/homebrew/include)
	    if(NOT ARGS_INCLUDE_DIR)
	        message(FATAL_ERROR "args not found — install with: brew install args")
	    endif()
	    add_library(args::args INTERFACE IMPORTED)
	    set_target_properties(args::args PROPERTIES
	        INTERFACE_INCLUDE_DIRECTORIES "${ARGS_INCLUDE_DIR}")
	endif()

    add_executable(zipsync ${zipsynccmd_sources})
    target_link_libraries(zipsync libzipsync libzipsyncextra args::args)
endif()
