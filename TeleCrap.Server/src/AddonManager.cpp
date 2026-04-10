#include <string>
#include <unordered_map>
#include <filesystem>
#include <exception>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <array>
#include <mutex>

#include <telecrap/Models.h>
#include <telecrap/Request.h>
#include <telecrap/Responce.h>
#include <telecrap/Update.h>
#include <telecrap/Protocol.h>
#include <telecrap/Transport.h>
#include <telecrap/Log.h>

#include <sol/sol.hpp>
#include <sol/error.hpp>
#include <sol/forward.hpp>
#include <sol/state.hpp>
#include <sol/types.hpp>
#include <sol/protected_function_result.hpp>

#include "../include/AddonManager.h"
#include "../include/Database.h"
#include "../include/Backend.h"
#include "../include/ChatHistory.h"

static std::mutex addonOpsMutex;
static sol::state lua;
static std::unordered_map<std::string, sol::protected_function> commands;
static sol::table addonState;

//   :
//    Lua-   .  ""     ,
//      / =  RCE/.
//    Unsafe,  :
// -      /  sandbox';
// -          ( unwind  Lua).
static std::string shellEscape(const std::string& input)
{
    //  escaping       .
    //      injection;   best-effort   .
    std::string escaped;
    escaped.reserve(input.size() + 8);
    for (char c : input)
    {
        if (c == '"')
            escaped += "\\\"";
        else
            escaped += c;
    }

    return escaped;
}

static std::string execCaptureUnsafe(const std::string& command)
{
    //  shell-   stdout.
    //  ;     .
    std::array<char, 256> buffer{};
    std::string result;

#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (pipe == nullptr)
        return result;

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        result += buffer.data();

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return result;
}

static std::string readFileUnsafe(const std::string& path)
{
    //   .    Lua.
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return std::string();

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool writeFileUnsafe(const std::string& path, const std::string& content)
{
    //   .      Lua.
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return false;
    out << content;
    return static_cast<bool>(out);
}

static bool appendFileUnsafe(const std::string& path, const std::string& content)
{
    //    .      Lua.
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out.is_open())
        return false;
    out << content;
    return static_cast<bool>(out);
}

static sol::table listDirUnsafe(const std::string& path)
{
    // Directory listing  Lua.   -.
    sol::table out = lua.create_table();
    int index = 1;
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(path))
            out[index++] = entry.path().string();
    }
    catch (...)
    {
        //   ( / ) ->  ,  Lua    C++.
        return lua.create_table();
    }
    return out;
}

static bool isDirectChat(const ChatInfo& chat)
{
    return chat.OwnerId == SYSTEM_FROMID;
}

static Chat asChatModel(const ChatInfo& chat)
{
    return Chat
    {
        .Id = chat.Id,
        .Name = chat.Name,
        .Type = isDirectChat(chat) ? ChatType::Direct : ChatType::Group
    };
}

static User asUserModel(const UserInfo& user)
{
    return User
    {
        .Id = user.Id,
        .Name = user.Name
    };
}

static sol::table usersToTable(const std::vector<UserInfo>& users)
{
    sol::table out = lua.create_table(static_cast<int>(users.size()), 0);
    for (size_t i = 0; i < users.size(); ++i)
        out[i + 1] = users[i];

    return out;
}

static sol::table messagesToTable(const std::vector<Message>& messages)
{
    sol::table out = lua.create_table(static_cast<int>(messages.size()), 0);
    for (size_t i = 0; i < messages.size(); ++i)
        out[i + 1] = messages[i];
    
    return out;
}

struct CommandContext
{
    // Контекст выполнения команды: "снимок" входных параметров из обработчика сообщения.
    // Передается в Lua, чтобы аддон мог:
    // - понять, где он выполняется (чат/пользователь/команда),
    // - ответить в текущий сокет,
    // - при необходимости дернуть серверные операции (kick/ban/rename и т.п.).
    Transport* Socket;
    CommitMessageRequest Request;
    UserInfo Requestor;
    ChatInfo Chat;

    std::string Cmd;
    std::string Args;

    void SendSystemMessage(const std::string& text) const;
    bool IsDirectChat() const;
    bool IsRequestorOwner() const;
    bool IsRequestorMember() const;

