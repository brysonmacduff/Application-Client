set(TEST ${COMPONENT}_test)

file(GLOB SOURCES "*.h" "*.cpp")

add_executable(${TEST} ${SOURCES})
target_link_libraries(${TEST} ${COMPONENT} GTest::gtest_main)
target_include_directories(${TEST} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
