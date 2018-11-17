#include <lua.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include "lua_profiler.h"

int lua_sleep(lua_State *L)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 0;
}

int main(int argc, char const *argv[])
{
    auto L = luaL_newstate();
    luaL_openlibs(L);
    lua_pushcfunction(L, lua_sleep);
    lua_setglobal(L, "sleep");
    luaopen_profiler(L);

    {
        using namespace std::chrono;
        if (luaL_dofile(L, "test.lua"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
            lua_close(L);
            return 0;
        }
        luaL_loadfile(L, "test.lua");
        auto begin = steady_clock::now();
        lua_call(L, 0, 0);
        auto duration = steady_clock::now() - begin;
        std::cout << "test finished in " << duration.count() << " ns" << std::endl;
        if (luaL_dostring(L, "profiler = require(\"profiler\")  profiler.start()"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }

        luaL_dofile(L, "test.lua");

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
