-- du.lua: disk usage browser for tb_lua.
-- Lists the sub-folders of the current folder, largest first.
-- Double-click (or Return) a row to descend into that folder.
-- Run with:  ./tb_lua du.lua

ui.title("Disk Usage")

local cwd = os.getenv("PWD") or "/"
local entries = {}          -- row number -> absolute path
local status, list

-- Single-quote a string for /bin/sh.
local function quote(s)
    return "'" .. (s:gsub("'", "'\\''")) .. "'"
end

-- du -k reports kilobytes; show a readable unit.
local function human(kb)
    if kb >= 1048576 then return string.format("%.1f GB", kb / 1048576) end
    if kb >= 1024 then return string.format("%.1f MB", kb / 1024) end
    return string.format("%d KB", kb)
end

local function scan(dir)
    local p = io.popen("du -k -d1 " .. quote(dir) .. " 2>/dev/null")
    if not p then
        ui.set(status, "Cannot run du")
        return
    end
    local rows, total = {}, nil
    for size, path in p:read("a"):gmatch("(%d+)\t([^\n]+)") do
        if path == dir then
            total = tonumber(size)          -- du prints the folder itself last
        else
            rows[#rows + 1] = { size = tonumber(size), path = path }
        end
    end
    p:close()

    table.sort(rows, function(a, b) return a.size > b.size end)

    ui.list_clear(list)
    entries = {}
    for _, r in ipairs(rows) do
        local name = r.path:match("([^/]+)$") or r.path
        local row = ui.list_add(list, { human(r.size), name })
        entries[row] = r.path
    end

    ui.menubar_status(dir)
    ui.set(status, string.format("%d folders, total %s",
        #rows, total and human(total) or "unknown"))
end

local function go_up()
    if cwd == "/" then return end
    local parent = cwd:match("^(.*)/[^/]+$")
    if not parent or parent == "" then parent = "/" end
    cwd = parent
    scan(cwd)
end

local function go_home()
    cwd = os.getenv("HOME") or cwd
    scan(cwd)
end

local function open_row(row)
    local path = entries[row]
    if path then
        cwd = path
        scan(cwd)
    end
end

ui.menu("File")
ui.item("Refresh", function() scan(cwd) end)
ui.item("-")
ui.item("Quit", function() ui.quit() end)

ui.menu("Go")
ui.item("Up", go_up)
ui.item("Home", go_home)

list = ui.list(0, 21, 480, 275,
    { "Size", "Name" }, { 100, 380 }, open_row)

status = ui.status(0, 296, 480, 20, "Scanning...")

scan(cwd)
