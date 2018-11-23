# LuaProfiler
Lua function call duration profiler in c++.

Target version : lua 5.3

## Build

### prerequisite
* compiler support c++17 
* cmake
* vcpkg
    - fmtlib
    - lua

### build

```sh
mkdir build
cmake --build ./build --config MinSizeRel --target libLuaProfiler
```

## Integrate

1. Link libLuaProfiler to your project.
2. Include or copy content `lua_profiler.h`.
3. Openlib with code at startup : `luaopen_profiler(L);`.
4. require and use function in lua.

```lua
local luaprofiler = require("luaprofiler")

--[[
    start profile by set hook
    should call it best outside (before function call)
    what you want to profile
]]--
luaprofiler.start() 

--[[
     stop profile with remove hook
     should call it best outside (after function return)
     what you want to profile
]]--
luaprofiler.stop()

--[[
    clear all data
    should call it after report
]]--
luaprofiler.clear() 

--[[ 
    **deprecated**
    report profiling result in string
    should print or log or save it to file
]]--
-- luaprofiler.report_tree()
-- luaprofiler.report_list()

--[[ 
    report profiling result to file
    it will save it at the current work directory
]]--
luaprofiler.report_to_file("json")
luaprofiler.report_to_file("list")
luaprofiler.report_to_file("tree")

```

## Json viewer


### Create standalone gui executable

```sh
pyinstaller json_viewer_main.py -w -F 
```

### Usage

#### Load json file

just drag `*.lua_profile_json.txt` to main window.

>Tips: Installing `everything` on windows helps to find file instantly.

#### Expand stack
1. click on the arrow to expand/fold stack
2. double click to expand the first child recursively
3. double click to fold

![test_result](test_result.png)