#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <utility>
#include <unordered_set>

#include <telecrap/Models.h>
#include <telecrap/Protocol.h>
#include <telecrap/Responce.h>
#include <telecrap/Request.h>
#include <telecrap/Transport.h>
#include <telecrap/Console.h>

#include "../include/TerminalUI.h"
#include "../include/MemoryCache.h"

enum class UIMode { Chat, ChatList };

static std::mutex uiMutex;
static Transport* transport = nullptr;

// "Пустой" чат-заглушка, чтобы UI мог рендериться до выбора реального чата.
// Id = SYSTEM_FROMID используется как гарантированно невалидный id пользовательского чата.
static ChatMemory idleChat{ .ChatInfo = { .Id = SYSTEM_FROMID, .Name = std::string_view("no_chat") } };
static std::optional<ChatMemory*> currentChat = &idleChat;
static std::optional<User> currentUser = std::nullopt;

static std::atomic<bool> running{ false };
static std::string inputBuffer;
static int scrollOffset = 0;
static int chatHeight = 0;

// Локальная модель "непрочитанных" сообщений.
// Сервер не хранит read-state: счетчики формируются на клиенте из потока UpdateType::Message
// и сбрасываются при открытии соответствующего чата.
static std::unordered_map<chatid_t, uint32_t> unreadByChat;
static std::unordered_map<chatid_t, std::string> unreadChatLabels;

static UIMode uiMode = UIMode::Chat;
static std::vector<Chat> chatList{};
static int chatListSelected = 0;

static std::vector<std::string> addonCommandsCache{};
static bool addonCommandsLoaded = false;

// Все функции с суффиксом *Unlocked предполагают, что uiMutex уже захвачен.
static void renderUnlocked();
static void addMessageUnlocked(const Message& message);
static void setChatListMode(bool enabled);
static bool selectChatAfterLoad(const Chat& chat);
static bool isLoggedIn() { return currentUser.has_value(); }
static bool isChatting() { return currentChat.has_value(); }

static const char* ansiNotifyStyle() { return "\x1b[1m\x1b[93m"; }
static const char* ansiReset()       { return "\x1b[0m"; }

static const char* ansiFg(Color c)
{
    switch (c)
    {
        case Color::BLACK:          return "\x1b[30m";
        case Color::DARK_CYAN:      return "\x1b[36m";
        case Color::CYAN:           return "\x1b[96m";
        case Color::GREEN:          return "\x1b[32m";
        case Color::DARK_YELLOW:    return "\x1b[33m";
        case Color::YELLOW:         return "\x1b[93m";
        case Color::MAGENTA:        return "\x1b[95m";
        case Color::WHITE:          return "\x1b[37m";
        case Color::DARK_GRAY:      return "\x1b[90m";
        default:                    return "\x1b[37m";
    }
}

static std::string clipUtf8Bytes(std::string_view s, size_t maxBytes)
{
    // Обрезка по байтам, но без разрезания UTF-8 codepoint'а пополам.
    if (s.size() <= maxBytes)
        return std::string(s);

    if (maxBytes <= 3)
        return "...";

    size_t cut = maxBytes - 3;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return std::string(s.substr(0, cut)) + "...";
}

// Для TUI: одна UTF-8 codepoint ≈ одна колонка (кириллица и латиница).
static size_t utf8CountCodepoints(std::string_view s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.size(); )
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if (c < 0x80)
            adv = 1;
        else if ((c & 0xE0) == 0xC0)
            adv = 2;
        else if ((c & 0xF0) == 0xE0)
            adv = 3;
        else if ((c & 0xF8) == 0xF0)
            adv = 4;
        if (i + adv > s.size())
            break;
        i += adv;
        ++n;
    }
    return n;
}

static std::string utf8TakeCodepoints(std::string_view s, size_t maxCp)
{
    if (maxCp == 0)
        return {};

    size_t n = 0;
    size_t end = 0;
    for (size_t i = 0; i < s.size() && n < maxCp; )
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t adv = 1;
        if (c < 0x80)
            adv = 1;
        else if ((c & 0xE0) == 0xC0)
            adv = 2;
        else if ((c & 0xF0) == 0xE0)
            adv = 3;
        else if ((c & 0xF8) == 0xF0)
            adv = 4;
        if (i + adv > s.size())
            break;
        i += adv;
        ++n;
        end = i;
    }
    return std::string(s.substr(0, end));
}

