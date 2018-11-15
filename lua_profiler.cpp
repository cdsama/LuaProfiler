
#include <vector>
#include <chrono>
#include <unordered_map>
#include <stack>
#include <functional>
#include <iostream>
#include <sstream>
#include <fmt/format.h>
#include <lua.hpp>

using namespace std::chrono;
using time_unit_t = std::chrono::nanoseconds;
using time_point_t = steady_clock::time_point;

static bool is_main_thread(lua_State *L)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    auto main_L = lua_tothread(L, -1);
    lua_pop(L, 1);
    return main_L == L;
}

using function_time_data_t = std::shared_ptr<struct function_time_data>;

struct function_time_data
{
    std::unordered_map<std::string, function_time_data_t> children;
    std::string function_name = "root";
    std::string function_source = "";
    time_unit_t self_time = time_unit_t(0);
    time_unit_t children_time = time_unit_t(0);
    time_unit_t total_time = time_unit_t(0);
    size_t count = 0;
};

struct function_stack_node
{
    std::string function_name = "";
    std::string function_source = "";
    time_point_t call_begin_time = time_point_t(time_unit_t(0));
    time_point_t call_end_time = time_point_t(time_unit_t(0));
    time_unit_t children_tool_time = time_unit_t(0);
    time_unit_t children_pure_time = time_unit_t(0);
    function_time_data_t node = nullptr;
};

static void calculate_time(std::stack<function_stack_node> &data_stack, const time_point_t &begin_time)
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
    function_time_data_t root = std::make_shared<function_time_data>();
    std::stack<function_stack_node> main_thread_stack;
    std::unordered_map<const lua_State *, std::stack<function_stack_node>> coroutine_stack_map;
    // const lua_State *last_thread = nullptr;
    size_t max_function_name_length = 0;
    size_t max_stack_length = 0;

    void calculate_total_time()
    {
        std::stack<function_time_data_t> stack;
        stack.push(root);
        while (!stack.empty())
        {
            auto &current = stack.top();
            stack.pop();
            current->total_time = current->self_time + current->children_time;
            for (auto &&child : current->children)
            {
                stack.push(child.second);
            }
        }
    }

    void calculate_root_time()
    {
        calculate_total_time();
        root->children_time = time_unit_t(0);
        for (auto &&i : root->children)
        {
            root->children_time += i.second->total_time;
        }
        root->total_time = root->children_time;
    }

    std::stack<function_stack_node> &get_function_data_stack(lua_State *L)
    {
        if (is_main_thread(L))
        {
            return main_thread_stack;
        }
        auto itr = coroutine_stack_map.find(L);
        if (itr != coroutine_stack_map.end())
        {
            return itr->second;
        }
        else
        {
            coroutine_stack_map.insert({L, std::stack<function_stack_node>()});
            return coroutine_stack_map[L];
        }
    }

    static const void *reg_key()
    {
        static char c;
        return &c;
    }
};

struct profile_data_userdata
{
    std::shared_ptr<profile_data> pd = nullptr;
};

static int profile_data_gc(lua_State *L)
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
        // pd->last_thread = L;
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

struct scope_on_exit
{
    std::function<void()> on_exit;
    scope_on_exit(std::function<void()> _on_exit)
        : on_exit(_on_exit)
    {
    }
    ~scope_on_exit()
    {
        if (on_exit != nullptr)
        {
            on_exit();
        }
    }
};

struct auto_time
{
    time_point_t begin_time;
    lua_State *L;
    std::string function_name = "";
    std::string function_source = "";
    int event = -1;
    bool is_tail_call = false;

