
#include <vector>
#include <chrono>
#include <unordered_map>
#include <stack>
#include <functional>
#include <iostream>
#include <sstream>
#include <fstream>
#include <fmt/format.h>
#include <lua.hpp>
#include <thread>

using namespace std::chrono;
using record_clock_t = high_resolution_clock;
using time_point_t = record_clock_t::time_point;
using time_unit_t = record_clock_t::duration;

static lua_State *get_main_thread(lua_State *L)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    auto main_L = lua_tothread(L, -1);
    lua_pop(L, 1);
    return main_L;
}

static bool is_couroutine_dead(lua_State *L, lua_State *co)
{
    if (L == co)
    {
        return false; // running
    }
    else
    {
        switch (lua_status(co))
        {
        case LUA_YIELD:
            return false; // suspended
        case LUA_OK:
        {
            lua_Debug ar;
            if (lua_getstack(co, 0, &ar) > 0)
            {
                return false; // running
            }
            else if (lua_gettop(co) == 0)
            {
                return true; // dead
            }
            else
            {
                return false; // dead
            }
        }
        default:
            return true; // dead with error
        }
    }
}

using function_time_data_t = std::shared_ptr<struct function_time_data>;

enum class sort_t : uint8_t
{
    none,
    total_time,
    self_time,
    children_time,
    add_time,
};

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

struct function_time_data
{
    std::unordered_map<std::string, function_time_data_t> children;
    std::string function_name = "root";
    std::string function_source = "";
    time_unit_t self_time = {};
    time_unit_t children_time = {};
    time_unit_t total_time = {};
    size_t count = 0;
};

template <sort_t sort_type = sort_t::self_time>
bool function_time_data_sort(const function_time_data_t &l, const function_time_data_t &r)
{
    return l->self_time < r->self_time;
}

template <>
bool function_time_data_sort<sort_t::children_time>(const function_time_data_t &l, const function_time_data_t &r)
{
    return l->children_time < r->children_time;
}

template <>
bool function_time_data_sort<sort_t::total_time>(const function_time_data_t &l, const function_time_data_t &r)
{
    return l->total_time < r->total_time;
}

template <>
bool function_time_data_sort<sort_t::add_time>(const function_time_data_t &l, const function_time_data_t &r)
{
    return l->self_time + l->children_time < r->self_time + r->children_time;
}
using on_traverse_function = std::function<void(function_time_data_t & /*current*/, size_t /*current_stack*/)>;

template <sort_t sort_type>
static void traverse_tree(function_time_data_t &root, size_t max_stack, on_traverse_function on_traverse)
{
    std::stack<function_time_data_t> stack;
    size_t current_stack = 0;
    stack.push(root);

    while (!stack.empty())
    {
        auto current = stack.top();
        stack.pop();

        if (current == nullptr)
        {
            --current_stack;
            continue;
        }

        if (on_traverse != nullptr)
        {
            on_traverse(current, current_stack);
        }

        if (current->children.empty())
        {
            continue;
        }

        if (max_stack > 0 && max_stack < current_stack)
        {
            continue;
        }

        stack.push(nullptr);
        ++current_stack;

        if constexpr (sort_type == sort_t::none)
        {
            for (auto &&child : current->children)
            {
                stack.push(child.second);
            }
        }
        else
        {
            std::vector<function_time_data_t> sortable_children;
            sortable_children.reserve(current->children.size());
            for (auto &&child : current->children)
            {
                sortable_children.push_back(child.second);
            }
            std::sort(sortable_children.begin(), sortable_children.end(), function_time_data_sort<sort_type>);
            for (auto &&i : sortable_children)
            {
                stack.push(i);
            }
        }
    }
}

struct function_stack_node
{
    std::string function_name = "";
    std::string function_source = "";
    time_point_t call_begin_time = {};
    time_point_t call_end_time = {};
    time_unit_t children_tool_time = {};
    time_unit_t children_pure_time = {};
    time_unit_t children_coroutine_time = {};
    function_time_data_t node = nullptr;
    bool is_tail_call = false;
};

