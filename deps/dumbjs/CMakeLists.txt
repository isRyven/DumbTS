cmake_minimum_required(VERSION 3.12)
project(dumbjs VERSION 0.1.0 LANGUAGES C)

option(EXAMPLES "Build DumbJS Examples" OFF)

if(MSVC)
	# set(RELEASE_FLAGS "/MP /O2 /sdl /W3 /Ot /Gm- /GS /GL /Gy /WX- /Zc:forScope /Gd /Oi /MD")
	# if (CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
		# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${RELEASE_FLAGS}")
    # endif()
	# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /nologo")
	# set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /GS /Zi /Od /WX- /Zc:forScope /Gd /MDd")
    # set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${RELEASE_FLAGS}")
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -ggdb")
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -flto -Wno-array-bounds -Wno-format-truncation")
endif()

add_library(dumbjs STATIC 
	quickjs.c 
	quickjs.h
	libunicode.c 
	libunicode.h 
	libregexp.c
	libregexp.h
	cutils.c
	cutils.h
	platform.h
)

add_library(dumbjs_h INTERFACE)
target_include_directories(dumbjs_h INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
if (NOT MSVC)
	target_link_libraries(dumbjs m)
endif()
target_link_libraries(dumbjs dumbjs_h)

if(EXAMPLES)
	message(STATUS "Building DumbJS Examples")
	add_subdirectory(examples)
endif()
