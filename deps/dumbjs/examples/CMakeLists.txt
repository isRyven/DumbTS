cmake_minimum_required(VERSION 3.12)

add_executable(djs djs.c)
target_link_libraries(djs dumbjs)
target_compile_definitions(djs PRIVATE 
	CONFIG_VERSION="${CMAKE_PROJECT_VERSION}"
)
