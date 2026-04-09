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

#define ignore catch (...) {}
#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))

enum class UiMode
{
    Chat,
    ChatList
};

// "Пустой" чат-заглушка, чтобы UI мог рендериться до выбора реального чата.
// Id = SYSTEM_FROMID используется как гарантированно невалидный идентификатор пользовательского чата.
static ChatMemory idleChatPlaceholder
{
    .ChatInfo =
    {
        .Id = SYSTEM_FROMID,
        .Name = std::string_view("no_chat")
    }
};

static std::recursive_mutex interfaceMutex;
static Transport* activeTransport = nullptr;

static std::optional<ChatMemory*> currentActiveChat = &idleChatPlaceholder;
static std::optional<User> currentLoggedInUser = std::nullopt;

static std::atomic<bool> isApplicationRunning{ false };
static std::string userInputBuffer;
static int messageScrollOffset = 0;
static int calculatedChatHeight = 0;

// Локальная модель "непрочитанных" сообщений.
// Сервер не хранит состояние прочтения: счетчики формируются на клиенте 
// из потока UpdateType::Message и сбрасываются при открытии соответствующего чата.
static std::unordered_map<chatid_t, uint32_t> unreadMessageCountByChatId;
static std::unordered_map<chatid_t, std::string> unreadChatNamesByChatId;

static UiMode currentUiMode = UiMode::Chat;
static std::vector<Chat> cachedChatList{};
static int selectedChatListIndex = 0;

static std::vector<std::string> cachedAddonCommands{};
static bool areAddonCommandsLoaded = false;

// Все функции с суффиксом *Unlocked предполагают, что interfaceMutex уже захвачен.
static void renderInterfaceUnlocked();
static void appendMessageToChatUnlocked(const Message& incomingMessage);
static void toggleChatListMode(bool isEnabled);
static bool loadAndSelectChat(const Chat& targetChat);
static bool isUserLoggedIn();
static bool isUserInActiveChat();

static const char* getAnsiNotificationStyle()
{
    return "\x1b[1m\x1b[93m";
}

static const char* getAnsiResetStyle()
{
    return "\x1b[0m";
}

