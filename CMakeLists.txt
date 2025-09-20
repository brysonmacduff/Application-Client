cmake_minimum_required(VERSION 3.22)
project(application_client CXX)
set(CMAKE_CXX_STANDARD 20)

include(GNUInstallDirs)
find_package(GTest REQUIRED)

add_subdirectory(lib)