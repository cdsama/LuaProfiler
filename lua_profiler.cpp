#include <iostream>
#include <vector>
#include <lua.hpp>
#include <fmt/format.h>
#include <chrono>
#include <unordered_map>
#include <stack>
#include <sstream>

using namespace std::chrono;
using time_unit_t = std::chrono::nanoseconds;


using function_time_data_t = std::shared_ptr<struct function_time_data>;

struct function_time_data
{
    function_time_data()
        : function_name("root"), self_time(0), children_time(0), total_time(0), count(0)
    {
    }
    std::unordered_map<std::string, function_time_data_t> children;

    std::string function_name;
    time_unit_t self_time;
    time_unit_t children_time;
    time_unit_t total_time;
    size_t count;
};


struct function_stack_node
{
    function_stack_node()
        : function_name(""), call_begin_time(0), call_end_time(0), children_tool_time(0), children_pure_time(0), node(nullptr)
    {
    }
    std::string function_name;
    time_unit_t call_begin_time;
    time_unit_t call_end_time;
    time_unit_t children_tool_time;
    time_unit_t children_pure_time;
    function_time_data_t node;
};

static void calculate_time(std::stack<function_stack_node> &data_stack, const time_unit_t &begin_time)
{
    auto &current_top = data_stack.top();
    // this_all = this_tool_time + children + children_tool_time + self
    // this_sub = children_tooltime + self + children
    auto tool_total_time = current_top.call_end_time - current_top.call_begin_time + current_top.children_tool_time;
    auto sub_time = begin_time - current_top.call_end_time;
    auto pure_sub_time = sub_time - current_top.children_tool_time;
    auto self_time = pure_sub_time - current_top.children_pure_time;
    current_top.node->children_time += current_top.children_pure_time;
    current_top.node->self_time += self_time;
    data_stack.pop();
    if (!data_stack.empty())
    {
        data_stack.top().children_tool_time += tool_total_time;
        data_stack.top().children_pure_time += pure_sub_time;
    }
}

struct profile_data
{
    profile_data()
    {
        root = std::make_shared<function_time_data>();
        max_function_name_length = 0;
        max_stack_length = 0;
    }
    function_time_data_t root;
    std::stack<function_stack_node> function_data_stack;
    size_t max_function_name_length;
    size_t max_stack_length;

    static const void *reg_key()
    {
        static char c;
        return &c;
    }
};

struct profile_data_userdata
{
    std::shared_ptr<profile_data> pd;
};

int profile_data_gc(lua_State *L)
{
    auto pdptr = static_cast<profile_data_userdata *>(lua_touserdata(L, -1));
    pdptr->pd = nullptr;
    return 0;
}

static std::shared_ptr<profile_data> get_or_new_pd_from_lua(lua_State *L)
{
    auto top = lua_gettop(L);
    std::shared_ptr<profile_data> pd;
    lua_rawgetp(L, LUA_REGISTRYINDEX, profile_data::reg_key());
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        auto dataptr = new (lua_newuserdata(L, sizeof(profile_data_userdata))) profile_data_userdata();
        pd = std::make_shared<profile_data>();
        dataptr->pd = pd;
        if (luaL_newmetatable(L, "profile_data_metatable"))
        {
            lua_pushcfunction(L, profile_data_gc);
            lua_setfield(L, -2, "__gc");
        }
        lua_setmetatable(L, -2);
        lua_rawsetp(L, LUA_REGISTRYINDEX, profile_data::reg_key());
    }
    else
    {
        auto dataptr = (profile_data_userdata *)lua_touserdata(L, -1);
        pd = dataptr->pd;
        lua_pop(L, 1);
    }
    assert(lua_gettop(L) == top);
    return pd;
}

struct auto_time
{
    time_unit_t begin_time;
    std::string function_name;
    int event;
    lua_State *L;

