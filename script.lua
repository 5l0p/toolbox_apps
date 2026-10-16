-- script.lua: example application for tb_lua.
-- Run with:  ./tb_lua script.lua

ui.title("Notes")

-- Status bar first, so handlers below can update it.
local status = ui.status(0, 260, 480, 20, "Ready")

local count = 0
local counter

ui.menu("File")
ui.item("Open...", function()
    local path = ui.open_file(".")
    if path then
        ui.set(status, "Opened " .. path)
    else
        ui.set(status, "Open cancelled")
    end
end)
ui.item("Save As...", function()
    local path = ui.save_file(".", "untitled.txt")
    if path then ui.set(status, "Saved " .. path) end
end)
ui.item("-")
ui.item("Quit", function()
    local choice = ui.alert(ui.CAUTION, "Quit now?", "Quit", "Cancel")
    if choice == 1 then ui.quit() end
end)

ui.menu("Demo")
ui.item("Say hello", function()
    ui.alert(ui.NOTE, "Hello from Lua!", "OK")
end)

ui.label(16, 40, "Click the button to count.", ui.NAVY)
counter = ui.label(16, 60, "Count: 0", ui.RED)

ui.button(16, 90, 100, 24, "Increment", function()
    count = count + 1
    ui.set(counter, "Count: " .. count)
    ui.set(status, "Clicked " .. count .. " time(s)")
end)

ui.button(124, 90, 100, 24, "Reset", function()
    count = 0
    ui.set(counter, "Count: 0")
    ui.set(status, "Reset")
end)