    auto_time(lua_State *_L)
    {
        begin_time = steady_clock::now();
        L = _L;
    }
    ~auto_time()
    {
        std::shared_ptr<profile_data> pd = get_or_new_pd_from_lua(L);

        // std::cout << std::endl
        //           << fmt::format("f:{:<50}    e:{} l:{} mt:{} tc:{}",
        //                          function_name,
        //                          event,
        //                          (const void *)L,
        //                          is_main_thread(L),
        //                          is_tail_call)
        //           << std::endl;
        // scope_on_exit _([&]() { pd->last_thread = L; });
        function_time_data_t parent = nullptr;

        auto &function_data_stack = pd->get_function_data_stack(L);

        if (function_data_stack.empty())
        {
            if (function_name.empty())
            {
                return;
            }
            parent = pd->root;
        }
        else
        {
            parent = function_data_stack.top().node;
        }
        if (function_name.empty())
        {
            function_data_stack.top().children_tool_time += (steady_clock::now() - begin_time);
            return;
        }
        else
        {
            function_time_data_t this_function_data = parent;
            if (event == LUA_HOOKCALL || event == LUA_HOOKTAILCALL)
            {
                auto itr = parent->children.find(function_name);
                if (itr == parent->children.end())
                {
                    this_function_data = std::make_shared<function_time_data>();
                    this_function_data->function_name = function_name;
                    this_function_data->function_source = function_source;
                    parent->children.insert({function_name, this_function_data});
                }
                else
                {
                    this_function_data = itr->second;
                }
            }

            if (event == LUA_HOOKCALL || event == LUA_HOOKTAILCALL)
            {
                function_stack_node node;
                node.function_name = function_name;
                node.function_source = function_source;
                node.call_begin_time = begin_time;
                node.node = this_function_data;
                this_function_data->count++;
                function_data_stack.push(node);
                pd->max_function_name_length = std::max(pd->max_function_name_length,
                                                        function_data_stack.size() * 4 + function_name.length());
                function_data_stack.top().call_end_time = steady_clock::now();
                return;
            }
            else
            {

                // for mismatch after error or return before yield
                while ((!function_data_stack.empty()) &&
                       (function_data_stack.top().function_source != function_source))
                {
                    // std::cout << function_source << "++++++++++++++++++++++++++" << function_data_stack.top().function_source << std::endl;
                    calculate_time(function_data_stack, begin_time);
                }

                // for mismatch right after sethook
                if (function_data_stack.empty())
                {
                    // pd->root = std::make_shared<function_time_data>();
                    // std::cout << "**************************" << std::endl;
                    return;
                }
                else
                {
                    assert(function_data_stack.top().function_source == function_source);
                }

                // for taill call
                while ((!function_data_stack.empty()) &&
                       is_tail_call &&
                       (function_data_stack.top().function_source == function_source))
                {
                    // std::cout << function_source << "-------------------------" << function_data_stack.top().function_source << std::endl;
                    calculate_time(function_data_stack, begin_time);
                }

                if (!is_tail_call)
                {
                    calculate_time(function_data_stack, begin_time);
                }

                if (!function_data_stack.empty())
                {
                    function_data_stack.top().children_tool_time += (steady_clock::now() - begin_time);
                }
            }
        }
    }
};
void print_tree(std::ostream &os, const function_time_data_t &root, size_t max_name_length, size_t max_stack, size_t current_Stack = 0)
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

    if (max_stack > 0 && max_stack < current_Stack)
    {
        return;
    }

    std::vector<std::pair<std::string, function_time_data_t>> sortable_children(root->children.begin(), root->children.end());
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
    if (lua_gettop(L) > 0 && lua_isinteger(L, 1))
    {
        max_stack = std::abs(lua_tointeger(L, 1));
    }

    auto pd = get_or_new_pd_from_lua(L);
    pd->calculate_root_time();
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
    bool is_c_function = (std::strcmp("C", ar->what) == 0);
    if (is_c_function && (ar->name == nullptr))
    {
        return;
    }
    t.function_name = fmt::format("{}:{}:{}",
                                  ar->name == nullptr ? "?" : ar->name,
                                  ar->short_src,
                                  ar->linedefined);
    if (t.event == LUA_HOOKRET)
    {
        lua_getinfo(L, "t", ar);
        t.is_tail_call = ar->istailcall;
    }
    if (is_c_function)
    {
        lua_getinfo(L, "f", ar);
        t.function_source = fmt::format("c:{}:{}",
                                        ar->name,
                                        lua_topointer(L, -1));
        lua_pop(L, 1);
    }
    else
    {
        t.function_source = fmt::format("lua:{}:{}",
                                        ar->short_src,
                                        ar->linedefined);
    }
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