#include <lua.hpp>
#include <iostream>
#include "lua_profiler.h"

int main(int argc, char const *argv[])
{
    auto L = luaL_newstate();
    luaL_openlibs(L);
    luaopen_profiler(L);

    {
        if (luaL_dostring(L, "profiler.start()"))
        {
            std::cout << lua_tostring(L, -1) << std::endl;
        }
        if (luaL_dofile(L, "test.lua"))
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
