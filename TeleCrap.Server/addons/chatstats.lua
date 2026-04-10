-- /chatstats — краткая сводка по текущему чату
RegisterCommand("chatstats", function(ctx)
    local members = ctx:GetMembers()
    local n = #members
    local kind = ctx:IsDirectChat() and "личный" or "групповой"
    local title = ctx.Chat:Name()
    local cid = ctx.Chat.Id
    local line = string.format(
        "📊 Чат «%s» (%s), id=%d — участников: %d",
        title, kind, cid, n)

    ctx:SendSystemMessage(line)
end)