static void calculate_time(std::stack<function_stack_node> &data_stack, const time_point_t &begin_time, bool &is_tail_call_popped)
{
    auto &current_top = data_stack.top();
    // this_all = this_tool_time + children + children_tool_time + self
    // this_sub = children_tooltime + self + children
    auto tool_total_time = current_top.call_end_time - current_top.call_begin_time + current_top.children_tool_time;
    auto coroutine_time = current_top.children_coroutine_time;
    auto sub_time = begin_time - current_top.call_end_time;
    auto pure_sub_time = sub_time - current_top.children_tool_time - coroutine_time;
    auto self_time = pure_sub_time - current_top.children_pure_time;
    current_top.node->children_time += current_top.children_pure_time;
    current_top.node->self_time += self_time;
    is_tail_call_popped = current_top.is_tail_call;
    data_stack.pop();
    if (!data_stack.empty())
    {
        data_stack.top().children_tool_time += tool_total_time;
        data_stack.top().children_pure_time += pure_sub_time;
        data_stack.top().children_coroutine_time += coroutine_time;
    }
}

static size_t per_indent_length = 4;
static size_t space_after_name = 4;
struct profile_data
{
    function_time_data_t root = std::make_shared<function_time_data>();
    std::stack<function_stack_node> main_thread_stack;
    std::unordered_map<lua_State *, std::stack<function_stack_node>> coroutine_stack_map;
    std::unordered_map<lua_State *, std::string> coroutine_name_map;
    lua_State *last_thread_of_hook = nullptr;
    lua_State *last_thread = nullptr;
    lua_State *main_thread = nullptr;
    time_point_t last_tool_begin = {};
    time_point_t last_tool_end = {};

    bool is_main_thread(lua_State *L) const
    {
        return main_thread == L;
    }

    std::string get_coroutine_name(lua_State *L)
    {
        scope_on_exit _([&]() {
            if (!is_main_thread(L))
            {
                if (auto itr = coroutine_stack_map.find(L); itr != coroutine_stack_map.end())
                {
                    if (itr->second.empty())
                    {
                        coroutine_stack_map.erase(itr);
                        if (auto itr = coroutine_name_map.find(L); itr != coroutine_name_map.end())
                        {
                            coroutine_name_map.erase(itr);
                        }
                    }
                }
                else
                {
                    if (auto itr = coroutine_name_map.find(L); itr != coroutine_name_map.end())
                    {
                        coroutine_name_map.erase(itr);
                    }
                }
            }
        });
        if (is_main_thread(L))
        {
            return "mainthread";
        }

        if (auto itr = coroutine_name_map.find(L); itr != coroutine_name_map.end())
        {
            return itr->second;
        }
        return "coroutine:[?]";
    }

    void calculate_total_time(size_t max_stack)
    {
        traverse_tree<sort_t::none>(root, max_stack, [](function_time_data_t &current, size_t current_stack) {
            current->total_time = current->self_time + current->children_time;
        });
        std::stack<function_time_data_t> stack;
        stack.push(root);
        while (!stack.empty())
        {
            auto &current = stack.top();
            stack.pop();

            for (auto &&child : current->children)
            {
                stack.push(child.second);
            }
        }
    }

    void calculate_root_time(size_t max_stack)
    {
        calculate_total_time(max_stack);
        root->children_time = {};
        for (auto &&i : root->children)
        {
            root->children_time += i.second->total_time;
        }
        root->total_time = root->children_time;
    }

