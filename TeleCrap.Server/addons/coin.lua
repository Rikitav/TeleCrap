-- /coin — подбросить монетку
RegisterCommand("coin", function(ctx)
    local side = (math.random(1, 2) == 1) and "Орёл" or "Решка"
    ctx:SendSystemMessage(ctx.Requestor:Name() .. " подбрасывает монетку… " .. side .. "!")
end)
