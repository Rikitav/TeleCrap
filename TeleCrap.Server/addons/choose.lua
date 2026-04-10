-- /choose вариант1 вариант2 ... — случайный выбор из списка (слова через пробел)
RegisterCommand("choose", function(ctx)
    local raw = ctx.Args
    if raw == "" then
        ctx:SendSystemMessage("/choose: перечислите варианты через пробел, например: /choose пицца суши бургер")
        return
    end

    local opts = {}
    for piece in string.gmatch(raw, "%S+") do
        table.insert(opts, piece)
    end

    if #opts == 1 then
        ctx:SendSystemMessage("/choose: нужно хотя бы два варианта")
        return
    end

    local pick = opts[math.random(1, #opts)]
    ctx:SendSystemMessage("🎲 Выбор: " .. pick)
end)