    std::stack<function_stack_node> &get_function_data_stack(lua_State *L, std::string *name = nullptr)
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
            if (name == nullptr)
            {
                static std::stack<function_stack_node> dummy;
                return dummy;
            }
            coroutine_name_map.insert({L, fmt::format("coroutine:{}", *name)});
            return coroutine_stack_map[L];
        }
    }

    static const void *reg_key()
    {
        static char c;
        return &c;
    }

    size_t get_max_function_name_length(size_t max_stack)
    {
        size_t max_function_name_length = 0;
        traverse_tree<sort_t::none>(root, max_stack, [&](function_time_data_t &current, size_t current_statck) {
            max_function_name_length = std::max(max_function_name_length, (current->function_name.length() + current_statck * per_indent_length));
        });
        return max_function_name_length;
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
        pd->last_thread_of_hook = L;
        pd->last_thread = L;
        pd->main_thread = get_main_thread(L);
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
    time_point_t begin_time;
    lua_State *L;
    std::string function_name = "";
    std::string function_source = "";
    int event = -1;
    // bool is_tail_call = false;

    auto_time(lua_State *_L)
    {
        begin_time = record_clock_t::now();
        L = _L;
    }
    ~auto_time()
    {
        std::shared_ptr<profile_data> pd = get_or_new_pd_from_lua(L);

        scope_on_exit _([&]() {
            pd->last_thread_of_hook = L;

            if (function_name.empty() || event == LUA_HOOKRET)
            {
                pd->last_tool_begin = begin_time;
                pd->last_tool_end = record_clock_t::now();
            }
            else
            {
                pd->last_tool_begin = {};
                pd->last_tool_end = {};
                auto &call_end_time = pd->get_function_data_stack(L).top().call_end_time;
                call_end_time = record_clock_t::now();
            }
        });

        if (L != pd->last_thread_of_hook)
        {
            pd->last_thread = pd->last_thread_of_hook;
        }

        // std::cout << std::endl
        //           << fmt::format("f:{:<50}    e:{} l:{} mt:{:5} ll:{}",
        //                          function_name,
        //                          event,
        //                          (const void *)L,
        //                          pd->is_main_thread(L),
        //                          (const void *)pd->last_thread)
        //           << std::endl;

        // delay calculate tool time
        if (auto &last_function_data_stack = pd->get_function_data_stack(pd->last_thread_of_hook); !last_function_data_stack.empty())
        {
            last_function_data_stack.top().children_tool_time += (pd->last_tool_end - pd->last_tool_begin);
            // std::cout << function_data_stack.top().function_name << "*************" << (pd->last_tool_end - pd->last_tool_begin).count() << std::endl;
        }

        function_time_data_t parent = nullptr;
        auto &function_data_stack = pd->get_function_data_stack(L, &function_name);
        parent = function_data_stack.empty() ? pd->root : function_data_stack.top().node;
        if (function_name.empty())
        {
            return;
        }
        else
        {
            function_time_data_t this_function_data = nullptr;
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
                node.is_tail_call = (event == LUA_HOOKTAILCALL);
                this_function_data->count++;
                function_data_stack.push(node);
                return;
            }
            else
            {
                if (L != pd->last_thread_of_hook)
                {
                    auto status = lua_status(pd->last_thread_of_hook);
                    // std::cout << "###########################"
                    //           << fmt::format("thread:{} status:{} ismain:{} isdead:{}",
                    //                          (const void *)pd->last_thread_of_hook,
                    //                          status,
                    //                          pd->is_main_thread(pd->last_thread_of_hook),
                    //                          is_couroutine_dead(L, pd->last_thread_of_hook))
                    //           << std::endl;

                    if (is_couroutine_dead(L, pd->last_thread_of_hook))
                    {
                        auto &last_function_data_stack = pd->get_function_data_stack(pd->last_thread_of_hook);
                        bool is_tail_call_popped = false;
                        while (!last_function_data_stack.empty())
                        {
                            calculate_time(last_function_data_stack, begin_time, is_tail_call_popped);
                        }
                    }

                    if (!function_data_stack.empty())
                    {
                        auto &top = function_data_stack.top();
                        top.children_coroutine_time += (begin_time - top.call_end_time);
                        function_time_data_t coroutine_function_data = nullptr;
                        std::string coroutine_function_name = pd->get_coroutine_name(pd->last_thread_of_hook);
                        auto itr = top.node->children.find(coroutine_function_name);
                        if (itr == top.node->children.end())
                        {
                            coroutine_function_data = std::make_shared<function_time_data>();
                            coroutine_function_data->function_name = coroutine_function_name;
                            top.node->children.insert({coroutine_function_name, coroutine_function_data});
                        }
                        else
                        {
                            coroutine_function_data = itr->second;
                        }
                        coroutine_function_data->children_time += top.children_coroutine_time;
                        coroutine_function_data->count++;
                    }
                }
                // for mismatch after error or return before yield
                bool is_tail_call_popped = false;
                while ((!function_data_stack.empty()) &&
                       (function_data_stack.top().function_source != function_source))
                {
                    // std::cout << function_source << "++++++++++++" << function_data_stack.top().function_name << "++++++++++++++" << function_data_stack.top().function_source << std::endl;
                    calculate_time(function_data_stack, begin_time, is_tail_call_popped);
                }

                if (function_data_stack.empty())
                {
                    // std::cout << "**************************" << std::endl;
                    return;
                }
                else
                {
                    assert(function_data_stack.top().function_source == function_source);
                }

                assert(is_tail_call_popped == false);
                // for normal ret
                calculate_time(function_data_stack, begin_time, is_tail_call_popped);
                // for taill call
                while ((!function_data_stack.empty()) && is_tail_call_popped)
                {
                    // std::cout << function_source << "-------------------------" << function_data_stack.top().function_source << std::endl;
                    calculate_time(function_data_stack, begin_time, is_tail_call_popped);
                }
            }
        }
    }
};

