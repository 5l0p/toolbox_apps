-- sysinfo.lua: system information viewer for tb_lua.
-- View menu switches between lspci, lsusb, lscpu, swap, df and mount.
-- Double-click a device in lspci or lsusb to see its full details.
-- Go > Back returns from a details view. Run with:  ./tb_lua sysinfo.lua

ui.title("System Info")

local list, status
local current        -- the list view being shown
local detail_from    -- list view to return to from a details view
local reload         -- re-runs whatever is on screen (Go > Refresh)
local entries = {}   -- row number -> details spec {title=, cmd=}

local function quote(s)
    return "'" .. (s:gsub("'", "'\\''")) .. "'"
end

local function trim(s)
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Run a command and return its output lines (stderr discarded).
local function run(cmd)
    local p = io.popen(cmd .. " 2>/dev/null")
    if not p then return {} end
    local lines = {}
    for line in p:lines() do lines[#lines + 1] = line end
    p:close()
    return lines
end

-- Details output. With `header`, the first line is a device header (lsusb -v);
-- the rest are "Key: value" lines, or aligned "key   value" lines.
local function details_rows(lines, header)
    local rows = {}
    local first = 1
    if header and lines[1] then
        rows[1] = { "Device", trim(lines[1]) }
        first = 2
    end
    for i = first, #lines do
        local l = lines[i]
        if l:match("%S") then
            local k, v = l:match("^%s*([^:]-)%s*:%s*(.-)%s*$")
            if not k then
                -- aligned "key    value" lines (lsusb -v)
                k, v = l:match("^%s*(.-)%s%s+(.-)%s*$")
                if not k then k, v = trim(l), "" end
            end
            rows[#rows + 1] = { k, v }
        end
    end
    return rows
end

-- ---- list views ------------------------------------------------------------
-- Each view: cols, widths, parse(lines) -> { {cells={...}, details=spec?}, ... }

local views = {}

-- `lspci -mm`: slot, then quoted class, vendor, device.
views.lspci = {
    title = "lspci",
    cols = { "Slot", "Class", "Device" }, widths = { 80, 150, 250 },
    cmd = "lspci -mm",
    parse = function(lines)
        local rows = {}
        for _, l in ipairs(lines) do
            local slot = l:match("^(%S+)")
            if slot then
                local q = {}
                for f in l:gmatch('"([^"]*)"') do q[#q + 1] = f end
                local class, vendor, device = q[1] or "", q[2] or "", q[3] or ""
                rows[#rows + 1] = {
                    cells = { slot, class, trim(vendor .. " " .. device) },
                    details = { title = "lspci " .. slot, header = false,
                                cmd = "lspci -vmm -s " .. quote(slot) },
                }
            end
        end
        return rows
    end,
}

views.lsusb = {
    title = "lsusb",
    cols = { "Bus/Dev", "ID", "Device" }, widths = { 80, 100, 300 },
    parse = function(lines)
        local rows = {}
        for _, l in ipairs(lines) do
            local bus, dev, id, desc = l:match("^Bus (%d+) Device (%d+): ID (%x+:%x+)%s*(.*)$")
            if bus then
                rows[#rows + 1] = {
                    cells = { bus .. "/" .. dev, id, desc },
                    details = { title = "lsusb " .. bus .. "/" .. dev, header = true,
                                cmd = "lsusb -v -s " .. bus .. ":" .. dev },
                }
            end
        end
        return rows
    end,
}

views.lscpu = {
    title = "lscpu",
    cols = { "Property", "Value" }, widths = { 200, 280 },
    parse = function(lines)
        local rows = {}
        for _, l in ipairs(lines) do
            local k, v = l:match("^([^:]+):%s*(.-)%s*$")
            if k then rows[#rows + 1] = { cells = { trim(k), v } } end
        end
        return rows
    end,
}

views.swap = {
    title = "swap",
    cols = { "Kind", "Total MB", "Used MB", "Free MB" }, widths = { 100, 120, 120, 140 },
    parse = function(lines)
        -- `free -m`: "Mem:" and "Swap:" rows; the first three numbers are
        -- total, used and free in both.
        local rows = {}
        for _, l in ipairs(lines) do
            local kind, total, used, free = l:match("^(%a+):%s+(%d+)%s+(%d+)%s+(%d+)")
            if kind then rows[#rows + 1] = { cells = { kind, total, used, free } } end
        end
        return rows
    end,
    cmd = "free -m",
}

views.df = {
    title = "df",
    cols = { "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on" },
    widths = { 140, 60, 60, 60, 50, 110 },
    parse = function(lines)
        local rows = {}
        for i, l in ipairs(lines) do
            if i > 1 then   -- skip the header line
                local fs, size, used, avail, pct, mount =
                    l:match("^(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(.+)$")
                if fs then rows[#rows + 1] = { cells = { fs, size, used, avail, pct, mount } } end
            end
        end
        return rows
    end,
    cmd = "df -P -h",
}

-- `findmnt -P`: one line per mount, as KEY="value" pairs.
views.mount = {
    title = "mount",
    cols = { "Source", "Mount point", "Type", "Options" },
    widths = { 130, 120, 60, 170 },
    cmd = "findmnt -P -o SOURCE,TARGET,FSTYPE,OPTIONS",
    parse = function(lines)
        local rows = {}
        for _, l in ipairs(lines) do
            local f = {}
            for k, v in l:gmatch('(%u+)="([^"]*)"') do f[k] = v end
            if f.TARGET then
                rows[#rows + 1] = { cells = { f.SOURCE or "", f.TARGET, f.FSTYPE or "", f.OPTIONS or "" } }
            end
        end
        return rows
    end,
}

-- Command run for a list view: `cmd` if set, otherwise the view's title.
local function view_cmd(v)
    return v.cmd or v.title
end

-- ---- display ---------------------------------------------------------------

local function show_list(view)
    current = view
    detail_from = nil
    reload = function() show_list(view) end
    local lines = run(view_cmd(view))
    local rows = view.parse(lines)

    ui.list_columns(list, view.cols, view.widths)
    entries = {}
    for _, r in ipairs(rows) do
        local n = ui.list_add(list, r.cells)
        entries[n] = r.details
    end
    ui.menubar_status(view.title)
    ui.set(status, string.format("%d entries", #rows))
end

local function show_details(spec, from)
    reload = function() show_details(spec, from) end
    local lines = run(spec.cmd)
    local rows = details_rows(lines, spec.header)

    ui.list_columns(list, { "Property", "Value" }, { 160, 320 })
    entries = {}
    for _, r in ipairs(rows) do ui.list_add(list, r) end
    detail_from = from
    current = nil
    ui.menubar_status(spec.title)
    ui.set(status, string.format("%d lines", #rows))
end

local function open_row(row)
    local spec = entries[row]
    if spec then show_details(spec, current) end
end

local function show(view)
    return function() show_list(view) end
end

local function back()
    if detail_from then show_list(detail_from) end
end

local function refresh()
    if reload then reload() end
end

-- ---- menus -----------------------------------------------------------------

ui.menu("File")
ui.item("Quit", function() ui.quit() end)

ui.menu("View")
ui.item("lspci", show(views.lspci))
ui.item("lsusb", show(views.lsusb))
ui.item("lscpu", show(views.lscpu))
ui.item("swap", show(views.swap))
ui.item("df", show(views.df))
ui.item("mount", show(views.mount))

ui.menu("Go")
ui.item("Back", back)
ui.item("Refresh", refresh)

-- ---- window ----------------------------------------------------------------

list = ui.list(0, 21, 480, 275,
    { "Slot", "Device" }, { 90, 390 }, open_row)
status = ui.status(0, 296, 480, 20, "")

show_list(views.lspci)
