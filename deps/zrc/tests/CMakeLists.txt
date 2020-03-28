cmake_minimum_required(VERSION 3.12)

zrc_add_resource_library(assets 
	WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
	assets/controllers/controller.txt assets/manifest.txt
)

zrc_add_resources(assets
	WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
	assets/bootstrap.txt
)

add_executable(test_zrc test_zrc.c)
target_link_libraries(test_zrc assets)
add_test(test_zrc test_zrc)

set_property(
	TEST test_zrc
	PROPERTY PASS_REGULAR_EXPRESSION "^print: \"hello world\""
)
