file(GLOB VKRG_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/vkrg/*.cpp)
file(GLOB VKRG_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vkrg/*.h)

add_library(vkrg STATIC ${VKRG_HEADER} ${VKRG_SOURCE})
target_include_directories(vkrg PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(vkrg gvk)