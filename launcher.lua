-- launcher.lua: application launcher for tb_lua.
-- Lists executable files and Lua scripts in the current directory.
-- Click an executable to run it. Click a .lua file to run it with ./tb_lua.
-- Run with:  ./tb_lua launcher.lua

ui.title("Launcher")

local cwd = os.getenv("PWD") or "."
local status
local exec_list, lua_list
local exec_entries = {}
local lua_entries = {}

local function quote(s)
    return "'" .. (s:gsub("'", "'\\''")) .. "'"
end

local function is_executable(path)
    local f = io.open(path, "r")
    if not f then return false end
    f:close()
    return os.execute("test -x " .. quote(path)) == 0
end

local function scan()
    local execs = {}
    local luas = {}

    local p = io.popen("ls -1 " .. quote(cwd) .. " 2>/dev/null")
    if not p then
        ui.set(status, "Cannot read directory")
        return
    end

    for name in p:lines() do
        local full = cwd .. "/" .. name
        if name:match("%.lua$") then
            luas[#luas + 1] = name
        elseif is_executable(full) and not name:match("^%.") then
            execs[#execs + 1] = name
        end
    end
    p:close()

    table.sort(execs)
    table.sort(luas)

    ui.list_clear(exec_list)
    exec_entries = {}
    for _, name in ipairs(execs) do
        local row = ui.list_add(exec_list, { name })
        exec_entries[row] = name
    end

    ui.list_clear(lua_list)
    lua_entries = {}
    for _, name in ipairs(luas) do
        local row = ui.list_add(lua_list, { name })
        lua_entries[row] = name
    end

    ui.set(status, string.format("%d executables, %d Lua scripts", #execs, #luas))
end

local function run_exec(row)
    local name = exec_entries[row]
    if name then
        local cmd = "./" .. name
        ui.set(status, "Running " .. cmd .. "...")
        os.execute(cmd .. " &")
    end
end

local function run_lua(row)
    local name = lua_entries[row]
    if name then
        local cmd = "./tb_lua " .. quote(name)
        ui.set(status, "Running " .. cmd .. "...")
        os.execute(cmd .. " &")
    end
end

ui.menu("File")
ui.item("Refresh", scan)
ui.item("-")
ui.item("Quit", function() ui.quit() end)

ui.label(16, 30, "Executables", ui.NAVY)
exec_list = ui.list(16, 50, 448, 110, { "Name" }, { 448 }, run_exec)

ui.label(16, 170, "Lua Scripts", ui.NAVY)
lua_list = ui.list(16, 190, 448, 110, { "Name" }, { 448 }, run_lua)

status = ui.status(0, 316, 480, 20, "Scanning...")

scan()