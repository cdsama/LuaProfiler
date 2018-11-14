cmake_minimum_required(VERSION 3.10)
set(VCPKG_CRT_LINKAGE static)
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
endif()
message(STATUS "CMAKE_EXPORT_COMPILE_COMMANDS: ${CMAKE_EXPORT_COMPILE_COMMANDS}")

project(LuaProfiler CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(Lua REQUIRED)
find_package(fmt CONFIG REQUIRED)
add_library(libLuaProfiler STATIC lua_profiler.cpp)
target_link_libraries(libLuaProfiler PRIVATE fmt::fmt-header-only)
add_executable(LuaProfiler main.cpp)
target_link_libraries(LuaProfiler PUBLIC ${LUA_LIBRARY} PRIVATE libLuaProfiler)
include_directories(${LUA_INCLUDE_DIR})
