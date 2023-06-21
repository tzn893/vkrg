file(GLOB DEFERRED_SERIALIZED_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
file(GLOB DEFERRED_SERIALIZED_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_executable(deferred-serialized ${DEFERRED_SERIALIZED_SOURCE} ${DEFERRED_SERIALIZED_HEADER})

target_include_directories(deferred-serialized PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${VKRG_SAMPLE_COMMON_DIRECTORIES})
target_link_libraries(deferred-serialized vkrg)