    auto_time(lua_State *_L)
    {
        begin_time = duration_cast<time_unit_t>(steady_clock::now().time_since_epoch());
        L = _L;
    }
    ~auto_time()
    {
        
        if (event) {
            std::cout<<std::endl<<fmt::format("f:{:>50}    e:{} l:{}", function_name, event, (const void*)L)<<std::endl;
        }
        else {
            std::cout<<std::endl<<fmt::format("f:{:<50}    e:{} l:{}", function_name, event, (const void*)L)<<std::endl;
        }
        
        
        std::shared_ptr<profile_data> pd = get_or_new_pd_from_lua(L);
        function_time_data_t parent = nullptr;
        if (pd->function_data_stack.empty())
        {
            if (function_name.empty())
            {
                return;
            }
            parent = pd->root;
        }
        else
        {
            parent = pd->function_data_stack.top().node;
        }
        if (function_name.empty())
        {
            pd->function_data_stack.top().children_tool_time += (duration_cast<time_unit_t>(steady_clock::now().time_since_epoch()) - begin_time);
            return;
        }
        else
        {
            function_time_data_t this_function_data = parent;
            if (event == LUA_HOOKCALL)
            {
                auto itr = parent->children.find(function_name);
                if (itr == parent->children.end())
                {
                    this_function_data = std::make_shared<function_time_data>();
                    this_function_data->function_name = function_name;
                    parent->children.insert({function_name, this_function_data});
                }
                else
                {
                    this_function_data = itr->second;
                }
            }

            if (event == LUA_HOOKCALL)
            {
                function_stack_node node;
                node.function_name = function_name;
                node.call_begin_time = begin_time;
                node.node = this_function_data;
                this_function_data->count++;
                pd->function_data_stack.push(node);
                pd->max_function_name_length = std::max(pd->max_function_name_length, pd->function_data_stack.size()*4 + function_name.length());
                pd->function_data_stack.top().call_end_time = duration_cast<time_unit_t>(steady_clock::now().time_since_epoch());
                return;
            }
            else
            {
                // for unmatch after error
                while ((!pd->function_data_stack.empty()) && (pd->function_data_stack.top().function_name != function_name))
                {
                    std::cout<<"++++++++++++++++++++++++++"<<std::endl;
                    calculate_time(pd->function_data_stack, begin_time);
                }

                if (pd->function_data_stack.empty())
                {
                    // pd->root = std::make_shared<function_time_data>();
                    std::cout<<"**************************"<<std::endl;
                    return;
                }
                else
                {
                    assert(pd->function_data_stack.top().function_name == function_name);
                }

                calculate_time(pd->function_data_stack, begin_time);
                if (!pd->function_data_stack.empty())
                {
                    pd->function_data_stack.top().children_tool_time += (duration_cast<time_unit_t>(steady_clock::now().time_since_epoch()) - begin_time);
                }
            }
        }
    }
};
void print_tree(std::ostream &os, const function_time_data_t& root, size_t max_name_length, size_t max_stack, size_t current_Stack = 0)
{
    std::string indent = current_Stack == 0 ? "" : fmt::format(fmt::format("{{:{}}}", current_Stack * 4), "");
    std::string align = fmt::format(fmt::format("{{:{}}}", (max_name_length - current_Stack * 4 - root->function_name.length())), "");

    os << fmt::format("{}{}{} count:{:>6} self:{:<10} children:{:<10} total:{:<15}",
                      indent,
                      root->function_name,
                      align,
                      root->count,
                      root->self_time.count(),
                      root->children_time.count(),
                      root->total_time.count())
       << std::endl;
    if (root->children.empty())
    {
        return;
    }
    
    if (max_stack>0 && max_stack < current_Stack) {
        return;
    }
    

    std::vector<std::pair<std::string, function_time_data_t>> sortable_children(root->children.begin(), root->children.end());

    for (auto &i : sortable_children)
    {
        auto &data = i.second;
        data->total_time = data->self_time + data->children_time;
    }
    std::sort(sortable_children.begin(), sortable_children.end(), [](const auto &l, const auto &r) {
        return l.second->total_time > r.second->total_time;
    });
    for (auto &i : sortable_children)
    {
        print_tree(os, i.second, max_name_length, max_stack, current_Stack + 1);
    }
}

int profile_report_tree(lua_State *L)
{
    size_t max_stack = 0;
    if (lua_gettop(L)>0 && lua_isinteger(L, 1)) {
        max_stack = std::abs(lua_tointeger(L,1));
    }
    
    auto pd = get_or_new_pd_from_lua(L);
    std::ostringstream os;
    print_tree(os, pd->root, pd->max_function_name_length + 2, 0);
    lua_pushstring(L, os.str().c_str());
    return 1;
}

void profile_hooker(lua_State *L, lua_Debug *ar)
{
    auto_time t(L);
    lua_getinfo(L, "Sn", ar);
    t.event = ar->event;
    if ((std::strcmp("C", ar->what) == 0) && (ar->name == nullptr))
    {
        return;
    }
    t.function_name = fmt::format("{}:{}:{}",
                                  ar->name == nullptr ? "?" : ar->name,
                                  ar->short_src,
                                  ar->linedefined);
}

int profile_start(lua_State *L)
{
    lua_sethook(L, profile_hooker, LUA_MASKCALL | LUA_MASKRET, 0);
    return 0;
}

int profile_stop(lua_State *L)
{
    lua_sethook(L, nullptr, 0, 0);
    return 0;
}

int profile_clear(lua_State *L)
{
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, profile_data::reg_key());
    return 0;
}

int new_lib_profiler(lua_State *L)
{
    lua_newtable(L);
    luaL_Reg lib_funcs[] = {{"start", profile_start},
                            {"stop", profile_stop},
                            {"clear", profile_clear},
                            {"report_tree", profile_report_tree},
                            {nullptr, nullptr}};
    luaL_setfuncs(L, lib_funcs, 0);
    return 1;
}

int luaopen_profiler(lua_State *L)
{
    luaL_requiref(L, "profiler", new_lib_profiler, 1);
    return 0;
}