// Преобразует перечисление Color в ANSI-код цвета текста.
static const char* getAnsiColorForeground(Color targetColor)
{
    switch (targetColor)
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

// Обрезает строку по количеству байт, не разрывая UTF-8 символы посередине.
static std::string truncateUtf8StringByBytes(std::string_view originalString, size_t maxAllowedBytes)
{
    if (originalString.size() <= maxAllowedBytes)
        return std::string(originalString);

    if (maxAllowedBytes <= 3)
        return "...";

    // Смещаем позицию обрезки влево, чтобы не разрезать UTF-8 символ.
    size_t cutPosition = maxAllowedBytes - 3;
    while (cutPosition > 0 && (static_cast<unsigned char>(originalString[cutPosition]) & 0xC0) == 0x80)
        --cutPosition;

    return std::string(originalString.substr(0, cutPosition)) + "...";
}

// Подсчитывает количество символов (кодовых точек) в UTF-8 строке.
// Для терминала одна кодовая точка обычно занимает одну колонку.
static size_t countUtf8Codepoints(std::string_view targetString)
{
    size_t codepointCount = 0;
    for (size_t byteIndex = 0; byteIndex < targetString.size(); )
    {
        const unsigned char currentByte = static_cast<unsigned char>(targetString[byteIndex]);
        size_t byteAdvanceAmount = 1;

        if (currentByte < 0x80)                 byteAdvanceAmount = 1;
        else if ((currentByte & 0xE0) == 0xC0)  byteAdvanceAmount = 2;
        else if ((currentByte & 0xF0) == 0xE0)  byteAdvanceAmount = 3;
        else if ((currentByte & 0xF8) == 0xF0)  byteAdvanceAmount = 4;

        if (byteIndex + byteAdvanceAmount > targetString.size())
            break;

        byteIndex += byteAdvanceAmount;
        ++codepointCount;
    }

    return codepointCount;
}

// Возвращает строку, содержащую указанное максимальное количество кодовых точек UTF-8.
static std::string extractMaxUtf8Codepoints(std::string_view originalString, size_t maxCodepoints)
{
    if (maxCodepoints == 0)
        return {};

    size_t codepointCount = 0;
    size_t endByteIndex = 0;

    for (size_t byteIndex = 0; byteIndex < originalString.size() && codepointCount < maxCodepoints; )
    {
        const unsigned char currentByte = static_cast<unsigned char>(originalString[byteIndex]);
        size_t byteAdvanceAmount = 1;

        if (currentByte < 0x80)                 byteAdvanceAmount = 1;
        else if ((currentByte & 0xE0) == 0xC0)  byteAdvanceAmount = 2;
        else if ((currentByte & 0xF0) == 0xE0)  byteAdvanceAmount = 3;
        else if ((currentByte & 0xF8) == 0xF0)  byteAdvanceAmount = 4;

        if (byteIndex + byteAdvanceAmount > originalString.size())
            break;

        byteIndex += byteAdvanceAmount;
        ++codepointCount;
        endByteIndex = byteIndex;
    }

    return std::string(originalString.substr(0, endByteIndex));
}

// Удаляет последний UTF-8 символ из строки (аналог Backspace).
static void removeLastUtf8Codepoint(std::string& targetString)
{
    if (targetString.empty())
        return;

    size_t cutPosition = targetString.size();
    while (cutPosition > 0 && (static_cast<unsigned char>(targetString[cutPosition - 1]) & 0xC0) == 0x80)
        --cutPosition;

    if (cutPosition == 0)
    {
        targetString.clear();
    }
    else
    {
        targetString.erase(cutPosition - 1);
    }
}

// Склеивает токены команды обратно в единую строку, начиная с указанного индекса.
static std::string joinStringTokens(const std::vector<std::string>& commandTokens, size_t startingIndex)
{
    std::string resultString;
    for (size_t tokenIndex = startingIndex; tokenIndex < commandTokens.size(); ++tokenIndex)
    {
        if (tokenIndex > startingIndex)
            resultString += ' ';

        resultString += commandTokens[tokenIndex];
    }

    return resultString;
}

// Регистронезависимая проверка на то, начинается ли строка с заданного префикса.
static bool checkStringStartsWithCaseInsensitive(std::string_view targetString, std::string_view expectedPrefix)
{
    if (expectedPrefix.size() > targetString.size())
        return false;

    for (size_t characterIndex = 0; characterIndex < expectedPrefix.size(); ++characterIndex)
    {
        unsigned char stringChar = static_cast<unsigned char>(targetString[characterIndex]);
        unsigned char prefixChar = static_cast<unsigned char>(expectedPrefix[characterIndex]);

        if (stringChar >= 'A' && stringChar <= 'Z')
            stringChar = static_cast<unsigned char>(stringChar - 'A' + 'a');

        if (prefixChar >= 'A' && prefixChar <= 'Z')
            prefixChar = static_cast<unsigned char>(prefixChar - 'A' + 'a');

        if (stringChar != prefixChar)
            return false;
    }

    return true;
}

struct SlashCommandMetadata
{
    std::string Name;
    std::string Description;
};

// Возвращает список встроенных клиентских команд.
static std::vector<SlashCommandMetadata> getLocalSlashCommandsList()
{
    return
    {
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

// Запрашивает команды расширений с сервера и кеширует их.
static void fetchAndCacheAddonCommands()
{
    if (activeTransport == nullptr || !isUserLoggedIn())
        return;

    try
    {
        Request addonCommandsRequest = Request::CreateGetAddonCommands(activeTransport->AccessToken);
        std::vector<Responce> serverResponses = Protocol::SendRequestList(*activeTransport, addonCommandsRequest,
            [](Responce& response) { return response.GetAddonCommands.RemainingCommands; });

        std::vector<std::string> fetchedCommands;
        fetchedCommands.reserve(serverResponses.size());

        for (Responce& currentResponse : serverResponses)
            fetchedCommands.push_back(std::string(currentResponse.GetAddonCommands.CurrentCommand.buffer));

        std::sort(fetchedCommands.begin(), fetchedCommands.end());
        fetchedCommands.erase(std::unique(fetchedCommands.begin(), fetchedCommands.end()), fetchedCommands.end());

        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

        cachedAddonCommands = std::move(fetchedCommands);
        areAddonCommandsLoaded = true;
    }
    catch (...)
    {
        // В случае ошибки просто не будем отображать серверные команды в подсказках.
        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
        areAddonCommandsLoaded = true;
    }
}

// Возвращает общее количество непрочитанных сообщений во всех чатах.
static uint32_t calculateTotalUnreadMessages()
{
    uint32_t totalMessages = 0;
    for (const auto& chatEntry : unreadMessageCountByChatId)
        totalMessages += chatEntry.second;

    return totalMessages;
}

// Сбрасывает счетчик непрочитанных сообщений для указанного чата.
static void clearUnreadMessagesForChatId(chatid_t targetChatId)
{
    unreadMessageCountByChatId.erase(targetChatId);
    unreadChatNamesByChatId.erase(targetChatId);
}

// Переключает режим отображения интерфейса между чатом и списком чатов.
static void toggleChatListMode(bool isEnabled)
{
    currentUiMode = isEnabled ? UiMode::ChatList : UiMode::Chat;
    if (selectedChatListIndex < 0)
        selectedChatListIndex = 0;

    if (!cachedChatList.empty() && selectedChatListIndex >= static_cast<int>(cachedChatList.size()))
        selectedChatListIndex = static_cast<int>(cachedChatList.size()) - 1;
}

static bool isUserLoggedIn()
{
    return currentLoggedInUser.has_value();
}

static bool isUserInActiveChat()
{
    return currentActiveChat.has_value();
}

// Запрашивает историю сообщений и участников для указанного чата.
static ChatMemory* fetchChatHistoryAndMembers(const Chat& targetChat)
{
    try
    {
        Request historyRequest = Request::CreateGetChatHistory(activeTransport->AccessToken, targetChat.Id);
        std::vector<Responce> messageResponses = Protocol::SendRequestList(*activeTransport, historyRequest,
            [](Responce& response) { return response.GetChatHistory.RemainingMessages; });

        Request membersRequest = Request::CreateGetChatMembers(activeTransport->AccessToken, targetChat.Id);
        std::vector<Responce> memberResponses = Protocol::SendRequestList(*activeTransport, membersRequest,
            [](Responce& response) { return response.GetChatMembers.RemainingUsers; });

        ChatMemory* allocatedChatMemory = MemoryCache::createChatMemory(targetChat);
        for (const Responce& messageResponse : messageResponses)
            MemoryCache::storeMessageToChat(targetChat.Id, messageResponse.GetChatHistory.CurrentMessage, false);

        for (const Responce& memberResponse : memberResponses)
            MemoryCache::addMemberToChat(targetChat.Id, memberResponse.GetChatMembers.CurrentUser);

        allocatedChatMemory->MessagesLoaded = true;
        return allocatedChatMemory;
    }
    catch (const std::runtime_error&)
    {
        MemoryCache::removeChatMemory(targetChat.Id);
        throw;
    }
}

// Обновляет локальный кэш списка доступных пользователю чатов.
static void fetchAndCacheChatList()
{
    try
    {
        Request chatListRequest = Request::CreateGetChatList(activeTransport->AccessToken);
        std::vector<Responce> serverResponses = Protocol::SendRequestList(*activeTransport, chatListRequest,
            [](Responce& response) { return response.GetChatList.RemainingChats; });

        std::vector<Chat> fetchedChats;
        for (Responce& responseData : serverResponses)
        {
            const Chat& currentChatModel = responseData.GetChatList.CurrentChat;
            fetchedChats.push_back(currentChatModel);

            if (!MemoryCache::getChatMemory(currentChatModel.Id).has_value())
                MemoryCache::createChatMemory(currentChatModel);
        }

        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
        cachedChatList = std::move(fetchedChats);

        if (selectedChatListIndex >= static_cast<int>(cachedChatList.size()))
            selectedChatListIndex = static_cast<int>(cachedChatList.size()) - 1;

        if (selectedChatListIndex < 0)
            selectedChatListIndex = 0;
    }
    ignore
}

// Загружает данные чата и устанавливает его как активный.
static bool loadAndSelectChat(const Chat& targetChat)
{
    try
    {
        std::optional<ChatMemory*> cachedMemory = MemoryCache::getChatMemory(targetChat.Id);

        ChatMemory* targetChatMemory = (cachedMemory.has_value() && cachedMemory.value()->MessagesLoaded)
            ? cachedMemory.value()
            : fetchChatHistoryAndMembers(targetChat);

        {
            std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
            currentActiveChat = targetChatMemory;
            clearUnreadMessagesForChatId(targetChat.Id);
        }

        TerminalUI::drawUI();
        return true;
    }
    catch (const disconnected_error&)
    {
        isApplicationRunning = false;
        return true;
    }
    catch (const std::runtime_error& executionError)
    {
        TerminalUI::addMessage(std::string("Ошибка загрузки: ") + executionError.what());
        TerminalUI::drawUI();
        return false;
    }
}

bool TerminalUI::isRunning()
{
    return isApplicationRunning.load();
}

void TerminalUI::stopRunning()
{
    isApplicationRunning = false;
}

void TerminalUI::hookRender(Transport* activeSocket)
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    activeTransport = activeSocket;
    if (activeTransport != nullptr)
        isApplicationRunning = true;

    renderInterfaceUnlocked();
}

void TerminalUI::hookInputChar(char inputCharacter)
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    userInputBuffer.push_back(inputCharacter);
    renderInterfaceUnlocked();
}

void TerminalUI::hookBackspace()
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    if (!userInputBuffer.empty())
    {
        removeLastUtf8Codepoint(userInputBuffer);
        renderInterfaceUnlocked();
    }
}

