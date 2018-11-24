-- print("dddd")
-- local unpack = table.unpack
-- local b = function(arg)
--     print(unpack(arg))
-- end
-- local a = function(...)
--     local up = {...}
--     local x = function() b(up) end
--     return x
-- end


-- a("hello", "world!", "aha")()
-- d = {1,2,3,4,5,6,7,8,9,0}
-- for c,v in pairs(d) do
--     a(c)()
-- end


-- local rt = {}
-- local proxy = {}
-- local mt = {
--     __index = function(t,k)
--         return rt[k]
--     end,
--     __newindex = function(t,k,v)
--         rt[k]=v
--     end
-- }

-- setmetatable(proxy, mt)
-- proxy.a = "hahaha"

-- for i=1,100,1 do
--     proxy.a = proxy.a .. tostring(i)
-- end
-- a(proxy.a)()

-- local pf = nil
-- pf = function(num)
--     if num < 0 then
--         return
--     end
--     print("before" .. tostring(num))
--     pf(num-1)
--     if num == 2 then
--         error("pferror")
--     end
--     print("after" .. tostring(num))
-- end

-- status, err, ret = xpcall(pf, debug.traceback,5)
-- print(err)


-- local resume = function(...)
--     return coroutine.resume(...)
-- end

-- local yield = function(...)
--     return coroutine.yield(...)
-- end

-- ---- simple co

-- local cofunc = function (a)
--     sleep()
--     local b = yield(a + 1)
--     sleep()
--     local c = yield(b + 1)
--     sleep()
--     local d = yield(c + 1)
--     sleep()
-- 	return d + 1
-- end
-- cof = coroutine.create(cofunc)
-- local s, r = resume(cof, 1)
-- sleep()
-- s, r = resume(cof, r + 1)
-- sleep()
-- s, r = resume(cof, r + 1)
-- sleep()
-- s, r = resume(cof, r + 1)
-- print(r)

-- function foo(a)
-- 	print("foo", a)
--     return yield(2 * a)
-- end

-- local cobodyfunction = function ( a, b )
-- 	print("co-body", a, b)
-- 	local r = foo(a + 1)
-- 	print("co-body", r)
-- 	local r, s = yield(a + b, a - b)
-- 	print("co-body", r, s)
-- 	return b, "end"
-- end

-- co = coroutine.create(cobodyfunction)
-- co2 = coroutine.create(cobodyfunction)

-- print("main", resume(co, 1, 10))
-- print("main", resume(co2, 1, 10))
-- print("main", resume(co, "r"))
-- print("main", resume(co2, "r"))
-- print("main", resume(co, "x", "y"))
-- print("main", resume(co2, "x", "y"))
-- print("main", coroutine.resume(co, "x", "y"))

-- ---- coroutine with error

-- local cobodyerror = function ( a, b )
-- 	print("co-body", a, b)
-- 	local r = foo(a + 1)
-- 	print("co-body", r)
-- 	local r, s = yield(a + b, a - b)
--     print("co-body", r, s)
--     error("opps")
-- 	return b, "end"
-- end

-- co3 = coroutine.create(cobodyerror)

-- print("main", resume(co3, 1, 10))
-- print("main", resume(co3, "r"))
-- print("main", resume(co3, "x", "y"))

-- yield()

-- ---- tail call
-- local tc = nil
-- tc = function(param)
--     if(param < 0) then
--         return 0
--     end
--     print(param)
--     return tc(param - 1)
-- end
-- print(tc(5))

-- ---- another tail call
-- local h = function()
--     print("h")
--     return "h";
-- end

-- local i = function()
--     return h()
-- end

-- local j = function()
--     return i()
-- end

-- local k = function()
--     return j()
-- end

-- local l = function()
--     return k()
-- end

-- print(l())

---- yield and no return

local function cofunc1()
    sleep()
    coroutine.yield()
    sleep()
    coroutine.yield()
    sleep()
end
local will_gc_co = coroutine.create(cofunc1)
coroutine.resume(will_gc_co)
coroutine.resume(will_gc_co)
will_gc_co = nil
collectgarbage()
sleep()
sleep()