    std::optional<UserInfo> FindUserById(userid_t userId) const;
    std::optional<UserInfo> FindUserByName(const std::string& username) const;
    std::optional<ChatInfo> FindChatById(chatid_t chatId) const;
    std::optional<ChatInfo> FindChatByName(const std::string& chatName) const;

    sol::table GetMembers() const;
    sol::table GetMessages() const;
    bool IsUserBanned(userid_t userId) const;
    bool KickUser(userid_t userId) const;
    bool BanUser(userid_t userId) const;
    bool UnbanUser(userid_t userId) const;
    bool RenameChat(const std::string& newName) const;
    bool IsChatOwner(userid_t userId) const;
    bool IsChatMember(userid_t userId) const;
    sol::optional<Message> SendSystemMessageToChatId(chatid_t chatId, const std::string& text) const;
};

void CommandContext::SendSystemMessage(const std::string& text) const
{
    try
    {
        Message message = Database::commitSystemMessage(Chat, Request.Timestamp, text);
        Update update{};
        update.Type = UpdateType::Message;
        update.MessageSent = message;
        Backend::pushUpdateToChatMembersAll(Chat, update);
    }
    catch (const std::exception& e)
    {
        // Never let C++ exceptions unwind through Lua call frames.
        Log::Error("Addons", std::string("SendSystemMessage failed: ") + e.what());
    }
    catch (...)
    {
        Log::Error("Addons", "SendSystemMessage failed: unknown error");
    }
}

bool CommandContext::IsDirectChat() const
{
    return isDirectChat(Chat);
}

bool CommandContext::IsRequestorOwner() const
{
    return Chat.OwnerId == Requestor.Id;
}

bool CommandContext::IsRequestorMember() const
{
    std::vector<UserInfo> members = Database::findMembersByChat(Chat);
    for (const UserInfo& member : members)
    {
        if (member.Id == Requestor.Id)
            return true;
    }

    return false;
}

std::optional<UserInfo> CommandContext::FindUserById(userid_t userId) const
{
    return Database::findUserById(userId);
}

std::optional<UserInfo> CommandContext::FindUserByName(const std::string& username) const
{
    fixed_string<CHATNAME_MAXLENGTH> name = std::string_view(username);
    return Database::findUserByName(name);
}

std::optional<ChatInfo> CommandContext::FindChatById(chatid_t chatId) const
{
    return Database::findChatById(chatId);
}

std::optional<ChatInfo> CommandContext::FindChatByName(const std::string& chatName) const
{
    fixed_string<CHATNAME_MAXLENGTH> name = std::string_view(chatName);
    return Database::findChatByName(name);
}

sol::table CommandContext::GetMembers() const
{
    return usersToTable(Database::findMembersByChat(Chat));
}

sol::table CommandContext::GetMessages() const
{
    return messagesToTable(Database::findMessagesByChat(Chat));
}

bool CommandContext::IsUserBanned(userid_t userId) const
{
    return Database::isUserBannedFromChat(Chat.Id, userId);
}

bool CommandContext::KickUser(userid_t userId) const
{
    if (isDirectChat(Chat))
        return false;

    if (userId == Chat.OwnerId)
        return false;

    Database::removeMemberFromChat(Chat.Id, userId);
    return true;
}

bool CommandContext::BanUser(userid_t userId) const
{
    if (isDirectChat(Chat))
        return false;

    std::optional<UserInfo> target = Database::findUserById(userId);
    if (!target.has_value())
        return false;

    if (target->Id == Chat.OwnerId)
        return false;

    Database::banUserInChat(Chat, target.value());
    Database::removeMemberFromChat(Chat.Id, target->Id);
    return true;
}

bool CommandContext::UnbanUser(userid_t userId) const
{
    if (isDirectChat(Chat))
        return false;

    Database::unbanUserInChat(Chat.Id, userId);
    return true;
}

bool CommandContext::RenameChat(const std::string& newName) const
{
    if (isDirectChat(Chat) || newName.empty())
        return false;

    fixed_string<CHATNAME_MAXLENGTH> chatName = std::string_view(newName);
    Database::renameChat(Chat, chatName);
    return true;
}

bool CommandContext::IsChatOwner(userid_t userId) const
{
    return Chat.OwnerId == userId;
}