void TerminalUI::hookArrowUp()
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    if (currentUiMode == UiMode::ChatList)
    {
        selectedChatListIndex = (std::max)(0, selectedChatListIndex - 1);
    }
    else
    {
        messageScrollOffset = (std::min)(messageScrollOffset + 1, 50);
    }

    renderInterfaceUnlocked();
}

void TerminalUI::hookArrowDown()
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    if (currentUiMode == UiMode::ChatList)
    {
        selectedChatListIndex = (std::min)(static_cast<int>(cachedChatList.size()) - 1, selectedChatListIndex + 1);
    }
    else
    {
        messageScrollOffset = (std::max)(messageScrollOffset - 1, 0);
    }

    renderInterfaceUnlocked();
}

void TerminalUI::hookEscape()
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    if (currentUiMode == UiMode::ChatList)
    {
        toggleChatListMode(false);
        renderInterfaceUnlocked();
        return;
    }

    isApplicationRunning = false;
}

void TerminalUI::hookEnter()
{
    if (currentUiMode == UiMode::ChatList)
    {
        Chat selectedChatModel{};
        {
            std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
            if (cachedChatList.empty())
                return;

            selectedChatModel = cachedChatList[static_cast<size_t>(selectedChatListIndex)];
            toggleChatListMode(false);
        }

        loadAndSelectChat(selectedChatModel);
        return;
    }

    std::string extractedLine;

    {
        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
        if (userInputBuffer.empty())
            return;

        extractedLine = std::move(userInputBuffer);
    }

    if (!extractedLine.empty() && extractedLine[0] == '/')
    {
        toggleChatListMode(false);
        if (TerminalUI::processCommand(extractedLine))
        {
            userInputBuffer.clear();
            return;
        }
    }

    chatid_t destinationChatId = 0;
    {
        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

        if (!isUserLoggedIn())
        {
            TerminalUI::addMessage("Войдите: /login");
            return;
        }

        if (!isUserInActiveChat())
        {
            TerminalUI::addMessage("Выберите чат: /chat");
            return;
        }

        destinationChatId = currentActiveChat.value()->ChatInfo.Id;
    }

    Request messageRequest = Request::CreateCommitMessage(activeTransport->AccessToken, destinationChatId, extractedLine);
    Responce serverResponse = Protocol::SendRequest(*activeTransport, messageRequest);

    if (serverResponse.Type == ResponceType::Error)
    {
        TerminalUI::addMessage("Ошибка! " + std::string(serverResponse.Error.Message.buffer));
        return;
    }

    MemoryCache::storeMessageToChat(currentActiveChat.value()->ChatInfo.Id, serverResponse.CommitMessage.MessageModel, true);
    TerminalUI::addMessage(serverResponse.CommitMessage.MessageModel);
}

