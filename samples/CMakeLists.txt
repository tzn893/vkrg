set(VKRG_SAMPLE_COMMON_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../src;${CMAKE_CURRENT_SOURCE_DIR}/common" CACHE STRING "" FORCE)

add_subdirectory(common)
add_subdirectory(deferred)
add_subdirectory(deferred_serialized)