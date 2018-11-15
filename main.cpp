#include <lua.hpp>
#include <iostream>
#include <chrono>
#include "lua_profiler.h"

int main(int argc, char const *argv[])
{
    auto L = luaL_newstate();
    luaL_openlibs(L);
    luaopen_profiler(L);

    {
        if (luaL_dofile(L, "test.lua"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
        using namespace std::chrono;
        auto begin = steady_clock::now();
        if (luaL_dofile(L, "test.lua"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
        auto duration = steady_clock::now() - begin;
        std::cout << "test finished in " << duration.count() << " ns" << std::endl;
        if (luaL_dostring(L, "profiler.start()"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
        if (luaL_dofile(L, "test.lua"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
        if (luaL_dostring(L, "profiler.stop()"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
        if (luaL_dostring(L, "print(profiler.report_tree())"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
    }

    lua_close(L);

    return 0;
}