static void profile_hooker(lua_State *L, lua_Debug *ar)
{
    auto_time t(L);
    lua_getinfo(L, "Sn", ar);
    t.event = ar->event;
    bool is_c_function = (std::strcmp("C", ar->what) == 0);
    if (is_c_function && (ar->name == nullptr))
    {
        // a internal c function ?
        return;
    }
    t.function_name = fmt::format("{}:{}:{}",
                                  ar->name == nullptr ? "?" : ar->name,
                                  ar->short_src,
                                  ar->linedefined);

    if (is_c_function)
    {
        lua_getinfo(L, "f", ar);
        t.function_source = fmt::format("c:{}",
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

static void print_tree(std::ostream &os, function_time_data_t &root, size_t max_name_length, size_t max_stack)
{

    traverse_tree<sort_t::total_time>(root, max_stack, [&](function_time_data_t &current, size_t current_stack) {
        size_t intent_length = current_stack * per_indent_length;
        std::string indent = current_stack == 0 ? "" : fmt::format(fmt::format("{{:{}}}", intent_length), "");
        size_t intent_name_length = intent_length + current->function_name.length();
        size_t align_length = max_name_length > intent_name_length ? (max_name_length - intent_name_length) : 2;
        std::string align = fmt::format(fmt::format("{{:{}}}", align_length), "");

        os << fmt::format("{}{}{} count:{:<10} total:{:<20} self:{:<16} children:{:<16}",
                          indent,
                          current->function_name,
                          align,
                          current->count,
                          current->total_time.count(),
                          current->self_time.count(),
                          current->children_time.count())
           << std::endl;
    });
}

static void print_list(std::ostream &os, function_time_data_t &root, size_t max_top)
{
    std::unordered_map<std::string, function_time_data> source_map;
    size_t max_function_name_length = 0;
    traverse_tree<sort_t::none>(root, 0, [&](function_time_data_t &current, size_t current_stack) {
        if (current->function_source.empty())
        {
            return;
        }
        auto itr = source_map.find(current->function_source);
        if (itr == source_map.end())
        {
            function_time_data data;
            data.function_name = current->function_name;
            data.function_source = current->function_source;
            source_map.insert({current->function_source, data});
            itr = source_map.find(current->function_source);
            max_function_name_length = std::max(max_function_name_length, current->function_name.length());
        }
        else
        {
            auto &function_name = itr->second.function_name;
            if (function_name != current->function_name && (function_name.find("?:") == 0))
            {
                function_name = current->function_name; // for a better name;
                max_function_name_length = std::max(max_function_name_length, current->function_name.length());
            }
        }
        auto &data = itr->second;
        data.count += current->count;
        data.self_time += current->self_time;
        data.children_time += current->children_time;
        data.total_time += (current->self_time + current->children_time);
    });
    std::vector<function_time_data *> sortable_data;
    sortable_data.reserve(source_map.size());

    for (auto &&i : source_map)
    {
        sortable_data.push_back(&i.second);
    }
    std::sort(sortable_data.begin(), sortable_data.end(), [](function_time_data *l, function_time_data *r) {
        return l->total_time > r->total_time;
    });

    if (max_top > 0 && max_top < sortable_data.size())
    {
        sortable_data.resize(max_top);
    }

    for (auto &&i : sortable_data)
    {
        std::string function_name = fmt::format(fmt::format("{{:{}}}", max_function_name_length + space_after_name), i->function_name);
        os << fmt::format("{} count:{:<10} total:{:<20} self:{:<16} children:{:<16}",
                          function_name,
                          i->count,
                          i->total_time.count(),
                          i->self_time.count(),
                          i->children_time.count())
           << std::endl;
    }
}

static int profile_report_tree(lua_State *L)
{
    size_t max_stack = 0;
    if (lua_gettop(L) > 0 && lua_isinteger(L, 1))
    {
        max_stack = std::abs(lua_tointeger(L, 1));
    }

    auto pd = get_or_new_pd_from_lua(L);
    pd->calculate_root_time(max_stack);
    std::ostringstream os;
    auto max_function_name_length = pd->get_max_function_name_length(max_stack);
    print_tree(os, pd->root, max_function_name_length + space_after_name, max_stack);
    lua_pushstring(L, os.str().c_str());
    return 1;
}

static int profile_report_list(lua_State *L)
{
    size_t max_top = 0;
    if (lua_gettop(L) > 0 && lua_isinteger(L, 1))
    {
        max_top = std::abs(lua_tointeger(L, 1));
    }

    auto pd = get_or_new_pd_from_lua(L);
    std::ostringstream os;
    print_list(os, pd->root, max_top);
    lua_pushstring(L, os.str().c_str());
    return 1;
}

static int profile_report_to_file(lua_State *L)
{
    std::string report_type = luaL_checkstring(L, 1); // tree/list/json

    size_t max_limit = 0; // max stack for tree or max top for list, 0 means no limit
    if (lua_gettop(L) > 1 && lua_isinteger(L, 2))
    {
        max_limit = std::abs(lua_tointeger(L, 2));
    }

    if (report_type == "tree")
    {
        auto pd = get_or_new_pd_from_lua(L);
        pd->calculate_root_time(max_limit);
        std::string file_name = fmt::format("{}.lua_profile_tree.txt", record_clock_t::now().time_since_epoch().count());
        std::ofstream os(file_name);
        auto max_function_name_length = pd->get_max_function_name_length(max_limit);
        print_tree(os, pd->root, max_function_name_length + space_after_name, max_limit);
    }
    else if (report_type == "list")
    {
        auto pd = get_or_new_pd_from_lua(L);
        std::string file_name = fmt::format("{}.lua_profile_list.txt", record_clock_t::now().time_since_epoch().count());
        std::ofstream os(file_name);
        print_list(os, pd->root, max_limit);
    }

    return 0;
}

static int profile_report_info(lua_State *L)
{
    auto pd = get_or_new_pd_from_lua(L);
    std::ostringstream os;
    os << fmt::format(" main_stack_size:{}",
                      pd->main_thread_stack.size());
    size_t id = 0;
    for (auto &&i : pd->coroutine_stack_map)
    {
        os << fmt::format(" thread[{}]_size:{}",
                          id,
                          i.second.size());
        ++id;
    }

    os << std::endl;
    lua_pushstring(L, os.str().c_str());
    return 1;
}

static int profile_start(lua_State *L)
{
    lua_sethook(L, profile_hooker, LUA_MASKCALL | LUA_MASKRET, 0);
    return 0;
}

static int profile_stop(lua_State *L)
{
    lua_sethook(L, nullptr, 0, 0);
    return 0;
}

static int profile_clear(lua_State *L)
{
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, profile_data::reg_key());
    return 0;
}

static int new_lib_profiler(lua_State *L)
{
    lua_newtable(L);
    luaL_Reg lib_funcs[] = {{"start", profile_start},
                            {"stop", profile_stop},
                            {"clear", profile_clear},
                            {"report_tree", profile_report_tree},
                            {"report_list", profile_report_list},
                            {"report_to_file", profile_report_to_file},
                            {"report_info", profile_report_info},
                            {nullptr, nullptr}};
    luaL_setfuncs(L, lib_funcs, 0);
    return 1;
}

int luaopen_profiler(lua_State *L)
{
    luaL_requiref(L, "profiler", new_lib_profiler, 0);
    return 0;
}