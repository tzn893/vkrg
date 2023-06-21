cmake_minimum_required(VERSION 3.24)
set(CMAKE_CXX_STANDARD 17)

option(VKRG_ENABLE_SAMPLE "enable building vulkan render graph samples" Off)
option(VKRG_ENABLE_TEST "enable building test cases" Off)

add_subdirectory(json)
add_subdirectory(general-vulkan)

project(vkrg)

add_subdirectory(src)

if(VKRG_ENABLE_SAMPLE)
	add_subdirectory(samples)
endif()

if(VKRG_ENABLE_TEST)
	add_subdirectory(tests)
endif()
