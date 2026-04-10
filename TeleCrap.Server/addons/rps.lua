-- /rps [камень|ножницы|бумага|rock|paper|scissors] — игра с ботом
local names = {
    rock = "камень",
    paper = "бумага",
    scissors = "ножницы",
    r = "камень",
    p = "бумага",
    s = "ножницы",
    ["камень"] = "камень",
    ["бумага"] = "бумага",
    ["ножницы"] = "ножницы",
    ["к"] = "камень",
    ["н"] = "ножницы",
    ["б"] = "бумага",
}

-- что бьёт какой фигурой (победитель -> проигравший)
local beats = {
    ["камень"] = "ножницы",
    ["ножницы"] = "бумага",
    ["бумага"] = "камень",
}

local all = { "камень", "ножницы", "бумага" }

local function normalize(s)
    s = string.lower(string.gsub(s or "", "^%s*(.-)%s*$", "%1"))
    return names[s]
end

RegisterCommand("rps", function(ctx)
    local userMove = normalize(ctx.Args)
    if not userMove then
        if ctx.Args ~= "" then
            ctx:SendSystemMessage("/rps: непонятный ход. Примеры: камень, ножницы, бумага, r, p, s")
            return
        end
        userMove = all[math.random(1, #all)]
        ctx:SendSystemMessage("🎲 Случайный ход за вас: " .. userMove)
    end

    local bot = all[math.random(1, #all)]
    local a = ctx.Requestor:Name()
    local msg

    if userMove == bot then
        msg = string.format("🤝 %s и бот выбрали «%s» — ничья.", a, userMove)
    elseif beats[userMove] == bot then
        msg = string.format("🏆 %s победил! Вы: «%s», бот: «%s».", a, userMove, bot)
    else
        msg = string.format("💀 Победа бота. Вы: «%s», бот: «%s».", userMove, bot)
    end

    ctx:SendSystemMessage(msg)
end)
