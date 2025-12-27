cmake_minimum_required(VERSION 4.0)
project(GPU_Particle_Sim)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)
