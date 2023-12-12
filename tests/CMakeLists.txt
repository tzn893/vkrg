# Now simply link against gtest or gtest_main as needed. Eg
add_subdirectory(googletest)
set(GTEST_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/googletest/googletest/include CACHE INTERNAL "GTEST_INCLUDE") 

set(test_cases dag)

message(STATUS "testing include directory : ${GTEST_INCLUDE}")

foreach(test_case ${test_cases})
  message(STATUS "adding test case ${test_case}...")

  add_executable(
      ${test_case}
      ${test_case}/main.cpp
    )
  target_link_libraries(
      ${test_case}
      gtest
      vkrg
    )

  target_include_directories(
      ${test_case}
      PUBLIC
      ${CMAKE_SOURCE_DIR}/src
      ${GTEST_INCLUDE}
    )

  add_test(
    NAME
     ${test_case}
    COMMAND
      $<TARGET_FILE_DIR:${test_case}>/${test_case}
    )

add_custom_command(TARGET ${test_case} POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:gtest>  $<TARGET_FILE_DIR:${test_case}>
   COMMAND_EXPAND_LISTS
   )
   
  message(STATUS "test case ${test_case} is added")
endforeach()

set_property(TARGET gmock PROPERTY FOLDER gtest-dir)
set_property(TARGET gmock_main PROPERTY FOLDER gtest-dir)
set_property(TARGET gtest PROPERTY FOLDER gtest-dir)
set_property(TARGET gtest_main PROPERTY FOLDER gtest-dir)
