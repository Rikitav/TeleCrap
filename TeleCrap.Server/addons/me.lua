RegisterCommand("me", function(ctx)
    if ctx.Args == "" then
        ctx:SendSystemMessage("/me: введите действие")
        return
    end

    local line = ctx.Requestor:Name() .. " " .. ctx.Args
    ctx:SendSystemMessage(line)
end)