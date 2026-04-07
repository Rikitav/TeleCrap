RegisterCommand("roll", function(ctx)
    local maxVal = 100
    
    if ctx.Args ~= "" then
        local num = tonumber(ctx.Args)
        if not num then
            ctx:SendSystemMessage("/roll: неверное число")
            return
        end
        maxVal = math.floor(num)
    end

    if maxVal <= 0 then
        ctx:SendSystemMessage("/roll: max должен быть > 0")
        return
    end

    local rolled = math.random(0, maxVal)
    local line = ctx.Requestor:Name() .. " rolled a " .. tostring(rolled) .. "!"
    ctx:SendSystemMessage(line)
end)