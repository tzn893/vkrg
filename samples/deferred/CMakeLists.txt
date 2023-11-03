file(GLOB DEFERRED_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB DEFERRED_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_executable(deferred ${DEFERRED_SOURCE} ${DEFERRED_HEADER} 
    ${VKRG_SAMPLE_COMMON_HEADER} ${VKRG_SAMPLE_COMMON_SOURCE})

target_include_directories(deferred PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VKRG_SAMPLE_COMMON_DIRECTORIES})
target_link_libraries(deferred vkrg tinygltf ktx)

add_custom_command(TARGET deferred POST_BUILD        # Adds a post-build event to deferred
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
        $<TARGET_FILE:ktx>      # <--this is in-file
        $<TARGET_FILE_DIR:deferred>)  