cmake_minimum_required(VERSION 3.30)
project(WiiStatic)

set(CMAKE_CXX_STANDARD 20)

add_executable(WiiStatic main.cpp)

target_link_libraries(WiiStatic
        aesnd
        wiiuse
        bte
        ogc
        m
        fat
)

ogc_create_dol(WiiStatic)
