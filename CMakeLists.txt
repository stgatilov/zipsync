cmake_minimum_required (VERSION 3.9.6)
project(tdmsync2 CXX C)

include(conanbuildinfo.cmake)
conan_basic_setup()

set(CMAKE_CONFIGURATION_TYPES "Debug;RelWithDebInfo" CACHE STRING "" FORCE)

set(sources
    tsassert.h
    tsassert.cpp
    TdmSync.h
    TdmSync.cpp

    StdFilesystem.cpp
    StdFilesystem.h
    StdString.cpp
    StdString.h

    minizip_extra.c
    minizip_extra.h
    minizip_private.h
)

set(tests_sources
    tests.cpp
)

set(playground_sources
    main.cpp
)


if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W2")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Ob2 /FAs")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
endif()


add_library(libtdmsync ${sources})

add_executable(tests ${tests_sources})
target_link_libraries(tests libtdmsync ${CONAN_LIBS})

add_executable(playground ${playground_sources})
target_link_libraries(playground libtdmsync ${CONAN_LIBS})