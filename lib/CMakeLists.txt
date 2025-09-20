set(COMPONENT ${PROJECT_NAME})

file(GLOB SOURCES "*.h" "*.cpp")

add_library(${COMPONENT} STATIC ${SOURCES})
target_include_directories(${COMPONENT} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(test)