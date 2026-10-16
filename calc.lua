-- calc.lua: four-function calculator for tb_lua.
-- Keys are buttons; the status bar at the bottom is the display.
-- Operations apply left to right, as on a pocket calculator.
-- Run with:  ./tb_lua calc.lua

ui.title("Calculator")

local display
local acc = nil        -- value so far (number), or nil
local op = nil         -- pending operator: "+", "-", "*", "/"
local entry = "0"      -- digits being typed
local fresh = true     -- next digit starts a new number

-- Format a number for the display: whole numbers without a decimal point.
local function fmt(n)
    if n == math.floor(n) and math.abs(n) < 1e15 then
        return string.format("%.0f", n)
    end
    return string.format("%.10g", n)
end

local function show(text)
    ui.set(display, text)
end

local function show_value(n)
    entry = fmt(n)
    show(entry)
end

local function clear()
    acc, op, entry, fresh = nil, nil, "0", true
    show(entry)
end

local function apply(a, o, b)
    if o == "+" then return a + b end
    if o == "-" then return a - b end
    if o == "*" then return a * b end
    if o == "/" then
        if b == 0 then return nil end
        return a / b
    end
end

local function error_state()
    acc, op, entry, fresh = nil, nil, "0", true
    show("Error")
end

local function digit(d)
    if fresh then
        entry = d
        fresh = false
    elseif entry == "0" then
        entry = d
    else
        entry = entry .. d
    end
    show(entry)
end

local function point()
    if fresh then
        entry = "0."
        fresh = false
    elseif not entry:find(".", 1, true) then
        entry = entry .. "."
    end
    show(entry)
end

local function operator(o)
    local b = tonumber(entry)
    if op and not fresh then
        local r = apply(acc, op, b)
        if r == nil then return error_state() end
        acc = r
        show_value(acc)
    else
        acc = b
    end
    op = o
    fresh = true
end

local function equals()
    if not op then return end
    local r = apply(acc, op, tonumber(entry))
    if r == nil then return error_state() end
    acc, op, fresh = r, nil, true
    show_value(acc)
end

local function negate()
    local n = tonumber(entry)
    if n then
        if n ~= 0 then
            show_value(-n)
        end
    end
end

-- ---- layout ----------------------------------------------------------------

display = ui.status(16, 30, 448, 28, "0")

-- Keys: label, column, row, action. Columns 0-3, rows 0-4.
local keys = {
    { "7", 0, 0, function() digit("7") end },
    { "8", 1, 0, function() digit("8") end },
    { "9", 2, 0, function() digit("9") end },
    { "/", 3, 0, function() operator("/") end },
    { "4", 0, 1, function() digit("4") end },
    { "5", 1, 1, function() digit("5") end },
    { "6", 2, 1, function() digit("6") end },
    { "*", 3, 1, function() operator("*") end },
    { "1", 0, 2, function() digit("1") end },
    { "2", 1, 2, function() digit("2") end },
    { "3", 2, 2, function() digit("3") end },
    { "-", 3, 2, function() operator("-") end },
    { "0", 0, 3, function() digit("0") end },
    { ".", 1, 3, point },
    { "+/-", 2, 3, negate },
    { "+", 3, 3, function() operator("+") end },
    { "C", 0, 4, clear },
    { "=", 1, 4, equals },
}

local BW, BH, GAP_X, GAP_Y = 100, 36, 12, 8
for _, k in ipairs(keys) do
    local label, col, row, action = k[1], k[2], k[3], k[4]
    ui.button(16 + col * (BW + GAP_X), 70 + row * (BH + GAP_Y), BW, BH, label, action)
end

ui.menu("File")
ui.item("Clear", clear)
ui.item("-")
ui.item("Quit", function() ui.quit() end)

ui.status(0, 296, 480, 20, "Keys are buttons; Clear resets")
