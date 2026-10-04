cmake_minimum_required(VERSION 3.10)
project(SandBots CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)

add_executable(sandbots
  src/main.cpp
  src/sand.cpp
  src/physics.cpp
  src/elec.cpp)

target_include_directories(sandbots PRIVATE ${SDL2_INCLUDE_DIRS})
if(TARGET SDL2::SDL2)
  target_link_libraries(sandbots PRIVATE SDL2::SDL2)
else()
  target_link_libraries(sandbots PRIVATE ${SDL2_LIBRARIES})
endif()
if(NOT MSVC)
  target_compile_options(sandbots PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