// Добавляет сообщение в UI с учетом того, какой чат сейчас открыт
void appendMessageToChatUnlocked(const Message& incomingMessage)
{
    const bool isCurrentlyViewingChat = isUserInActiveChat() && currentActiveChat.value()->ChatInfo.Id == incomingMessage.DestChat.Id;
    const bool isMessageFromSelf = currentLoggedInUser.has_value() && incomingMessage.From.Id == currentLoggedInUser->Id;

    if (incomingMessage.From.Id != SYSTEM_FROMID && !isMessageFromSelf && !isCurrentlyViewingChat)
    {
        unreadMessageCountByChatId[incomingMessage.DestChat.Id]++;
        unreadChatNamesByChatId[incomingMessage.DestChat.Id] = MemoryCache::getChatMemory(incomingMessage.DestChat.Id).value()->ChatInfo.Name;

        renderInterfaceUnlocked();
        return;
    }

    if (isCurrentlyViewingChat)
    {
        messageScrollOffset = 0;
        renderInterfaceUnlocked();
    }
}

// Основная функция отрисовки пользовательского интерфейса в терминал
void renderInterfaceUnlocked()
{
    size_t terminalWidth = 0;
    size_t terminalHeight = 0;

    TerminalUI::platformGetScreenSize(
        *reinterpret_cast<int*>(&terminalWidth),
        *reinterpret_cast<int*>(&terminalHeight));

    // Рассчитываем доступную высоту для окна чата
    calculatedChatHeight = static_cast<int>(terminalHeight - 5);

    // --- Окно с подсказками для слэш-команд ---
    struct CommandHintRow
    {
        std::string CommandString;
        std::string CommandDescription;
        bool IsAddonCommand = false;
    };

    std::vector<CommandHintRow> generatedHintLines;
    int reservedHintBoxLines = 0;

    const bool isTypingCommand =
        (currentUiMode == UiMode::Chat) &&
        (!userInputBuffer.empty()) &&
        (userInputBuffer[0] == '/');

    if (isTypingCommand)
    {
        // Извлекаем только имя команды, исключая ведущий слэш и параметры
        std::string_view commandView(userInputBuffer);
        commandView.remove_prefix(1);

        const size_t firstSpacePosition = commandView.find(' ');
        const std::string_view typedCommandName = (firstSpacePosition == std::string_view::npos)
            ? commandView : commandView.substr(0, firstSpacePosition);

        std::unordered_set<std::string> processedCommands;
        std::vector<CommandHintRow> potentialCandidates;
        potentialCandidates.reserve(16);

        // Ищем совпадения среди локальных команд
        for (const SlashCommandMetadata& localCommand : getLocalSlashCommandsList())
        {
            if (checkStringStartsWithCaseInsensitive(localCommand.Name, typedCommandName) && processedCommands.insert(localCommand.Name).second)
            {
                potentialCandidates.push_back(CommandHintRow
                    {
                        .CommandString = "/" + localCommand.Name,
                        .CommandDescription = localCommand.Description,
                        .IsAddonCommand = false,
                    });
            }
        }

        // Ищем совпадения среди серверных команд (аддонов)
        for (const std::string& addonCommand : cachedAddonCommands)
        {
            if (checkStringStartsWithCaseInsensitive(addonCommand, typedCommandName) && processedCommands.insert(addonCommand).second)
            {
                potentialCandidates.push_back(CommandHintRow
                    {
                        .CommandString = "/" + addonCommand,
                        .CommandDescription = "addon",
                        .IsAddonCommand = true,
                    });
            }
        }

        constexpr size_t maxAllowedHints = 8;
        if (!potentialCandidates.empty())
        {
            // Сортируем подсказки по алфавиту
            std::sort(potentialCandidates.begin(), potentialCandidates.end(), [](const CommandHintRow& itemA, const CommandHintRow& itemB)
                {
                    return itemA.CommandString < itemB.CommandString;
                });

            if (potentialCandidates.size() > maxAllowedHints)
                potentialCandidates.resize(maxAllowedHints);

            generatedHintLines = std::move(potentialCandidates);
            reservedHintBoxLines = static_cast<int>(generatedHintLines.size()) + 2;
        }
    }

    // --- Подготовка параметров сетки и буфера вывода ---
    const int maximumMessageRows = static_cast<int>(max(0, terminalHeight - 6 - reservedHintBoxLines));
    const int innerContainerWidth = static_cast<int>(max(4, terminalWidth - 4));

    std::vector<Message> currentChatMessages;

    if (isUserInActiveChat())
        currentChatMessages = currentActiveChat.value()->Messages;

    const int totalMessageCount = static_cast<int>(currentChatMessages.size());
    const int startMessageIndex = (std::max)(0, totalMessageCount - maximumMessageRows - messageScrollOffset);

    std::string outputBuffer;
    outputBuffer.reserve(terminalWidth * terminalHeight * 4u + 256u);

    // Скрываем курсор, очищаем экран, перемещаем каретку в начало и устанавливаем черный фон
    outputBuffer.append("\x1b[?25l\x1b[2J\x1b[H\x1b[40m");

    // Лямбда для создания горизонтального разделителя
    auto generateHorizontalLine = [terminalWidth]()
        {
            std::string lineString;
            lineString.reserve(static_cast<size_t>(terminalWidth));
            lineString.push_back('+');
            lineString.append(static_cast<size_t>(terminalWidth - 2), '-');
            lineString.push_back('+');
            return lineString;
        };

    const std::string displayedUsername = isUserLoggedIn() ? std::string(currentLoggedInUser->Name.buffer) : "not_logged";
    const std::string displayedChatName = isUserInActiveChat() ? std::string(currentActiveChat.value()->ChatInfo.Name.buffer) : "no_chat";
    const std::string inputPromptLabel = displayedUsername + "@" + displayedChatName + "> ";

    // Автоматический переход в список чатов, если чат еще не выбран
    if (currentUiMode == UiMode::Chat && isUserLoggedIn() && currentActiveChat.value() == &idleChatPlaceholder && !cachedChatList.empty())
        currentUiMode = UiMode::ChatList;

    // --- Отрисовка верхнего колонтитула ---
    outputBuffer.append(getAnsiColorForeground(Color::DARK_CYAN))
        .append(generateHorizontalLine())
        .append(getAnsiResetStyle())
        .push_back('\n');

    {
        std::string topHeaderRaw = isUserInActiveChat() ? ("Чат: " + std::string(currentActiveChat.value()->ChatInfo.Name.buffer)) : "TeleCrap";
        topHeaderRaw = truncateUtf8StringByBytes(topHeaderRaw, static_cast<size_t>(innerContainerWidth));

        outputBuffer.append("\x1b[90m|\x1b[0m")
            .append(getAnsiColorForeground(Color::CYAN))
            .append(topHeaderRaw)
            .append(getAnsiResetStyle())
            .append("\x1b[K\33[1000C|\n");
    }

    // --- Отрисовка основного контента (Список чатов ИЛИ Сообщения) ---
    if (currentUiMode == UiMode::ChatList)
    {
        const int listItemsCount = static_cast<int>(cachedChatList.size());
        const int visibleListItems = maximumMessageRows;
        const int topListIndex = max(0, (std::min)(selectedChatListIndex - visibleListItems / 2, listItemsCount - visibleListItems));

        for (int rowOffset = 0; rowOffset < maximumMessageRows; ++rowOffset)
        {
            const int currentItemIndex = topListIndex + rowOffset;
            outputBuffer.append("\x1b[90m|\x1b[0m ");

            if (currentItemIndex >= listItemsCount)
            {
                outputBuffer.append("\x1b[K\33[1000C|\n");
                continue;
            }

            const Chat& listChatModel = cachedChatList[currentItemIndex];
            const bool isItemSelected = (currentItemIndex == selectedChatListIndex);
            std::string chatLineText = std::string(listChatModel.Name.buffer);

            chatLineText = truncateUtf8StringByBytes(chatLineText, static_cast<size_t>(innerContainerWidth - 2));
            const Color chatItemColor = (listChatModel.Type == ChatType::Direct) ? Color::MAGENTA : Color::CYAN;

            if (isItemSelected)
                outputBuffer.append("\x1b[7m");

            outputBuffer.append(getAnsiColorForeground(chatItemColor))
                .append("> ")
                .append(chatLineText)
                .append(getAnsiResetStyle());

            if (isItemSelected)
                outputBuffer.append(getAnsiResetStyle());

            outputBuffer.append("\x1b[K\33[1000C|\n");
        }
    }
    else
    {
        // Отрисовка сообщений чата
        for (int rowOffset = 0; rowOffset < maximumMessageRows; ++rowOffset)
        {
            const int currentMessageIndex = startMessageIndex + rowOffset;
            if (currentMessageIndex >= totalMessageCount)
            {
                outputBuffer.append("\x1b[90m|")
                    .append(static_cast<size_t>(terminalWidth - 2), ' ')
                    .append("\x1b[0m\33[1000C|\n");

                continue;
            }

            const Message& messageModel = currentChatMessages[static_cast<size_t>(currentMessageIndex)];
            std::string timeString = "[" + TerminalUI::platformGetTimeString(messageModel.Timestamp) + "] ";
            std::string authorPrefix = std::string(messageModel.From.Name.buffer) + ": ";

            Color timeStyleColor = Color::WHITE;
            Color nameStyleColor = Color::YELLOW;
            Color textStyleColor = Color::WHITE;

            if (messageModel.From.Id == SYSTEM_FROMID)
            {
                timeStyleColor = Color::DARK_YELLOW;
                nameStyleColor = Color::DARK_YELLOW;
                textStyleColor = Color::DARK_YELLOW;
            }
            else if (currentLoggedInUser.has_value() && messageModel.From.Id == currentLoggedInUser->Id)
            {
                timeStyleColor = Color::CYAN;
                nameStyleColor = Color::GREEN;
            }
            else
            {
                timeStyleColor = Color::CYAN;
            }

            std::string messageBodyText = messageModel.Text.buffer;
            const size_t combinedPrefixLength = timeString.size() + authorPrefix.size();
            size_t containerWidthSizeT = static_cast<size_t>(innerContainerWidth);

            messageBodyText = truncateUtf8StringByBytes(messageBodyText, containerWidthSizeT > combinedPrefixLength ? containerWidthSizeT - combinedPrefixLength : 0) + " ";

            outputBuffer.append("\x1b[90m|\x1b[0m ")
                .append(getAnsiColorForeground(timeStyleColor))
                .append(timeString)
                .append(getAnsiResetStyle());

            if (!authorPrefix.empty())
            {
                outputBuffer.append(getAnsiColorForeground(nameStyleColor))
                    .append(authorPrefix)
                    .append(getAnsiResetStyle());
            }

            outputBuffer.append(getAnsiColorForeground(textStyleColor))
                .append(messageBodyText)
                .append(getAnsiResetStyle())
                .append("\x1b[K\33[1000C|\n");
        }
    }

    // --- Отрисовка подсказок для команд (если есть) ---
    if (reservedHintBoxLines > 0)
    {
        const int hintContentColumns = innerContainerWidth;
        auto generateHintBorder = [&hintContentColumns]()
            {
                std::string borderString;
                borderString.push_back('+');

                if (hintContentColumns > 2)
                    borderString.append(static_cast<size_t>(hintContentColumns - 2), '-');

                if (hintContentColumns >= 2)
                    borderString.push_back('+');

                return borderString;
            };

        size_t longestCommandLength = 0;
        for (const CommandHintRow& hintRow : generatedHintLines)
            longestCommandLength = (std::max)(longestCommandLength, countUtf8Codepoints(hintRow.CommandString));

        longestCommandLength = min(longestCommandLength + 2u, 22u);
        outputBuffer.append("\x1b[90m|\x1b[0m ")
            .append(getAnsiColorForeground(Color::DARK_GRAY))
            .append(generateHintBorder())
            .append(getAnsiResetStyle())
            .append("\x1b[K\33[1000C|\n");

        for (const CommandHintRow& hintRow : generatedHintLines)
        {
            const size_t currentCommandWidth = countUtf8Codepoints(hintRow.CommandString);
            size_t paddingSpaces = (longestCommandLength > currentCommandWidth) 
                ? (longestCommandLength - currentCommandWidth) : 1u;

            const int availableDescriptionBudget = static_cast<int>(hintContentColumns - longestCommandLength + 1);
            std::string commandDescriptionText = hintRow.CommandDescription;

            if (availableDescriptionBudget > 0)
            {
                if (countUtf8Codepoints(commandDescriptionText) > availableDescriptionBudget)
                {
                    const size_t charactersToKeep = availableDescriptionBudget > 3 ? availableDescriptionBudget - 3 : availableDescriptionBudget;
                    commandDescriptionText = extractMaxUtf8Codepoints(commandDescriptionText, charactersToKeep) + "...";
                }
            }
            else
            {
                commandDescriptionText.clear();
            }

            outputBuffer.append("\x1b[90m|\x1b[0m ")
                .append(getAnsiColorForeground(Color::YELLOW))
                .append(hintRow.CommandString)
                .append(getAnsiResetStyle());

            for (size_t spaceIndex = 0; spaceIndex < paddingSpaces; ++spaceIndex)
                outputBuffer.push_back(' ');

            outputBuffer.append(getAnsiColorForeground(Color::DARK_GRAY))
                .append(commandDescriptionText)
                .append(getAnsiResetStyle())
                .append("\x1b[K\33[1000C|\n");
        }

        outputBuffer.append("\x1b[90m|\x1b[0m ")
            .append(getAnsiColorForeground(Color::DARK_GRAY))
            .append(generateHintBorder())
            .append(getAnsiResetStyle())
            .append("\x1b[K\33[1000C|\n");
    }

    // --- Отрисовка строки ввода и нижнего статуса ---
    outputBuffer.append(getAnsiColorForeground(Color::DARK_CYAN))
        .append(generateHorizontalLine())
        .append(getAnsiResetStyle())
        .push_back('\n');

    size_t activePromptLength = inputPromptLabel.size();
    std::string safeDisplayInput = userInputBuffer;

    if (activePromptLength + 3 > static_cast<size_t>(innerContainerWidth))
    {
        safeDisplayInput.clear();
    }
    else
    {
        const size_t maxInputAllowed = innerContainerWidth - activePromptLength;
        if (safeDisplayInput.size() > maxInputAllowed)
            safeDisplayInput = "..." + safeDisplayInput.substr(safeDisplayInput.size() - (maxInputAllowed - 3));

    }

    outputBuffer.append("\x1b[90m|\x1b[0m ")
        .append(getAnsiColorForeground(Color::GREEN))
        .append(inputPromptLabel)
        .append(getAnsiResetStyle());

    outputBuffer.append(getAnsiColorForeground(Color::WHITE))
        .append(safeDisplayInput)
        .append(getAnsiResetStyle())
        .append("\x1b[K\33[1000C|\n");

    outputBuffer.append(getAnsiColorForeground(Color::DARK_CYAN))
        .append(generateHorizontalLine())
        .append(getAnsiResetStyle())
        .push_back('\n');

    // --- Бейдж уведомлений о непрочитанных сообщениях ---
    const uint32_t totalUnreadCountBadge = calculateTotalUnreadMessages();
    std::string notificationBadgeCore;

    if (totalUnreadCountBadge > 0)
    {
        notificationBadgeCore = "[" + std::to_string(totalUnreadCountBadge) + "] ";
        if (unreadMessageCountByChatId.size() == 1)
        {
            notificationBadgeCore += truncateUtf8StringByBytes(unreadChatNamesByChatId[unreadMessageCountByChatId.begin()->first], 28);
        }
        else
        {
            notificationBadgeCore += std::to_string(unreadMessageCountByChatId.size()) + " \xD1\x87\xD0\xB0\xD1\x82\xD0\xBE\xD0\xB2"; // "чатов"
        }
    }

    std::string leftStatusPanel = "| ESC — выход | /help | ";
    if (!notificationBadgeCore.empty())
    {
        leftStatusPanel = truncateUtf8StringByBytes(
            leftStatusPanel, max(6, terminalWidth - notificationBadgeCore.size() - 2));
    }

    outputBuffer.append(getAnsiColorForeground(Color::DARK_GRAY)).append(leftStatusPanel);
    if (!notificationBadgeCore.empty())
    {
        char ansiPosition[40]{};
        snprintf(
            ansiPosition,
            sizeof(ansiPosition),
            "\x1b[%d;%dH",
            static_cast<int>(terminalHeight),
            max(2, terminalWidth - notificationBadgeCore.size() + 1));

        outputBuffer.append(ansiPosition)
            .append(getAnsiNotificationStyle())
            .append(notificationBadgeCore)
            .append(getAnsiResetStyle());
    }

    outputBuffer.append("\x1b[K\33[1000C|\n").append(getAnsiResetStyle());

    // Возвращаем курсор обратно в строку ввода текста
    char cursorPositionCommand[48]{};
    snprintf(
        cursorPositionCommand,
        sizeof(cursorPositionCommand),
        "\x1b[?25h\x1b[%d;%dH",
        static_cast<int>(calculatedChatHeight + 3),
        static_cast<int>(min(3 + activePromptLength + safeDisplayInput.size(), terminalWidth)));

    outputBuffer.append(cursorPositionCommand);
    TerminalUI::platformWriteStdout(outputBuffer);
}

