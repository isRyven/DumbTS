cmake_minimum_required(VERSION 3.12)
project(dumbts VERSION 0.1.0 LANGUAGES C CXX)

include(deps/zrc/CMakeZRC.cmake)

set(EXAMPLES ON CACHE BOOL "build dumbjs examples")
add_subdirectory(deps/dumbjs)
target_compile_definitions(dumbjs PRIVATE NOSTACKCHECKS)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -D_GNU_SOURCE -Wno-unused-parameter")
	if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Wunreachable-code")
    endif()
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s -flto -Wno-array-bounds -Wno-format-truncation -O3")
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -ggdb")
endif()

add_executable(dumbts main.c libstd.c libstd.h)
target_link_libraries(dumbts dumbjs)

zrc_add_resource_library(assets 
	WORKING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/js
	manifest.json
)

file(GLOB ts_scripts RELATIVE "${PROJECT_SOURCE_DIR}/ts" "${PROJECT_SOURCE_DIR}/ts/*" )
zrc_add_resources(assets
	WORKING_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ts
	${ts_scripts}
)

file(GLOB js_scripts "${PROJECT_SOURCE_DIR}/js/*.js")
set(js_assets_temp "${CMAKE_CURRENT_BINARY_DIR}/__js_assets_temp")
file(MAKE_DIRECTORY ${js_assets_temp})
list(TRANSFORM js_scripts REPLACE "^${PROJECT_SOURCE_DIR}/js/(.+)" "\\1bin" OUTPUT_VARIABLE js_bin_scripts)

zrc_add_resources(assets 
	WORKING_DIR ${js_assets_temp}
	${js_bin_scripts}
)

# special handling for tsc
set(js_scripts_tsc "${PROJECT_SOURCE_DIR}/js/tsc.js")
list(REMOVE_ITEM js_scripts js_scripts_tsc)
# compile tsc in global environment
add_custom_target(js_precompile_scripts_tsc
  	COMMAND djs -c ${js_scripts_tsc}
  	DEPENDS djs ${js_scripts_tsc}
  	WORKING_DIRECTORY ${js_assets_temp}
  	COMMENT "Precompiling tsc.js script"
)

add_custom_target(js_precompile_scripts
  	COMMAND djs -cm ${js_scripts}
  	DEPENDS djs ${js_scripts}
  	WORKING_DIRECTORY ${js_assets_temp}
  	COMMENT "Precompiling js scripts"
)

add_dependencies(assets js_precompile_scripts_tsc js_precompile_scripts)

target_link_libraries(dumbts assets)