bool CommandContext::IsChatMember(userid_t userId) const
{
    std::vector<UserInfo> members = Database::findMembersByChat(Chat);
    for (const UserInfo& member : members)
    {
        if (member.Id == userId)
            return true;
    }

    return false;
}

sol::optional<Message> CommandContext::SendSystemMessageToChatId(chatid_t chatId, const std::string& text) const
{
    std::optional<ChatInfo> target = Database::findChatById(chatId);
    if (!target.has_value())
        return sol::nullopt;

    return Database::commitSystemMessage(target.value(), Request.Timestamp, text);
}

void AddonManager::Init()
{
    // Сериализация с ExecuteCommand/ListRegisteredCommands: иначе пересоздание lua state
    // во время вызова аддона — UB и падение процесса.
    std::lock_guard<std::mutex> lock(addonOpsMutex);
    commands.clear();
    lua = sol::state{};

    lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table,
        sol::lib::coroutine
        //sol::lib::io,
        //sol::lib::os,
        //sol::lib::utf8,
        //sol::lib::debug
    );

    addonState = lua.create_table();
    lua.new_usertype<UserInfo>("UserInfo",
        "Id", &UserInfo::Id,
        "Name", [](const UserInfo& u) { return std::string(u.Name.c_str()); }
    );

    lua.new_usertype<User>("User",
        "Id", &User::Id,
        "Name", [](const User& u) { return std::string(u.Name.c_str()); }
    );

    lua.new_usertype<ChatInfo>("ChatInfo",
        "Id", &ChatInfo::Id,
        "Name", [](const ChatInfo& c) { return std::string(c.Name.c_str()); },
        "OwnerId", &ChatInfo::OwnerId
    );

    lua.new_usertype<Chat>("Chat",
        "Id", &Chat::Id,
        "Name", [](const Chat& c) { return std::string(c.Name.c_str()); },
        "Type", &Chat::Type
    );

    lua.new_usertype<Message>("Message",
        "Id", &Message::Id,
        "DestChat", &Message::DestChat,
        "From", &Message::From,
        "Timestamp", &Message::Timestamp,
        "Text", [](const Message& m) { return std::string(m.Text.c_str()); }
    );

    lua.new_enum("ChatType",
        "Group", ChatType::Group,
        "Direct", ChatType::Direct
    );

    lua.new_usertype<CommandContext>("CommandContext",
        "Socket", &CommandContext::Socket,
        "Requestor", &CommandContext::Requestor,
        "Request", &CommandContext::Request,
        "Chat", &CommandContext::Chat,

        "Cmd", &CommandContext::Cmd,
        "Args", &CommandContext::Args,

        "SendSystemMessage", &CommandContext::SendSystemMessage,
        "IsDirectChat", &CommandContext::IsDirectChat,
        "IsRequestorOwner", &CommandContext::IsRequestorOwner,
        "IsRequestorMember", &CommandContext::IsRequestorMember,
        "FindUserById", &CommandContext::FindUserById,
        "FindUserByName", &CommandContext::FindUserByName,
        "FindChatById", &CommandContext::FindChatById,
        "FindChatByName", &CommandContext::FindChatByName,
        "GetMembers", &CommandContext::GetMembers,
        "GetMessages", &CommandContext::GetMessages,
        "IsUserBanned", &CommandContext::IsUserBanned,
        "KickUser", &CommandContext::KickUser,
        "BanUser", &CommandContext::BanUser,
        "UnbanUser", &CommandContext::UnbanUser,
        "RenameChat", &CommandContext::RenameChat,
        "IsChatOwner", &CommandContext::IsChatOwner,
        "IsChatMember", &CommandContext::IsChatMember,
        "SendSystemMessageToChatId", &CommandContext::SendSystemMessageToChatId
    );

    lua.set_function("LogInfo", [](const std::string& tag, const std::string& message)
        {
            Log::Info("Lua: " + tag, message);
		});

    lua.set_function("LogWarn", [](const std::string& tag, const std::string& message)
        {
            Log::Info("Lua/WARN: " + tag, message);
        });

    lua.set_function("LogError", [](const std::string& tag, const std::string& message)
        {
            Log::Error("Lua: " + tag, message);
        });

    lua.set_function("LogTrace", [](const std::string& tag, const std::string& message)
        {
            Log::Trace("Lua: " + tag, message);
        });

    lua.set_function("UnixTimeMs", []() -> timestamp_t
        {
            return static_cast<timestamp_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        });

    lua.set_function("FindUserById", [](userid_t userId)
        {
            return Database::findUserById(userId);
        });

    lua.set_function("FindUserByName", [](const std::string& username)
        {
            fixed_string<CHATNAME_MAXLENGTH> name = std::string_view(username);
            return Database::findUserByName(name);
        });

    lua.set_function("FindChatById", [](chatid_t chatId)
        {
            return Database::findChatById(chatId);
        });

    lua.set_function("FindChatByName", [](const std::string& chatName)
        {
            fixed_string<CHATNAME_MAXLENGTH> name = std::string_view(chatName);
            return Database::findChatByName(name);
        });

    lua.set_function("GetMembersByChatId", [](chatid_t chatId)
        {
            std::optional<ChatInfo> chat = Database::findChatById(chatId);
            if (!chat.has_value())
                return lua.create_table();
        
            return usersToTable(Database::findMembersByChat(chat.value()));
        });

    lua.set_function("GetMessagesByChatId", [](chatid_t chatId)
        {
            std::optional<ChatInfo> chat = Database::findChatById(chatId);
            if (!chat.has_value())
                return lua.create_table();
            
            return messagesToTable(Database::findMessagesByChat(chat.value()));
        });

    lua.set_function("GetChatsByUserId", [](userid_t userId)
        {
            std::optional<UserInfo> user = Database::findUserById(userId);
            sol::table out = lua.create_table();
            if (!user.has_value())
                return out;

            const std::vector<ChatInfo> chats = Database::findChatsByUser(user.value());
            out = lua.create_table(static_cast<int>(chats.size()), 0);
            for (size_t i = 0; i < chats.size(); ++i)
                out[i + 1] = chats[i];
            
            return out;
        });

    lua.set_function("IsUserBannedFromChat", [](chatid_t chatId, userid_t userId)
        {
            return Database::isUserBannedFromChat(chatId, userId);
        });

    lua.set_function("CommitSystemMessage", [](chatid_t chatId, timestamp_t timestamp, const std::string& text) -> sol::optional<Message>
        {
            std::optional<ChatInfo> chat = Database::findChatById(chatId);
            if (!chat.has_value())
                return sol::nullopt;
            
            return Database::commitSystemMessage(chat.value(), timestamp, text);
        });

    lua.set_function("CreateGroupChatUnsafe", [](userid_t ownerUserId, const std::string& chatName) -> sol::optional<ChatInfo>
        {
            std::optional<UserInfo> owner = Database::findUserById(ownerUserId);
            if (!owner.has_value() || chatName.empty())
                return sol::nullopt;

            fixed_string<CHATNAME_MAXLENGTH> name = std::string_view(chatName);
            UserInfo ownerUser = owner.value();
            return Database::createGroupChat(ownerUser, name);
        });

    lua.set_function("JoinChatMemberUnsafe", [](chatid_t chatId, userid_t userId) -> bool
        {
            std::optional<ChatInfo> chat = Database::findChatById(chatId);
            std::optional<UserInfo> user = Database::findUserById(userId);
            if (!chat.has_value() || !user.has_value())
                return false;
    
            Database::joinChatMember(chat.value(), user.value());
            return true;
        });

    lua.set_function("BanUserInChatUnsafe", [](chatid_t chatId, userid_t userId) -> bool
        {
            std::optional<ChatInfo> chat = Database::findChatById(chatId);
            std::optional<UserInfo> user = Database::findUserById(userId);
            if (!chat.has_value() || !user.has_value())
                return false;
            
            Database::banUserInChat(chat.value(), user.value());
            return true;
        });

    lua.set_function("UnbanUserInChatUnsafe", [](chatid_t chatId, userid_t userId) -> bool
        {
            Database::unbanUserInChat(chatId, userId);
            return true;
        });

    lua.set_function("RemoveMemberFromChatUnsafe", [](chatid_t chatId, userid_t userId) -> bool
        {
            Database::removeMemberFromChat(chatId, userId);
            return true;
        });

    lua.set_function("RenameChatUnsafe", [](chatid_t chatId, const std::string& newName) -> bool
        {
            std::optional<ChatInfo> chat = Database::findChatById(chatId);
            if (!chat.has_value() || newName.empty())
                return false;
            
            fixed_string<CHATNAME_MAXLENGTH> name = std::string_view(newName);
            Database::renameChat(chat.value(), name);
            return true;
        });

    lua.set_function("RegisterCommand", [](const std::string& name, sol::protected_function callback)
        {
            commands[name] = callback;
            Log::Info("Addons", "Registered command handler : /" + name);
        });

    /*
    sol::table unsafeModule = lua.create_named_table("unsafe");
    unsafeModule.set_function("exec", [](const std::string& command) -> int
        {
            return std::system(command.c_str());
        });

    unsafeModule.set_function("exec_capture", [](const std::string& command) -> std::string
        {
            return execCaptureUnsafe(command);
        });

    unsafeModule.set_function("eval", [](const std::string& code)
        {
            return lua.safe_script(code, sol::script_pass_on_error);
        });

    unsafeModule.set_function("dofile", [](const std::string& path)
        {
            return lua.safe_script_file(path, sol::script_pass_on_error);
        });

    unsafeModule.set_function("read_file", [](const std::string& path) -> std::string
        {
            return readFileUnsafe(path);
        });

    unsafeModule.set_function("write_file", [](const std::string& path, const std::string& content) -> bool
        {
            return writeFileUnsafe(path, content);
        });

    unsafeModule.set_function("append_file", [](const std::string& path, const std::string& content) -> bool
        {
            return appendFileUnsafe(path, content);
        });

    unsafeModule.set_function("delete_file", [](const std::string& path) -> bool
        {
            try
            {
                return std::filesystem::remove(path);
            }
            catch (...)
            {
                return false;
            }
        });

    unsafeModule.set_function("mkdir", [](const std::string& path) -> bool
        {
            try
            {
                return std::filesystem::create_directories(path);
            }
            catch (...)
            {
                return false;
            }
        });

    unsafeModule.set_function("rename_path", [](const std::string& fromPath, const std::string& toPath) -> bool
        {
            try
            {
                std::filesystem::rename(fromPath, toPath);
                return true;
            }
            catch (...)
            {
                return false;
            }
        });

    unsafeModule.set_function("list_dir", [](const std::string& path)
        {
            return listDirUnsafe(path);
        });

    unsafeModule.set_function("exists", [](const std::string& path) -> bool
        {
            return std::filesystem::exists(path);
        });

    unsafeModule.set_function("cwd", []() -> std::string
        {
            return std::filesystem::current_path().string();
        });

    unsafeModule.set_function("sleep_ms", [](int milliseconds)
        {
            if (milliseconds <= 0)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        });
    */

    sol::table stateModule = lua.create_named_table("state");
    stateModule.set_function("set", [](const std::string& key, sol::object value)
        {
            addonState[key] = value;
        });

    stateModule.set_function("get", [](const std::string& key) -> sol::object
        {
            return addonState.get<sol::object>(key);
        });

    stateModule.set_function("has", [](const std::string& key) -> bool
        {
            sol::object v = addonState.get<sol::object>(key);
            return v.valid() && v.get_type() != sol::type::none && v.get_type() != sol::type::nil;
        });

    stateModule.set_function("del", [](const std::string& key)
        {
            addonState[key] = sol::lua_nil;
        });

    stateModule.set_function("clear", []()
        {
            addonState = lua.create_table();
        });

    stateModule.set_function("keys", []()
        {
            sol::table keys = lua.create_table();
            int idx = 1;
            for (const auto& kv : addonState)
            {
                sol::object keyObj = kv.first;
                if (keyObj.get_type() == sol::type::string)
                    keys[idx++] = keyObj.as<std::string>();
            }
            return keys;
        });

    sol::table httpModule = lua.create_named_table("http");
    httpModule.set_function("get", [](const std::string& url) -> std::string
        {
#ifdef _WIN32
            const std::string command = "powershell -NoProfile -Command \"try { (Invoke-WebRequest -UseBasicParsing -Uri \\\"" + shellEscape(url) + "\\\").Content } catch { '' }\"";
#else
            const std::string command = "curl -fsSL \"" + shellEscape(url) + "\"";
#endif
            return execCaptureUnsafe(command);
        });

    httpModule.set_function("post_json", [](const std::string& url, const std::string& jsonBody) -> std::string
        {
#ifdef _WIN32
            const std::string escapedBody = shellEscape(jsonBody);
            const std::string command = "powershell -NoProfile -Command \"$b = @'" + escapedBody + "'@; try { (Invoke-WebRequest -UseBasicParsing -Method POST -ContentType 'application/json' -Body $b -Uri \\\"" + shellEscape(url) + "\\\").Content } catch { '' }\"";
#else
            const std::string command = "curl -fsSL -X POST -H \"Content-Type: application/json\" --data \"" + shellEscape(jsonBody) + "\" \"" + shellEscape(url) + "\"";
#endif
            return execCaptureUnsafe(command);
        });

    httpModule.set_function("download_to_file", [](const std::string& url, const std::string& filePath) -> bool
        {
            const std::string content =
#ifdef _WIN32
                execCaptureUnsafe("powershell -NoProfile -Command \"try { (Invoke-WebRequest -UseBasicParsing -Uri \\\"" + shellEscape(url) + "\\\").Content } catch { '' }\"");
#else
                execCaptureUnsafe("curl -fsSL \"" + shellEscape(url) + "\"");
#endif
            if (content.empty())
                return false;
            return writeFileUnsafe(filePath, content);
        });

    //lua["unsafe"] = unsafeModule;
    lua["state"] = stateModule;
    lua["http"] = httpModule;
    //lua["package"]["loaded"]["unsafe"] = unsafeModule;
    lua["package"]["loaded"]["state"] = stateModule;
    lua["package"]["loaded"]["http"] = httpModule;

    /*
    lua.set_exception_handler([](lua_State* L, sol::optional<const std::exception&>, sol::string_view what) -> int
        {
            Log::Error("Addons", std::string("Lua/C++ bridge exception: ") + std::string(what.data(), what.size()));
            lua_pushlstring(L, what.data(), what.size());
            return 1;
        });

    lua.set_panic([](lua_State* L) -> int
        {
            const char* panicMessage = lua_tostring(L, -1);
            Log::Error("Addons", std::string("LUA PANIC: ") + (panicMessage != nullptr ? panicMessage : "<no message>"));
            return 0;
        });
    */

    std::filesystem::create_directory("addons");
    for (const auto& entry : std::filesystem::directory_iterator("addons"))
    {
        if (entry.path().extension() == ".lua")
        {
            try
            {
                lua.script_file(entry.path().string());
            }
            catch (const sol::error& e)
            {
                Log::Error("Addons", "LuaState error : " + entry.path().filename().generic_string() + " : " + e.what());
            }
        }
    }
}