void TerminalUI::tryAuth(Transport* connectionSocket, std::string& inputUsername, std::string& inputPassword)
{
    activeTransport = connectionSocket;
    Request loginRequest = Request::CreateLogin(activeTransport->AccessToken, inputUsername, inputPassword);
    Responce serverResponse = Protocol::SendRequest(*activeTransport, loginRequest);

    if (serverResponse.Type == ResponceType::Error)
    {
        addMessage("Ошибка: " + std::string(serverResponse.Error.Message.buffer));
        drawUI();
        return;
    }

    {
        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
        currentLoggedInUser = serverResponse.Auth.UserModel;
        currentActiveChat = &idleChatPlaceholder;
    }

    addMessage("Добро пожаловать, " + inputUsername + "!");

    fetchAndCacheChatList();
    fetchAndCacheAddonCommands();

    try
    {
        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
        if (!cachedChatList.empty())
            toggleChatListMode(true);
    }
    ignore

    drawUI();
}

void TerminalUI::drawUI()
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
    renderInterfaceUnlocked();
}

void TerminalUI::addMessage(const Message& incomingMessage)
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
    appendMessageToChatUnlocked(incomingMessage);
}

void TerminalUI::addMessage(const std::string& plainTextContent)
{
    std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);

    Message systemMessage{};
    systemMessage.From.Id = SYSTEM_FROMID;
    systemMessage.Text = plainTextContent.c_str();

    if (currentActiveChat.value() == &idleChatPlaceholder)
    {
        systemMessage.DestChat = idleChatPlaceholder.ChatInfo;
        idleChatPlaceholder.Messages.push_back(systemMessage);
    }
    else
    {
        systemMessage.DestChat = currentActiveChat.value()->ChatInfo;
        MemoryCache::appendMessageToChat(systemMessage.DestChat.Id, systemMessage);
    }

    appendMessageToChatUnlocked(systemMessage);
}

