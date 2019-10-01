cmake_minimum_required(VERSION 3.13)
project(logfault_test VERSION 0.2 LANGUAGES CXX)

add_executable(${PROJECT_NAME} general_tests.cpp)
target_link_libraries(${PROJECT_NAME}
    logfault
)
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
