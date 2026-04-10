-- /dice [NdM] — бросок костей, по умолчанию 1d6 (N от 1 до 20, граней 2..1000)
RegisterCommand("dice", function(ctx)
    local spec = ctx.Args
    if spec == "" then
        spec = "1d6"
    end
    spec = string.lower(string.gsub(spec, "%s", ""))

    local countStr, sidesStr = string.match(spec, "^(%d+)d(%d+)$")
    if not countStr then
        ctx:SendSystemMessage("/dice: формат NdM, например: /dice 2d6 или /dice 1d20")
        return
    end

    local n = tonumber(countStr)
    local s = tonumber(sidesStr)
    if not n or not s then
        ctx:SendSystemMessage("/dice: не удалось разобрать числа")
        return
    end

    if n < 1 or n > 20 then
        ctx:SendSystemMessage("/dice: количество костей — от 1 до 20")
        return
    end
    if s < 2 or s > 1000 then
        ctx:SendSystemMessage("/dice: граней на кости — от 2 до 1000")
        return
    end

    local sum = 0
    local rolls = {}
    for _ = 1, n do
        local r = math.random(1, s)
        sum = sum + r
        table.insert(rolls, tostring(r))
    end

    local who = ctx.Requestor:Name()
    local detail = table.concat(rolls, " + ")
    ctx:SendSystemMessage(string.format("🎲 %s: %dd%d → [%s] = %d", who, n, s, detail, sum))
end)
