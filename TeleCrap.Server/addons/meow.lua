RegisterCommand("meow", function(ctx)
    if ctx.Args == "" then
        ctx:SendSystemMessage(ctx.Requestor:Name() .. " meowed")
    else
        local target = ctx:FindUserByName(ctx.Args)
        if target then
            local line = ctx.Requestor:Name() .. " meowed at " .. target:Name()
            ctx:SendSystemMessage(line)
        else
            ctx:SendSystemMessage("/meow: пользователь не найден")
        end
    end
end)