void TerminalUI::onChatRenamed(const Chat& renamedChat)
{
    addMessage("Чат переименован в " + std::string(renamedChat.Name));
    drawUI();
}

void TerminalUI::onUserKickedFromChat(const Chat& targetChat, const User& targetUser)
{
    addMessage("Пользователь @" + std::string(targetUser.Name) + " исключен из чата");
    drawUI();
}

void TerminalUI::onUserBannedFromChat(const Chat& targetChat, const User& targetUser)
{
    addMessage("Пользователь @" + std::string(targetUser.Name) + " забанен в чате");
    drawUI();
}

void TerminalUI::onUserUnbannedFromChat(const Chat& targetChat, const User& targetUser)
{
    addMessage("Пользователь @" + std::string(targetUser.Name) + " разбанен в чате");
    drawUI();
}

bool TerminalUI::processCommand(const std::string& userCommandString)
{
    if (userCommandString.empty())
        return true;

    std::vector<std::string> commandTokens;
    std::stringstream stringStream(userCommandString);
    std::string currentToken;

    while (stringStream >> currentToken)
        commandTokens.push_back(currentToken);

    if (commandTokens[0] == "/help")
    {
        addMessage("Доступные команды: /register, /login, /logout, /chat, /chats, /members, /create, /join, /refresh, /clear, /exit");
        drawUI();
        return true;
    }

    if (commandTokens[0] == "/register" || commandTokens[0] == "/login")
    {
        if (commandTokens.size() < 3)
        {
            addMessage("Использование: " + commandTokens[0] + " [username] [password]");
            drawUI();
            return true;
        }

        std::string authUser = commandTokens[1];
        std::string authPass = commandTokens.size() >= 3 ? commandTokens[2] : "";

        Request authRequest = (commandTokens[0] == "/register")
            ? Request::CreateRegister(activeTransport->AccessToken, authUser, authPass)
            : Request::CreateLogin(activeTransport->AccessToken, authUser, authPass);

        Responce authResponse = Protocol::SendRequest(*activeTransport, authRequest);
        if (authResponse.Type == ResponceType::Error)
        {
            addMessage("Ошибка: " + std::string(authResponse.Error.Message.buffer));
            drawUI();
            return true;
        }

        {
            std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
            currentLoggedInUser = authResponse.Auth.UserModel;
            currentActiveChat = &idleChatPlaceholder;
        }

        addMessage("Добро пожаловать, " + authUser + "!");

        fetchAndCacheChatList();
        fetchAndCacheAddonCommands();

        try
        {
            std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
            if (!cachedChatList.empty())
                toggleChatListMode(true);
        }
        ignore

        drawUI();
        return true;
    }

    if (commandTokens[0] == "/logout")
    {
        if (!isUserLoggedIn())
        {
            addMessage("Неавторизован!");
            drawUI();
            return false;
        }

        Request logoutRequest = Request::CreateLogin(activeTransport->AccessToken, "", "");
        Protocol::SendRequest(*activeTransport, logoutRequest);

        {
            std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
            currentLoggedInUser = std::nullopt;
            currentActiveChat = &idleChatPlaceholder;

            selectedChatListIndex = 0;
            cachedChatList.clear();
            toggleChatListMode(false);

            cachedAddonCommands.clear();
            areAddonCommandsLoaded = false;
        }

        drawUI();
        return true;
    }

    if (commandTokens[0] == "/chats")
    {
        if (!isUserLoggedIn())
        {
            addMessage("Неавторизован!");
            drawUI();
            return true;
        }

        try
        {
            fetchAndCacheChatList();
        }
        catch (const std::runtime_error& requestError)
        {
            addMessage(std::string("Ошибка: ") + requestError.what());
            drawUI();
            return true;
        }

        if (!areAddonCommandsLoaded)
        {
            fetchAndCacheAddonCommands();
        }

        std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
        toggleChatListMode(true);
        drawUI();
        return true;
    }

    if (commandTokens[0] == "/chat")
    {
        if (!isUserLoggedIn())
        {
            addMessage("Неавторизован!");
            drawUI();
            return true;
        }

        if (commandTokens.size() < 2)
        {
            addMessage("Использование: /chat [chat_name]");
            drawUI();
            return true;
        }

        Request getChatInfoRequest = Request::CreateGetChatInfo(activeTransport->AccessToken, commandTokens[1]);
        Responce chatInfoResponse = Protocol::SendRequest(*activeTransport, getChatInfoRequest);

        if (chatInfoResponse.Type == ResponceType::Error)
        {
            addMessage("Ошибка: " + std::string(chatInfoResponse.Error.Message.buffer));
            drawUI();
            return true;
        }

        return loadAndSelectChat(chatInfoResponse.GetChatInfo.ChatModel);
    }

    if (commandTokens[0] == "/members")
    {
        if (!isUserLoggedIn() || !isUserInActiveChat())
        {
            addMessage("Войдите и выберите чат");
            drawUI();
            return true;
        }

        for (const User& chatMember : currentActiveChat.value()->Members)
        {
            addMessage(chatMember.Name.buffer);
        }

        drawUI();
        return true;
    }

    if (commandTokens[0] == "/create" || commandTokens[0] == "/join")
    {
        if (!isUserLoggedIn())
        {
            addMessage("Неавторизован!");
            drawUI();
            return true;
        }

        if (commandTokens.size() < 2)
        {
            addMessage("Использование: " + commandTokens[0] + " [название]");
            drawUI();
            return true;
        }

        const std::string combinedChatQuery = joinStringTokens(commandTokens, 1);
        Request groupActionRequest = (commandTokens[0] == "/create")
            ? Request::CreateSpawnGroupChat(activeTransport->AccessToken, combinedChatQuery)
            : Request::CreateJoinGroupChat(activeTransport->AccessToken, combinedChatQuery);

        Responce groupActionResponse = Protocol::SendRequest(*activeTransport, groupActionRequest);
        if (groupActionResponse.Type == ResponceType::Error)
        {
            addMessage(std::string(groupActionResponse.Error.Message.buffer));
            drawUI();
            return true;
        }

        const Chat& resultedChat = (commandTokens[0] == "/create")
            ? groupActionResponse.CreateChat.ChatModel
            : groupActionResponse.JoinChat.ChatModel;

        return loadAndSelectChat(resultedChat);
    }

    if (commandTokens[0] == "/refresh")
    {
        if (!isUserLoggedIn() || !isUserInActiveChat())
        {
            addMessage("Войдите и выберите чат");
            drawUI();
            return true;
        }

        try
        {
            Request historyRequest = Request::CreateGetChatHistory(activeTransport->AccessToken, currentActiveChat.value()->ChatInfo.Id);
            std::vector<Responce> serverResponses = Protocol::SendRequestList(*activeTransport, historyRequest,
                [](Responce& responseData) { return responseData.GetChatHistory.RemainingMessages; });

            ChatMemory* currentTargetChat = currentActiveChat.value();
            {
                std::lock_guard<std::recursive_mutex> threadLock(interfaceMutex);
                currentTargetChat->Messages.clear();

                for (Responce& historyResponse : serverResponses)
                {
                    MemoryCache::storeMessageToChat(currentTargetChat->ChatInfo.Id, historyResponse.GetChatHistory.CurrentMessage, false);
                    appendMessageToChatUnlocked(historyResponse.GetChatHistory.CurrentMessage);
                }

                currentTargetChat->MessagesLoaded = true;
            }

            drawUI();
            return true;
        }
        catch (const std::runtime_error& reloadError)
        {
            addMessage("Ошибка обновления: " + std::string(reloadError.what()));
            drawUI();
            return true;
        }
    }

    if (commandTokens[0] == "/clear")
    {
        drawUI();
        return true;
    }

    if (commandTokens[0] == "/exit")
    {
        isApplicationRunning = false;
        return true;
    }

    return false;
}