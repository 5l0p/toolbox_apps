-- controlpanel.lua: desktop background and sound settings for tb_lua.
-- View > Desktop sets the X root window colour (xsetroot).
-- View > Sound sets the output volume and mute (wpctl, PipeWire/WirePlumber).
-- Run with:  ./tb_lua controlpanel.lua

ui.title("Control Panel")

local status
local page_handles = { desktop = {}, sound = {} }
local page = nil
local volume_label
local volume_slider, mute_box

local function quote(s)
    return "'" .. (s:gsub("'", "'\\''")) .. "'"
end

-- Run a command and return its whole output (stderr discarded).
local function run(cmd)
    local p = io.popen(cmd .. " 2>/dev/null")
    if not p then return "" end
    local out = p:read("a")
    p:close()
    return out
end

-- ---- desktop ---------------------------------------------------------------

local presets = {
    { "Teal",   "#008080" }, { "Navy",   "#000080" },
    { "Olive",  "#808000" }, { "Maroon", "#800000" },
    { "Purple", "#800080" }, { "Gray",   "#808080" },
    { "Silver", "#c0c0c0" }, { "Black",  "#000000" },
}

local function set_background(color)
    if color == "" then return end
    local ok = os.execute("xsetroot -solid " .. quote(color) .. " 2>/dev/null")
    if ok then
        ui.set(status, "Background set to " .. color)
    else
        ui.set(status, "Not a colour: " .. color)
    end
end

-- ---- sound -----------------------------------------------------------------

local SINK = "@DEFAULT_AUDIO_SINK@"

local function read_sound()
    local out = run("wpctl get-volume " .. SINK)
    local vol = tonumber(out:match("Volume:%s*([%d%.]+)"))
    local muted = out:find("MUTED", 1, true) ~= nil
    return vol and math.floor(vol * 100 + 0.5) or nil, muted
end

local function set_volume(percent)
    os.execute(string.format("wpctl set-volume %s %d%%", SINK, percent))
    ui.set(volume_label, percent .. "%")
    ui.set(status, string.format("Volume %d%%", percent))
end

local function set_mute(muted)
    os.execute(string.format("wpctl set-mute %s %d", SINK, muted and 1 or 0))
    ui.set(status, muted and "Muted" or "Unmuted")
end

local function refresh_sound()
    local vol, muted = read_sound()
    if vol then
        ui.put(volume_slider, vol)
        ui.set(volume_label, vol .. "%")
    else
        ui.set(volume_label, "n/a")
    end
    ui.put(mute_box, muted)
    ui.set(status, vol and "Sound settings read" or "wpctl: no default sink")
end

-- ---- pages -----------------------------------------------------------------

local function show_page(name)
    for pname, handles in pairs(page_handles) do
        for _, h in ipairs(handles) do
            ui.visible(h, pname == name)
        end
    end
    page = name
    ui.menubar_status(name == "desktop" and "Desktop" or "Sound")
    if name == "sound" then refresh_sound() end
end

local function add(page_name, h)
    table.insert(page_handles[page_name], h)
    return h
end

-- Desktop page
add("desktop", ui.label(16, 36, "Background colour", ui.NAVY))
for i, p in ipairs(presets) do
    local col = (i - 1) % 4
    local row = (i - 1) // 4
    local name, hex = p[1], p[2]
    add("desktop", ui.button(16 + col * 116, 60 + row * 36, 100, 24, name,
        function() set_background(hex) end))
end
add("desktop", ui.label(16, 140, "Custom colour (#rrggbb or a name)", ui.NAVY))
local custom = add("desktop", ui.field(16, 160, 300, 22, "#336699", function(text)
    set_background(text)
end))
add("desktop", ui.button(326, 160, 80, 22, "Apply", function()
    set_background(ui.get(custom))
end))

-- Sound page
add("sound", ui.label(16, 36, "Output volume", ui.NAVY))
volume_slider = add("sound", ui.slider(16, 60, 300, 20, 0, 100, 50, function(v)
    set_volume(v)
end))
volume_label = add("sound", ui.label(326, 62, "", ui.NAVY))
mute_box = add("sound", ui.checkbox(16, 100, "Mute", false, function(muted)
    set_mute(muted)
end))

-- ---- menus -----------------------------------------------------------------

ui.menu("File")
ui.item("Quit", function() ui.quit() end)

ui.menu("View")
ui.item("Desktop", function() show_page("desktop") end)
ui.item("Sound", function() show_page("sound") end)

ui.menu("Go")
ui.item("Refresh", function()
    if page == "sound" then refresh_sound() end
end)

status = ui.status(0, 296, 480, 20, "")
show_page("desktop")
