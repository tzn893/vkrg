
add_executable(deferred-naive-multilight ${CMAKE_CURRENT_SOURCE_DIR}/Naive.cpp 
    ${VKRG_SAMPLE_COMMON_HEADER} ${VKRG_SAMPLE_COMMON_SOURCE})

add_executable(forward-plus-multilight ${CMAKE_CURRENT_SOURCE_DIR}/ForwardPlus.cpp 
    ${VKRG_SAMPLE_COMMON_HEADER} ${VKRG_SAMPLE_COMMON_SOURCE})

add_executable(deferred-tiled-multilight ${CMAKE_CURRENT_SOURCE_DIR}/Tiled.cpp 
    ${VKRG_SAMPLE_COMMON_HEADER} ${VKRG_SAMPLE_COMMON_SOURCE})


target_include_directories(deferred-naive-multilight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VKRG_SAMPLE_COMMON_DIRECTORIES})
target_link_libraries(deferred-naive-multilight vkrg tinygltf ktx)

target_include_directories(forward-plus-multilight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VKRG_SAMPLE_COMMON_DIRECTORIES})
target_link_libraries(forward-plus-multilight vkrg tinygltf ktx)

target_include_directories(deferred-tiled-multilight PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VKRG_SAMPLE_COMMON_DIRECTORIES})
target_link_libraries(deferred-tiled-multilight vkrg tinygltf ktx)

add_custom_command(TARGET deferred-naive-multilight POST_BUILD        # Adds a post-build event to deferred
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
        $<TARGET_FILE:ktx>      # <--this is in-file
        $<TARGET_FILE_DIR:deferred-naive-multilight>)  

add_custom_command(TARGET forward-plus-multilight POST_BUILD        # Adds a post-build event to deferred
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
        $<TARGET_FILE:ktx>      # <--this is in-file
        $<TARGET_FILE_DIR:forward-plus-multilight>)  

add_custom_command(TARGET deferred-tiled-multilight  POST_BUILD        # Adds a post-build event to deferred
    COMMAND ${CMAKE_COMMAND} -E copy_if_different  # which executes "cmake - E copy_if_different..."
        $<TARGET_FILE:ktx>      # <--this is in-file
        $<TARGET_FILE_DIR:deferred-tiled-multilight>)  