bool AddonManager::ExecuteCommand(const std::string& cmd, const std::string& args, Transport* transport, CommitMessageRequest& Request, UserInfo& requestor, ChatInfo& chat)
{
    std::lock_guard<std::mutex> lock(addonOpsMutex);
    const auto find = commands.find(cmd);
    if (find == commands.end())
        return false;

    try
    {
        CommandContext ctx
        {
            .Socket = transport,
			.Request = Request,
			.Requestor = requestor,
			.Chat = chat,
			
            .Cmd = cmd,
			.Args = args
        };

        sol::protected_function func = find->second;
        sol::protected_function_result result = func(ctx);

        if (!result.valid())
        {
            sol::error err = result;
            Log::Error("Addons", "Lua Script error : " + find->first + " : " + err.what());
            return true;
        }
    }
    catch (const sol::error& e)
    {
        Log::Error("Addons", "LuaState error : " + find->first + " : " + e.what());
        return true;
    }
    catch (const std::exception& e)
    {
        Log::Error("Addons", "Command handler error : " + find->first + " : " + e.what());
        return true;
    }
    catch (...)
    {
        Log::Error("Addons", "Unknown error in command handler : " + find->first);
        return true;
    }

    return true;
}

std::vector<std::string> AddonManager::ListRegisteredCommands()
{
    std::lock_guard<std::mutex> lock(addonOpsMutex);
    std::vector<std::string> out;
    out.reserve(commands.size());
    for (const auto& kv : commands)
        out.push_back(kv.first);

    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
