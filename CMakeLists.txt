cmake_minimum_required(VERSION 3.16)
project(lpss_info VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(RMVL REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(lpss_info src/main.cpp)

target_link_libraries(lpss_info PRIVATE 
    rmvl_lpss 
    rmvl_io 
    rmvl_core
    pthread
)

target_include_directories(lpss_info PRIVATE ${RMVL_INCLUDE_DIRS})

# 发布者测试节点
add_executable(test_pub test/publisher_node.cpp)
target_link_libraries(test_pub PRIVATE rmvl_lpss rmvl_core)

# 订阅者测试节点
add_executable(test_sub test/subscriber_node.cpp)
target_link_libraries(test_sub PRIVATE rmvl_lpss rmvl_core)

find_package(Doxygen)
if (DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
endif()