# Stands alone: configure this directory on its own, not from the top level.
# The tool is only needed when the icons change.
cmake_minimum_required(VERSION 3.21)
project(render-icons LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Svg)
qt_standard_project_setup(REQUIRES 6.5)
qt_add_executable(render-icons main.cpp)
target_link_libraries(render-icons PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
