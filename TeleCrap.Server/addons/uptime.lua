-- /uptime — время с момента загрузки Lua-аддонов на сервере (не ОС)
if not state.has("addons_boot_ms") then
    state.set("addons_boot_ms", UnixTimeMs())
end

local function format_duration(ms)
    if ms < 0 then
        ms = 0
    end
    local sec = math.floor(ms / 1000)
    local days = math.floor(sec / 86400)
    sec = sec % 86400
    local hours = math.floor(sec / 3600)
    sec = sec % 3600
    local mins = math.floor(sec / 60)
    local secs = sec % 60
    local parts = {}
    if days > 0 then
        table.insert(parts, tostring(days) .. "д")
    end
    if hours > 0 or days > 0 then
        table.insert(parts, tostring(hours) .. "ч")
    end
    if mins > 0 or hours > 0 or days > 0 then
        table.insert(parts, tostring(mins) .. "м")
    end
    table.insert(parts, tostring(secs) .. "с")
    return table.concat(parts, " ")
end

RegisterCommand("uptime", function(ctx)
    local boot = state.get("addons_boot_ms")
    local boot_ms = tonumber(boot)
    if not boot_ms then
        boot_ms = UnixTimeMs()
        state.set("addons_boot_ms", boot_ms)
    end
    local elapsed = UnixTimeMs() - boot_ms
    ctx:SendSystemMessage("⏱ Аддоны Lua работают уже: " .. format_duration(elapsed))
end)
