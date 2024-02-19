file(GLOB RT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB RT_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_compile_definitions(RT_SHADER_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(rt ${RT_SOURCE} ${RT_HEADER} 
    ${VKRG_SAMPLE_COMMON_HEADER} ${VKRG_SAMPLE_COMMON_SOURCE})

target_include_directories(rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VKRG_SAMPLE_COMMON_DIRECTORIES})
target_link_libraries(rt vkrg tinygltf ktx)

add_custom_command(TARGET rt POST_BUILD        # Adds a post-build event to deferred
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
        $<TARGET_FILE:ktx>      # <--this is in-file
        $<TARGET_FILE_DIR:rt>)  