static void utf8PopBack(std::string& s)
{
    // Backspace для UTF-8: отматываем до начала последнего codepoint'а.
    if (s.empty())
        return;

    size_t i = s.size();
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    
    if (i == 0)
        s.clear();
    else
        s.erase(i - 1);
}

static std::string joinTokens(const std::vector<std::string>& tokens, size_t from)
{
    // Склейка аргументов команды обратно в строку (для команд с произвольным текстом).
    std::string s;
    for (size_t i = from; i < tokens.size(); ++i)
    {
        if (i > from)
            s += ' ';

        s += tokens[i];
    }

    return s;
}

static bool startsWithCaseInsensitiveAscii(std::string_view s, std::string_view prefix)
{
    if (prefix.size() > s.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        unsigned char a = static_cast<unsigned char>(s[i]);
        unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (a >= 'A' && a <= 'Z') a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

struct SlashCommandInfo
{
    std::string Name; // без ведущего '/'
    std::string Description;
};

static std::vector<SlashCommandInfo> localSlashCommands()
{
    return {
        { "help", "показать список команд" },
        { "register", "создать аккаунт: /register user pass" },
        { "login", "войти: /login user pass" },
        { "logout", "выйти из аккаунта" },
        { "chats", "открыть список чатов" },
        { "chat", "выбрать чат по имени/ид: /chat <query>" },
        { "members", "показать участников текущего чата" },
        { "create", "создать групповой чат: /create <name>" },
        { "join", "вступить в групповой чат: /join <name>" },
        { "refresh", "перезагрузить историю сообщений" },
        { "clear", "перерисовать экран" },
        { "exit", "выйти из клиента" },
    };
}

static void requestAddonCommandsIntoCache()
{
    if (transport == nullptr || !isLoggedIn())
        return;

    try
    {
        std::vector<Responce> responces = Protocol::SendRequestList(
            *transport,
            Request::CreateGetAddonCommands(transport->AccessToken),
            [](Responce& responce) { return responce.GetAddonCommands.RemainingCommands; });

        std::vector<std::string> fresh;
        fresh.reserve(responces.size());
        for (Responce& r : responces)
            fresh.push_back(std::string(r.GetAddonCommands.CurrentCommand.buffer));

        std::sort(fresh.begin(), fresh.end());
        fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());

        std::lock_guard<std::mutex> lk(uiMutex);
        addonCommandsCache = std::move(fresh);
        addonCommandsLoaded = true;
    }
    catch (...)
    {
        // Не критично для работы UI: подсказка просто будет без серверных команд.
        std::lock_guard<std::mutex> lk(uiMutex);
        addonCommandsLoaded = true;
    }
}

// --- СЕТЬ И СОСТОЯНИЕ ---
static uint32_t totalUnreadCount()
{
    uint32_t n = 0;
    for (const auto& e : unreadByChat)
        n += e.second;

    return n;
}

static void clearUnreadForChat(chatid_t id)
{
    unreadByChat.erase(id);
    unreadChatLabels.erase(id);
}

static void setChatListMode(bool enabled)
{
    uiMode = enabled ? UIMode::ChatList : UIMode::Chat;
    if (chatListSelected < 0)
        chatListSelected = 0;

    if (!chatList.empty() && chatListSelected >= static_cast<int>(chatList.size()))
        chatListSelected = static_cast<int>(chatList.size()) - 1;
}

static ChatMemory* requestChatMemory(const Chat& chat)
{
    try
    {
        std::vector<Responce> messages = Protocol::SendRequestList(
            *transport, Request::CreateGetChatHistory(transport->AccessToken, chat.Id),
            [](Responce& responce) { return responce.GetChatHistory.RemainingMessages; });

        std::vector<Responce> members = Protocol::SendRequestList(
            *transport, Request::CreateGetChatMembers(transport->AccessToken, chat.Id),
            [](Responce& responce) { return responce.GetChatMembers.RemainingUsers; });

        ChatMemory* chatMemory = MemoryCache::createChatMemory(chat);
        for (const Responce& message : messages)
            MemoryCache::storeMessageToChat(chat.Id, message.GetChatHistory.CurrentMessage, false);

        for (const Responce& member : members)
            MemoryCache::addMemberToChat(chat.Id, member.GetChatMembers.CurrentUser);

        chatMemory->MessagesLoaded = true;
        return chatMemory;
    }
    catch (const std::runtime_error&)
    {
        MemoryCache::removeChatMemory(chat.Id);
        throw;
    }
}

static void requestChatListIntoCache()
{
    std::vector<Responce> responces = Protocol::SendRequestList(
        *transport, Request::CreateGetChatList(transport->AccessToken),
        [](Responce& responce) { return responce.GetChatList.RemainingChats; });

    std::vector<Chat> fresh;
    for (Responce& responce : responces)
    {
        const Chat& ch = responce.GetChatList.CurrentChat;
        fresh.push_back(ch);

        if (!MemoryCache::getChatMemory(ch.Id).has_value())
            MemoryCache::createChatMemory(ch);
    }

    std::lock_guard<std::mutex> lk(uiMutex);
    chatList = std::move(fresh);
    if (chatListSelected >= static_cast<int>(chatList.size()))
        chatListSelected = static_cast<int>(chatList.size()) - 1;

    if (chatListSelected < 0)
        chatListSelected = 0;
}

static bool selectChatAfterLoad(const Chat& chat)
{
    try
    {
        std::optional<ChatMemory*> chatMemory = MemoryCache::getChatMemory(chat.Id);
        ChatMemory* cm = (chatMemory.has_value() && chatMemory.value()->MessagesLoaded) 
            ? chatMemory.value()
            : requestChatMemory(chat);

        {
            std::lock_guard<std::mutex> lk(uiMutex);
            currentChat = cm;
            clearUnreadForChat(chat.Id);
        }

        TerminalUI::drawUI();
        return true;
    }
    catch (const disconnected_error&)
    {
        running = false;
        return true;
    }
    catch (const std::runtime_error& err)
    {
        TerminalUI::addMessage(std::string("Ошибка загрузки: ") + err.what());
        TerminalUI::drawUI();
        return false;
    }
}

bool TerminalUI::isRunning() { return running.load(); }
void TerminalUI::stopRunning() { running = false; }

void TerminalUI::hookRender(Transport* transportSocket)
{
    std::lock_guard<std::mutex> lock(uiMutex);

    transport = transportSocket;
    if (transport != nullptr)
        running = true;

    renderUnlocked();
}

void TerminalUI::hookInputChar(char c)
{
    std::lock_guard<std::mutex> lk(uiMutex);
    inputBuffer.push_back(c);
    renderUnlocked();
}

void TerminalUI::hookBackspace()
{
    std::lock_guard<std::mutex> lk(uiMutex);
    if (!inputBuffer.empty())
    {
        utf8PopBack(inputBuffer);
        renderUnlocked();
    }
}

void TerminalUI::hookArrowUp()
{
    std::lock_guard<std::mutex> lk(uiMutex);
    if (uiMode == UIMode::ChatList)
        chatListSelected = (std::max)(0, chatListSelected - 1);
    else
        scrollOffset = (std::min)(scrollOffset + 1, 50);

    renderUnlocked();
}

void TerminalUI::hookArrowDown()
{
    std::lock_guard<std::mutex> lk(uiMutex);
    if (uiMode == UIMode::ChatList)
        chatListSelected = (std::min)(static_cast<int>(chatList.size()) - 1, chatListSelected + 1);
    else
        scrollOffset = (std::max)(scrollOffset - 1, 0);

    renderUnlocked();
}

void TerminalUI::hookEscape()
{
    std::lock_guard<std::mutex> lk(uiMutex);
    if (uiMode == UIMode::ChatList)
    {
        setChatListMode(false);
        renderUnlocked();
        return;
    }

    running = false;
}

void TerminalUI::hookEnter()
{
    if (uiMode == UIMode::ChatList)
    {
        Chat ch{};
        {
            std::lock_guard<std::mutex> lk(uiMutex);
            if (chatList.empty())
                return;

            ch = chatList[static_cast<size_t>(chatListSelected)];
            setChatListMode(false);
        }

        selectChatAfterLoad(ch);
        return;
    }

    std::string line;
    {
        std::lock_guard<std::mutex> lk(uiMutex);
        if (inputBuffer.empty())
            return;

        line = std::move(inputBuffer);
    }

    if (!line.empty() && line[0] == '/')
    {
        setChatListMode(false);
        if (TerminalUI::processCommand(line))
        {
            inputBuffer.clear();
            return;
        }
    }

    chatid_t chatId = 0;
    {
        std::lock_guard<std::mutex> lk(uiMutex);
        if (!isLoggedIn())
        {
            TerminalUI::addMessage("Войдите: /login");
            return;
        }

        if (!isChatting())
        {
            TerminalUI::addMessage("Выберите чат: /chat");
            return;
        }

        chatId = currentChat.value()->ChatInfo.Id;
    }

    Responce responce = Protocol::SendRequest(*transport, Request::CreateCommitMessage(transport->AccessToken, chatId, line));
    if (responce.Type == ResponceType::Error)
    {
        TerminalUI::addMessage("Ошибка! " + std::string(responce.Error.Message.buffer));
        return;
    }

    MemoryCache::storeMessageToChat(currentChat.value()->ChatInfo.Id, responce.CommitMessage.MessageModel, true);
    TerminalUI::addMessage(responce.CommitMessage.MessageModel);
}

void addMessageUnlocked(const Message& message)
{
    const bool viewing = isChatting() && currentChat.value()->ChatInfo.Id == message.DestChat.Id;
    const bool fromSelf = currentUser.has_value() && message.From.Id == currentUser->Id;

    if (message.From.Id != SYSTEM_FROMID && !fromSelf && !viewing)
    {
        unreadByChat[message.DestChat.Id]++;
        unreadChatLabels[message.DestChat.Id] = MemoryCache::getChatMemory(message.DestChat.Id).value()->ChatInfo.Name;
        renderUnlocked();
        return;
    }

    if (viewing)
    {
        scrollOffset = 0;
        renderUnlocked();
    }
}

void renderUnlocked()
{
    int width = 0, height = 0;
    TerminalUI::platformGetScreenSize(width, height);

    chatHeight = height - 5;

    // --- Slash-command hint window ---
    struct HintRow
    {
        std::string Command;     // с ведущим '/'
        std::string Description; // человекочитаемое описание
        bool IsAddon = false;
    };

    std::vector<HintRow> hintLines;
    int hintBoxLines = 0;
    {
        const bool wantHints = (uiMode == UIMode::Chat) && !inputBuffer.empty() && inputBuffer[0] == '/';
        if (wantHints)
        {
            // Берем только имя команды (до первого пробела) без ведущего '/'
            std::string_view sv(inputBuffer);
            sv.remove_prefix(1);
            const size_t sp = sv.find(' ');
            const std::string_view typedCmd = (sp == std::string_view::npos) ? sv : sv.substr(0, sp);

            std::unordered_set<std::string> seen;

            std::vector<HintRow> candidates;
            candidates.reserve(16);

            for (const SlashCommandInfo& c : localSlashCommands())
            {
                if (startsWithCaseInsensitiveAscii(c.Name, typedCmd) && seen.insert(c.Name).second)
                {
                    candidates.push_back(HintRow{
                        .Command = "/" + c.Name,
                        .Description = c.Description,
                        .IsAddon = false,
                    });
                }
            }

            for (const std::string& c : addonCommandsCache)
            {
                if (startsWithCaseInsensitiveAscii(c, typedCmd) && seen.insert(c).second)
                {
                    candidates.push_back(HintRow{
                        .Command = "/" + c,
                        .Description = "addon",
                        .IsAddon = true,
                    });
                }
            }

            constexpr size_t maxHints = 8;
            if (!candidates.empty())
            {
                std::sort(candidates.begin(), candidates.end(), [](const HintRow& a, const HintRow& b)
                    {
                        return a.Command < b.Command;
                    });

                if (candidates.size() > maxHints)
                    candidates.resize(maxHints);

                hintLines = std::move(candidates);
                hintBoxLines = static_cast<int>(hintLines.size()) + 2; // top+bottom
            }
        }
    }

    const int maxMsgRows = (std::max)(0, height - 6 - hintBoxLines);
    const int innerW = (std::max)(4, width - 4);

    std::vector<Message> chatMessages;
    if (isChatting())
        chatMessages = currentChat.value()->Messages;

    const int total = static_cast<int>(chatMessages.size());
    const int startIndex = (std::max)(0, total - maxMsgRows - scrollOffset);

    std::string out;
    out.reserve(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u + 256u);
    out.append("\x1b[?25l\x1b[2J\x1b[H\x1b[40m");

    auto horiz = [width]()
        {
            std::string s;
            s.reserve(static_cast<size_t>(width));
            s.push_back('+');
            s.append(static_cast<size_t>(width - 2), '-');
            s.push_back('+');
            return s;
        };

    const std::string username = isLoggedIn() ? std::string(currentUser->Name.buffer) : "not_logged";
    const std::string chatname = isChatting() ? std::string(currentChat.value()->ChatInfo.Name.buffer) : "no_chat";
    const std::string prompt = username + "@" + chatname + "> ";

    if (uiMode == UIMode::Chat && isLoggedIn() && currentChat.value() == &idleChat && !chatList.empty())
        uiMode = UIMode::ChatList;

    out.append(ansiFg(Color::DARK_CYAN)).append(horiz()).append(ansiReset()).push_back('\n');

    {
        std::string raw = isChatting() ? ("Чат: " + std::string(currentChat.value()->ChatInfo.Name.buffer)) : "TeleCrap";
        raw = clipUtf8Bytes(raw, static_cast<size_t>(innerW));
        out.append("\x1b[90m|\x1b[0m").append(ansiFg(Color::CYAN)).append(raw).append(ansiReset()).append("\x1b[K\33[1000C|\n");
    }

    if (uiMode == UIMode::ChatList)
    {
        const int listCount = static_cast<int>(chatList.size());
        const int visible = maxMsgRows;
        const int top = (std::max)(0, (std::min)(chatListSelected - visible / 2, listCount - visible));

        for (int row = 0; row < maxMsgRows; ++row)
        {
            const int idx = top + row;
            out.append("\x1b[90m|\x1b[0m ");

            if (idx >= listCount)
            {
                out.append("\x1b[K\33[1000C|\n");
                continue;
            }

            const Chat& ch = chatList[static_cast<size_t>(idx)];
            const bool selected = (idx == chatListSelected);
            std::string line = std::string(ch.Name.buffer);
            line = clipUtf8Bytes(line, static_cast<size_t>(innerW - 2));
            const Color chatColor = (ch.Type == ChatType::Direct) ? Color::MAGENTA : Color::CYAN;

            if (selected)
                out.append("\x1b[7m");

            out.append(ansiFg(chatColor)).append("> ").append(line).append(ansiReset());
            if (selected)
                out.append(ansiReset());

            out.append("\x1b[K\33[1000C|\n");
        }
    }
    else
    {
        for (int row = 0; row < maxMsgRows; ++row)
        {
            const int idx = startIndex + row;
            if (idx >= total) { out.append("\x1b[90m|").append(static_cast<size_t>(width - 2), ' ').append("\x1b[0m\33[1000C|\n"); continue; }

            const Message& msg = chatMessages[static_cast<size_t>(idx)];
            std::string t = "[" + TerminalUI::platformGetTimeString(msg.Timestamp) + "] ";
            std::string namePrefix = std::string(msg.From.Name.buffer) + ": ";

            Color timeColor = Color::WHITE;
            Color nameColor = Color::YELLOW;
            Color textColor = Color::WHITE;
            
            if (msg.From.Id == SYSTEM_FROMID)
            {
                timeColor = Color::DARK_YELLOW;
                nameColor = Color::DARK_YELLOW;
                textColor = Color::DARK_YELLOW;
            }
            else if (currentUser.has_value() && msg.From.Id == currentUser->Id)
            {
                timeColor = Color::CYAN;
                nameColor = Color::GREEN;
            }
            else
            {
                timeColor = Color::CYAN;
            }

            std::string body = msg.Text.buffer;
            const size_t prefixLen = t.size() + namePrefix.size();
            size_t innerW_sz = static_cast<size_t>(innerW);
            body = clipUtf8Bytes(body, innerW_sz > prefixLen ? innerW_sz - prefixLen : 0) + " ";

            out.append("\x1b[90m|\x1b[0m ").append(ansiFg(timeColor)).append(t).append(ansiReset());
            if (!namePrefix.empty())
                out.append(ansiFg(nameColor)).append(namePrefix).append(ansiReset());

            out.append(ansiFg(textColor)).append(body).append(ansiReset()).append("\x1b[K\33[1000C|\n");
        }
    }

    if (hintBoxLines > 0)
    {
        const int contentCols = innerW;

        size_t cmdColCp = 0;
        for (const HintRow& r : hintLines)
            cmdColCp = (std::max)(cmdColCp, utf8CountCodepoints(r.Command));

        cmdColCp = min(cmdColCp + 2u, 22u);

        auto hintTopBottom = [&contentCols]()
            {
                std::string s;
                s.push_back('+');
                if (contentCols > 2)
                    s.append(static_cast<size_t>(contentCols - 2), '-');

                if (contentCols >= 2)
                    s.push_back('+');

                return s;
            };

        out.append("\x1b[90m|\x1b[0m ")
            .append(ansiFg(Color::DARK_GRAY))
            .append(hintTopBottom())
            .append(ansiReset())
            .append("\x1b[K\33[1000C|\n");

        for (const HintRow& r : hintLines)
        {
            const size_t cmdW = utf8CountCodepoints(r.Command);
            size_t padCp = (cmdColCp > cmdW) ? (cmdColCp - cmdW) : 1u;

            const int descBudget = contentCols - static_cast<int>(cmdColCp + 1);
            std::string desc = r.Description;
            if (descBudget > 0)
            {
                const size_t maxDescCp = static_cast<size_t>(descBudget);
                if (utf8CountCodepoints(desc) > maxDescCp)
                {
                    const size_t keep = maxDescCp > 3 ? maxDescCp - 3 : maxDescCp;
                    desc = utf8TakeCodepoints(desc, keep) + "...";
                }
            }
            else
            {
                desc.clear();
            }

            out.append("\x1b[90m|\x1b[0m ")
                .append(ansiFg(Color::YELLOW))
                .append(r.Command)
                .append(ansiReset());

            for (size_t p = 0; p < padCp; ++p)
                out.push_back(' ');

            out.append(ansiFg(Color::DARK_GRAY)).append(desc).append(ansiReset()).append("\x1b[K\33[1000C|\n");
        }

        out.append("\x1b[90m|\x1b[0m ")
            .append(ansiFg(Color::DARK_GRAY))
            .append(hintTopBottom())
            .append(ansiReset())
            .append("\x1b[K\33[1000C|\n");
    }

    out.append(ansiFg(Color::DARK_CYAN)).append(horiz()).append(ansiReset()).push_back('\n');

    size_t promptLen = prompt.size();
    std::string displayInput = inputBuffer;
    if (promptLen + 3 > static_cast<size_t>(innerW))
    {
        displayInput.clear();
    }
    else
    {
        const size_t maxIn = innerW - promptLen;
        if (displayInput.size() > maxIn)
            displayInput = "..." + displayInput.substr(displayInput.size() - (maxIn - 3));
    }

    out.append("\x1b[90m|\x1b[0m ").append(ansiFg(Color::GREEN)).append(prompt).append(ansiReset());
    out.append(ansiFg(Color::WHITE)).append(displayInput).append(ansiReset()).append("\x1b[K\33[1000C|\n");
    out.append(ansiFg(Color::DARK_CYAN)).append(horiz()).append(ansiReset()).push_back('\n');

    const uint32_t unreadTotal = totalUnreadCount();
    std::string badgeCore;
    if (unreadTotal > 0)
    {
        badgeCore = "[" + std::to_string(unreadTotal) + "] ";
        if (unreadByChat.size() == 1)
            badgeCore += clipUtf8Bytes(unreadChatLabels[unreadByChat.begin()->first], 28);
        else
            badgeCore += std::to_string(unreadByChat.size()) + " \xD1\x87\xD0\xB0\xD1\x82\xD0\xBE\xD0\xB2";
    }

    {
        std::string left = "| ESC — выход | /help | ";
        if (!badgeCore.empty())
            left = clipUtf8Bytes(left, static_cast<size_t>((std::max)(6, width - static_cast<int>(badgeCore.size()) - 2)));

        out.append(ansiFg(Color::DARK_GRAY)).append(left);
        if (!badgeCore.empty())
        {
            char pos[40]{};
            snprintf(pos, sizeof(pos), "\x1b[%d;%dH", height, (std::max)(2, width - static_cast<int>(badgeCore.size()) + 1));
            out.append(pos).append(ansiNotifyStyle()).append(badgeCore).append(ansiReset());
        }

        out.append("\x1b[K\33[1000C|\n").append(ansiReset());
    }

    char cup[48]{};
    snprintf(cup, sizeof(cup), "\x1b[?25h\x1b[%d;%dH", chatHeight + 3, (std::min)(3 + static_cast<int>(promptLen) + static_cast<int>(displayInput.size()), width));
    out.append(cup);

    TerminalUI::platformWriteStdout(out);
}

void TerminalUI::tryAuth(Transport* transportSocket, std::string& username, std::string& password)
{
    transport = transportSocket;
    Request req = Request::CreateLogin(transport->AccessToken, username, password);
    Responce responce = Protocol::SendRequest(*transport, req);

    if (responce.Type == ResponceType::Error)
    {
        addMessage("Ошибка: " + std::string(responce.Error.Message.buffer));
        drawUI();
        return;
    }

    {
        std::lock_guard<std::mutex> lk(uiMutex);
        currentUser = responce.Auth.UserModel;
        currentChat = &idleChat;
    }

    addMessage("Добро пожаловать, " + username + "!");
    try
    {
        requestChatListIntoCache();
    }
    catch (...) {}

    requestAddonCommandsIntoCache();
    try
    {
        std::lock_guard<std::mutex> lk(uiMutex);
        if (!chatList.empty())
            setChatListMode(true);
    }
    catch (...) {}

    drawUI();
    return;
}

void TerminalUI::drawUI()
{
    std::lock_guard<std::mutex> lock(uiMutex);
    renderUnlocked();
}

void TerminalUI::addMessage(const Message& message)
{
    std::lock_guard<std::mutex> lock(uiMutex);
    addMessageUnlocked(message);
}

void TerminalUI::addMessage(const std::string& content)
{
    std::lock_guard<std::mutex> lock(uiMutex);
    Message message{};
    message.From.Id = SYSTEM_FROMID;
    message.Text = content.c_str();

    if (currentChat.value() == &idleChat)
    {
        message.DestChat = idleChat.ChatInfo;
        idleChat.Messages.push_back(message);
    }
    else
    {
        message.DestChat = currentChat.value()->ChatInfo;
        MemoryCache::appendMessageToChat(message.DestChat.Id, message);
    }

    addMessageUnlocked(message);
}

void TerminalUI::onChatRenamed(const Chat& chat)
{
    addMessage("Чат переименоан в " + std::string(chat.Name));
    drawUI();
}

void TerminalUI::onUserKickedFromChat(const Chat& chat, const User& target)
{
    addMessage("Пользователь @" + std::string(target.Name) + " исключен из чата");
    drawUI();
}

void TerminalUI::onUserBannedFromChat(const Chat& chat, const User& target)
{
    addMessage("Пользователь @" + std::string(target.Name) + " забанен в чате");
    drawUI();
}

void TerminalUI::onUserUnbannedFromChat(const Chat& chat, const User& target)
{
    addMessage("Пользователь @" + std::string(target.Name) + " разбанен в чате");
    drawUI();
}

bool TerminalUI::processCommand(const std::string& command)
{
    if (command.empty())
        return true;

    std::vector<std::string> tokens;
    std::stringstream ss(command);
    std::string token;
    while (ss >> token) tokens.push_back(token);

    if (tokens[0] == "/help")
    {
        addMessage("Доступные команды: /register, /login, /logout, /chat, /chats, /members, /create, /join, /refresh, /clear, /exit");
        drawUI();
        return true;
    }

    if (tokens[0] == "/register" || tokens[0] == "/login")
    {
        if (tokens.size() < 3) { addMessage("Использование: " + tokens[0] + " [username] [password]"); drawUI(); return false; }
        std::string user = tokens[1], pass = tokens.size() >= 3 ? tokens[2] : "";

        Request req = (tokens[0] == "/register") ? Request::CreateRegister(transport->AccessToken, user, pass) : Request::CreateLogin(transport->AccessToken, user, pass);
        Responce responce = Protocol::SendRequest(*transport, req);
        
        if (responce.Type == ResponceType::Error) { addMessage("Ошибка: " + std::string(responce.Error.Message.buffer)); drawUI(); return false; }

        { std::lock_guard<std::mutex> lk(uiMutex); currentUser = responce.Auth.UserModel; currentChat = &idleChat; }
        addMessage("Добро пожаловать, " + user + "!");
        try { requestChatListIntoCache(); } catch (...) {}
        requestAddonCommandsIntoCache();
        try { std::lock_guard<std::mutex> lk(uiMutex); if (!chatList.empty()) setChatListMode(true); } catch (...) {}
        drawUI();
        return true;
    }

    if (tokens[0] == "/logout")
    {
        if (!isLoggedIn()) { addMessage("Неавторизован!"); drawUI(); return false; }
        Protocol::SendRequest(*transport, Request::CreateLogin(transport->AccessToken, "", ""));
        { std::lock_guard<std::mutex> lk(uiMutex); currentUser = std::nullopt; currentChat = &idleChat; chatList.clear(); chatListSelected = 0; setChatListMode(false); addonCommandsCache.clear(); addonCommandsLoaded = false; }
        drawUI();
        return true;
    }

    if (tokens[0] == "/chats")
    {
        if (!isLoggedIn()) { addMessage("Неавторизован!"); drawUI(); return false; }
        try { requestChatListIntoCache(); } catch (const std::runtime_error& err) { addMessage(std::string("Ошибка: ") + err.what()); drawUI(); return false; }
        if (!addonCommandsLoaded) requestAddonCommandsIntoCache();
        std::lock_guard<std::mutex> lk(uiMutex); setChatListMode(true); drawUI(); return true;
    }

    if (tokens[0] == "/chat")
    {
        if (!isLoggedIn()) { addMessage("Неавторизован!"); drawUI(); return false; }
        if (tokens.size() < 2) { addMessage("Использование: /chat [chat_name]"); drawUI(); return false; }
        Responce responce = Protocol::SendRequest(*transport, Request::CreateGetChatInfo(transport->AccessToken, tokens[1]));
        if (responce.Type == ResponceType::Error) { addMessage("Ошибка: " + std::string(responce.Error.Message.buffer)); drawUI(); return false; }
        return selectChatAfterLoad(responce.GetChatInfo.ChatModel);
    }

    if (tokens[0] == "/members")
    {
        if (!isLoggedIn() || !isChatting()) { addMessage("Войдите и выберите чат"); drawUI(); return false; }
        for (const User& member : currentChat.value()->Members) addMessage(member.Name.buffer);
        drawUI(); return true;
    }

    if (tokens[0] == "/create" || tokens[0] == "/join")
    {
        if (!isLoggedIn()) { addMessage("Неавторизован!"); drawUI(); return false; }
        if (tokens.size() < 2) { addMessage("Использование: " + tokens[0] + " [название]"); drawUI(); return false; }
        const std::string chatQuery = joinTokens(tokens, 1);
        Request req = (tokens[0] == "/create") ? Request::CreateSpawnGroupChat(transport->AccessToken, chatQuery) : Request::CreateJoinGroupChat(transport->AccessToken, chatQuery);
        Responce responce = Protocol::SendRequest(*transport, req);
        if (responce.Type == ResponceType::Error) { addMessage(std::string(responce.Error.Message.buffer)); drawUI(); return false; }
        return selectChatAfterLoad((tokens[0] == "/create") ? responce.CreateChat.ChatModel : responce.JoinChat.ChatModel);
    }

    if (tokens[0] == "/refresh")
    {
        if (!isLoggedIn() || !isChatting())
        {
            addMessage("Войдите и выберите чат");
            drawUI();
            return false;
        }

        try
        {
            std::vector<Responce> responces = Protocol::SendRequestList(*transport, Request::CreateGetChatHistory(transport->AccessToken, currentChat.value()->ChatInfo.Id), [](Responce& responce) { return responce.GetChatHistory.RemainingMessages; });
            ChatMemory* chat = currentChat.value();
            {
                std::lock_guard<std::mutex> lk(uiMutex);
                chat->Messages.clear();
                for (Responce& responce : responces)
                {
                    MemoryCache::storeMessageToChat(chat->ChatInfo.Id, responce.GetChatHistory.CurrentMessage, false);
                    addMessageUnlocked(responce.GetChatHistory.CurrentMessage);
                }

                chat->MessagesLoaded = true;
            }

            drawUI();
            return true;
        }
        catch (const std::runtime_error& err) 
        {
            addMessage("Ошибка обновления: " + std::string(err.what()));
            drawUI();
            return false;
        }
    }

    if (tokens[0] == "/clear")
    {
        drawUI();
        return true;
    }

    if (tokens[0] == "/exit")
    {
        running = false;
        return true;
    }

    return false;
}