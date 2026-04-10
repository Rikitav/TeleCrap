#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <stdexcept>
#include <atomic>
#include <iostream>
#include <chrono>
#include <map>
#include <optional>
#include <vector>
#include <cstdint>
#include <string_view>
#include <sstream>
#include <random>
#include <cctype>
#include <unordered_set>

#include <telecrap/Update.h>
#include <telecrap/Protocol.h>
#include <telecrap/Models.h>
#include <telecrap/Request.h>
#include <telecrap/Responce.h>
#include <telecrap/SocketHelper.h>
#include <telecrap/Log.h>

#include "../include/ChatHistory.h"
#include "../include/Backend.h"
#include "../include/Database.h"
#include "../include/AddonManager.h"

// ������������� �������:
// ���� ������ ����� � "������" ������� � ����������� ��������� ���������� �������.
// ������ ������ ������������ ��������� TCP-�������, � ������ ���������� "push" �������
// (Update) � ������� �� userId. ������ ������������ ����������� ��� ������� ����� GetUpdates.
//
// ������ ����������/�����������:
// - ������� UpdatesCache �������� ��������� mutex �� ������������ (UpdatesMutex[userId]).
// - �������� Transport* -> userId (ClientUsers) � ������ �������� ������� (ClientSockets)
//   ������ �������� ������������ (CredentialsMutex).
// - ����� ��������� ��� ����������� �������� Update "� ���" �� ������: ���������� ������������
//   ��������� (SendRequestList(GetUpdates)) �� ������� �������.

static std::atomic<bool> IsActive;
static std::recursive_mutex CredentialsMutex;

// ��� �������� ���������� (transport ����� � ���� � ��������� � ������ �������).
static std::vector<const Transport*> ClientSockets;

// �������� "���������� -> ������������" ��� �������������� ��������.
static std::map<const Transport*, userid_t> ClientUsers;

// Per-user ���������� �������� UpdatesCache. ������������ map, ����� mutex ���������� ������ �� userId.
static std::map<userid_t, std::mutex> UpdatesMutex;

// ������� push-���������� �� ������������. ������ ������ � ����� RequestType::GetUpdates.
static std::map<userid_t, std::vector<Update>> UpdatesCache;

static bool isDirectChat(std::optional<ChatInfo> chatInfo)
{
    return chatInfo.has_value() && chatInfo->OwnerId == SYSTEM_FROMID;
}

static bool isUserInChat(const UserInfo& user, const ChatInfo& chat)
{
    for (const UserInfo& m : Database::findMembersByChat(chat))
    {
        if (m.Id == user.Id)
            return true;
    }
    return false;
}

static std::string_view trimSv(std::string_view sv)
{
    // trim ��� ���������: �������� �� std::string_view, ������ �������/������� ��������.
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);

    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
    
    return sv;
}

static std::optional<UserInfo> resolveUserToken(std::string_view tok)
{
    // ������ ������ ������������ ��� ������ ���������.
    // �������:
    // - @name  (����� ������������ �� �����)
    // - $id    (����� ������������ �� id)
    tok = trimSv(tok);
    if (tok.empty())
        return std::nullopt;

    if (tok.front() == '@')
    {
        fixed_string<CHATNAME_MAXLENGTH> name;
        name.assign(tok.substr(1));
        return Database::findUserByName(name);
    }

    if (tok.front() == '$')
        return Database::findUserById(static_cast<userid_t>(std::stoul(std::string(tok.substr(1)))));

    return std::nullopt;
}

static void pushUpdateToUser(userid_t userId, const Update& update)
{
    // ��� �� ����������� ������������, ����� ������������ ������ (������ ����/�������)
    // ����� ������ ���������� ������ ����������� ��� ����������� lock.
    std::lock_guard<std::mutex> updatesLock(UpdatesMutex[userId]);
    UpdatesCache[userId].push_back(update);
}

void Backend::pushMessageToChatMembersExcept(const ChatInfo& chat, const Update& update, userid_t exceptUserId)
{
    for (const UserInfo& member : Database::findMembersByChat(chat))
    {
        if (member.Id == exceptUserId)
            continue;

        pushUpdateToUser(member.Id, update);
    }
}

void Backend::pushUpdateToChatMembersAll(const ChatInfo& chat, const Update& update)
{
    for (const UserInfo& member : Database::findMembersByChat(chat))
        pushUpdateToUser(member.Id, update);
}

std::vector<OnlineClientInfo> Backend::listOnlineClients()
{
    std::vector<OnlineClientInfo> out;
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    out.reserve(ClientSockets.size());

    for (const Transport* transport : ClientSockets)
    {
        OnlineClientInfo info{};
        info.AccessToken = transport->AccessToken;
        info.PeerPort = SocketHelper::PeerPortOf(*transport);
        info.PeerAddress = SocketHelper::PeerIdentificatorOf(*transport);

        const auto it = ClientUsers.find(transport);
        if (it != ClientUsers.end())
        {
            info.HasUser = true;
            info.UserId = it->second;
        }

        out.push_back(std::move(info));
    }

    return out;
}

bool Backend::dropConnectionByPeerPort(u_short peerPort)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    for (const Transport* transport : ClientSockets)
    {
        if (SocketHelper::PeerPortOf(*transport) != peerPort)
            continue;

        const SOCKET s = transport->TransportSocket;
        if (s == 0 || s == INVALID_SOCKET)
            return false;

        shutdown(s, 0);
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return true;
    }

    return false;
}

size_t Backend::pushGlobalAlert(const std::string& text)
{
    Update u{};
    u.Type = UpdateType::Message;
    u.MessageSent.Id = 0;
    u.MessageSent.DestChat.Id = SYSTEM_FROMID;
    u.MessageSent.DestChat.Name = "server_alert";
    u.MessageSent.DestChat.Type = ChatType::Group;
    u.MessageSent.From.Id = SYSTEM_FROMID;
    u.MessageSent.From.Name = "SERVER";
    u.MessageSent.Timestamp = static_cast<timestamp_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    u.MessageSent.Text = text;

    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    size_t sent = 0;
    for (const auto& [transport, userId] : ClientUsers)
    {
        (void)transport;
        pushUpdateToUser(userId, u);
        ++sent;
    }

    return sent;
}

static bool tryHandleSlashCommand(const Transport* transport, UserInfo& requestor, CommitMessageRequest& request, ChatInfo& chat)
{
    // Slash-������� �������������� �� ������� �� ���������� ��������� ��� �������� ������.
    // ���� �������� ��� "����������" ������� ���������, ��� � ������� ������� (��. AddonManager).
    std::string raw(request.Content.c_str());
    raw = trimSv(raw);
    if (raw.empty() || raw.front() != '/')
        return false;

    raw = raw.substr(1);
    const size_t sp = raw.find(' ');

    std::string cmd = (sp == std::string::npos) ? raw : raw.substr(0, sp);
    std::string rest = (sp == std::string::npos) ? std::string{} : std::string(trimSv(raw.substr(sp + 1)));

    auto chatModelGroup = [&chat]()
    {
        return Chat{ .Id = chat.Id, .Name = chat.Name, .Type = ChatType::Group };
    };

    if (cmd != "kick" && cmd != "ban" && cmd != "unban" && cmd != "rename")
        return AddonManager::ExecuteCommand(cmd, rest, const_cast<Transport*>(transport), request, requestor, chat);

    // ������� ��������� ��������� ������ � ��������� ����� � ������ ��������� ����.
    const bool direct = isDirectChat(chat);
    if (direct)
    {
        Protocol::SendResponce(*transport, Responce::CreateError("��� ������� ������ ��� ��������� �����"));
        return true;
    }

    if (chat.OwnerId != requestor.Id)
    {
        Protocol::SendResponce(*transport, Responce::CreateError("������ �������� ���� ����� ������������ ��� �������"));
        return true;
    }

    if (cmd == "rename")
    {
        if (rest.empty())
        {
            Protocol::SendResponce(*transport, Responce::CreateError("/rename: ������� ����� ���"));
            return true;
        }

        fixed_string<CHATNAME_MAXLENGTH> newName;
        newName.assign(rest);
        Database::renameChat(chat, newName);

        std::optional<ChatInfo> refreshed = Database::findChatById(chat.Id);
        if (!refreshed.has_value())
        {
            Protocol::SendResponce(*transport, Responce::CreateError("��� �� ������"));
            return true;
        }

        Chat renamed = chatModelGroup();
        renamed.Name = refreshed->Name;

        Message ack = Database::commitSystemMessage(refreshed.value(), request.Timestamp,
            std::string("��� ������������ � \"") + std::string(refreshed->Name.c_str()) + "\"");
        Protocol::SendResponce(*transport, Responce::CreateCommitMessage(ack));

        Update u{};
        u.Type = UpdateType::ChatRenamed;
        u.ChatRenamed.ChatModel = renamed;

        for (const UserInfo& member : Database::findMembersByChat(refreshed.value()))
        {
            if (member.Id == requestor.Id)
                continue;
            pushUpdateToUser(member.Id, u);
        }

        return true;
    }

    const size_t tsp = rest.find(' ');
    const std::string_view targetTok = (tsp == std::string::npos) ? rest : trimSv(rest.substr(0, tsp));
    if (targetTok.empty())
    {
        Protocol::SendResponce(*transport, Responce::CreateError("������� @��� ��� $id"));
        return true;
    }

    std::optional<UserInfo> target;
    try
    {
        target = resolveUserToken(targetTok);
    }
    catch (...)
    {
        Protocol::SendResponce(*transport, Responce::CreateError("�������� $id"));
        return true;
    }

    if (!target.has_value())
    {
        Protocol::SendResponce(*transport, Responce::CreateError("������������ �� ������"));
        return true;
    }

    if (target->Id == SYSTEM_FROMID)
    {
        Protocol::SendResponce(*transport, Responce::CreateError("������ ��������� � ���������� ������������"));
        return true;
    }

    if (cmd == "unban")
    {
        if (!Database::isUserBannedFromChat(chat.Id, target->Id))
        {
            Protocol::SendResponce(*transport, Responce::CreateError("������������ �� � ����"));
            return true;
        }

        Database::unbanUserInChat(chat.Id, target->Id);

        Message ack = Database::commitSystemMessage(chat, request.Timestamp,
            std::string(target->Name.c_str()) + std::string(" ��������"));
        Protocol::SendResponce(*transport, Responce::CreateCommitMessage(ack));

        Update u{};
        u.Type = UpdateType::UserUnbanned;
        u.MemberEvent.ChatModel = chatModelGroup();
        u.MemberEvent.TargetUser = User{ .Id = target->Id, .Name = target->Name };

        std::unordered_set<userid_t> recipients;
        for (const UserInfo& m : Database::findMembersByChat(chat))
            recipients.insert(m.Id);

        recipients.insert(target->Id);
        for (userid_t uid : recipients)
        {
            if (uid == requestor.Id)
                continue;
            pushUpdateToUser(uid, u);
        }

        return true;
    }

    if (target->Id == chat.OwnerId)
    {
        Protocol::SendResponce(*transport, Responce::CreateError("������ ��������� � ��������� ����"));
        return true;
    }

    if (cmd == "kick")
    {
        if (!isUserInChat(*target, chat))
        {
            Protocol::SendResponce(*transport, Responce::CreateError("������������ �� � ����"));
            return true;
        }

        std::vector<UserInfo> members = Database::findMembersByChat(chat);
        Database::removeMemberFromChat(chat.Id, target->Id);

        Message ack = Database::commitSystemMessage(chat, request.Timestamp,
            std::string(target->Name.c_str()) + std::string(" �������� �� ����"));
        Protocol::SendResponce(*transport, Responce::CreateCommitMessage(ack));

        Update u{};
        u.Type = UpdateType::UserKicked;
        u.MemberEvent.ChatModel = chatModelGroup();
        u.MemberEvent.TargetUser = User{ .Id = target->Id, .Name = target->Name };

        for (const UserInfo& m : members)
        {
            if (m.Id == requestor.Id)
                continue;

            pushUpdateToUser(m.Id, u);
        }

        return true;
    }

    if (cmd == "ban")
    {
        std::vector<UserInfo> members = Database::findMembersByChat(chat);
        Database::banUserInChat(chat, *target);
        Database::removeMemberFromChat(chat.Id, target->Id);

        Message ack = Database::commitSystemMessage(chat, request.Timestamp,
            std::string(target->Name.c_str()) + std::string(" �������"));
        Protocol::SendResponce(*transport, Responce::CreateCommitMessage(ack));

        Update u{};
        u.Type = UpdateType::UserBanned;
        u.MemberEvent.ChatModel = chatModelGroup();
        u.MemberEvent.TargetUser = User{ .Id = target->Id, .Name = target->Name };

        std::unordered_set<userid_t> recipients;
        for (const UserInfo& m : members)
            recipients.insert(m.Id);

        recipients.insert(target->Id);
        for (userid_t uid : recipients)
        {
            if (uid == requestor.Id)
                continue;

            pushUpdateToUser(uid, u);
        }

        return true;
    }

    return false;
}

void Backend::start()
{
    IsActive = true;
}

void Backend::stop()
{
    IsActive = false;
}

bool Backend::isActive()
{
    return IsActive;
}

void Backend::addClientSocket(const Transport* transport)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    ClientSockets.push_back(transport);
}

void Backend::removeClientSocket(const Transport* transport)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    ClientSockets.erase(std::remove(ClientSockets.begin(), ClientSockets.end(), transport), ClientSockets.end());
    ClientUsers.erase(transport);
}

void Backend::assignUserInfo(const Transport* transport, userid_t userId)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    ClientUsers[transport] = userId;
}

void Backend::designUserInfo(const Transport* transport)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    ClientUsers.erase(transport);
}

std::optional<userid_t> Backend::findUserBySocket(const Transport* transport)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    for (const auto& user : ClientUsers)
    {
        if (user.first == transport)
            return user.second;
    }

    return std::nullopt;
}

std::optional<Transport*> Backend::findSocketByUser(const userid_t userId)
{
    std::lock_guard<std::recursive_mutex> credentialsLock(CredentialsMutex);
    for (const auto& user : ClientUsers)
    {
        if (user.second == userId)
            return const_cast<Transport*>(user.first);
    }

    return std::nullopt;
}

std::optional<ChatInfo> Backend::findChatByQuery(const UserInfo& requestor, const fixed_string<CHATNAME_MAXLENGTH>& chatQuery)
{
    switch (chatQuery.buffer[0])
    {
        // ������ ������������ �� ����� (��� ��)
        case '@':
        {
            std::optional<UserInfo> user = Database::findUserByName(std::string_view(chatQuery.buffer + 1));
            if (!user.has_value())
            {
                return std::nullopt;
            }

            std::optional<DirectChatInfo> chat = Database::findDirectChat(requestor.Id, user->Id);
            if (!chat.has_value())
            {
                chat = Database::createDirectChat(requestor, user.value());
                Log::Trace("Chats", std::string("Direct chat created: '") + requestor.Name.c_str() + "' <-> '" + user->Name.c_str() + "'");
            }

            chat->Name = user->Name;
            return chat;
        }

        // ������ ������������ �� ID (��� ��)
        case '$':
        {
            std::optional<UserInfo> user = Database::findUserById(std::stoul(chatQuery.buffer + 1));
            if (!user.has_value())
            {
                return std::nullopt;
            }

            std::optional<DirectChatInfo> chat = Database::findDirectChat(requestor.Id, user->Id);
            if (!chat.has_value())
            {
                chat = Database::createDirectChat(requestor, user.value());
                Log::Trace("Chats", std::string("Direct chat created: '") + requestor.Name.c_str() + "' <-> '" + user->Name.c_str() + "'");
            }

            chat->Name = user->Name;
            return chat;
        }

        // ������ ��������� ��� �� ����
        case '#':
        {
            std::optional<ChatInfo> chat = Database::findChatById(std::stoul(chatQuery.buffer + 1));
            if (isDirectChat(chat))
            {
                return std::nullopt;
            }

            return chat;
        }

        // ������ ��������� ��� �� �����
        default:
        {
            std::optional<ChatInfo> chat = Database::findChatByName(chatQuery);
            if (isDirectChat(chat))
            {
                return std::nullopt;
            }

            return chat;
        }
    }
}

void Backend::processRequest(const Transport* transport, const Request& request)
{
    try
    {
        switch (request.Type)
        {
            default:
            {
                Protocol::SendResponce(*transport, Responce::CreateError("unsupported request type"));
                return;
            }

            case RequestType::Register:
            {
                if (request.CommitMessage.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session.", ERR_EXPIRED));
                    return;
                }

                std::optional<UserInfo> userInfo = Database::findUserByName(request.Auth.Username);
                if (userInfo.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("User with such name already exists"));
                    return;
                }

                userInfo = Database::createUser(request.Auth.Username, request.Auth.Password);
                Protocol::SendResponce(*transport, Responce::CreateLogin(User{ .Id = userInfo->Id, .Name = userInfo->Name }));
                assignUserInfo(transport, userInfo.value().Id);

                std::string ip = SocketHelper::PeerIdentificatorOf(*transport) + ":" + std::to_string(SocketHelper::PeerPortOf(*transport));
                Log::Trace("Auth", std::string("User '") + userInfo->Name.c_str() + "' (" + std::to_string(userInfo->Id) + ") registered on " + ip);
                return;
            }

            case RequestType::Login:
            {
                if (request.CommitMessage.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session.", ERR_EXPIRED));
                    return;
                }

                if (request.Auth.Username.size() == 0)
                {
                    designUserInfo(transport);
                    Protocol::SendResponce(*transport, Responce::CreateLogin(User{}));
                    return;
                }

                std::optional<UserInfo> userInfo = Database::findUserByName(request.Auth.Username);
                if (!userInfo.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Account not found"));
                    return;
                }

                std::optional<Transport*> loggedUser = findSocketByUser(userInfo->Id);
                if (loggedUser.has_value())
                {
                    if (loggedUser.value()->TransportSocket != transport->TransportSocket)
                    {
                        Protocol::SendResponce(*transport, Responce::CreateError("This account is already logged in on another client"));
                        return;
                    }
                }

                if (!Database::verifyPassword(request.Auth.Password.buffer, userInfo->PasswordHash))
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid password"));
                    return;
                }
                
                Protocol::SendResponce(*transport, Responce::CreateLogin(User { .Id = userInfo->Id, .Name = userInfo->Name }));
                assignUserInfo(transport, userInfo->Id);

                std::string ip = SocketHelper::PeerIdentificatorOf(*transport) + ":" + std::to_string(SocketHelper::PeerPortOf(*transport));
                Log::Trace("Auth", std::string("User '") + userInfo->Name.c_str() + "' (" + std::to_string(userInfo->Id) + ") logged in on " + ip);
                return;
            }

            case RequestType::GetUpdates:
            {
                if (request.CommitMessage.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session.", ERR_EXPIRED));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account.", ERR_UNAUTHORIZED));
                    return;
                }

				if (request.GetUpdates.DropPending)
                {
                    std::lock_guard<std::mutex> updatesLock(UpdatesMutex[requestor->Id]);
                    UpdatesCache[requestor->Id].clear();
                    return;
                }

                Responce responce{};
                responce.Type = ResponceType::GetUpdates;

				std::vector<Update> updates;
                {
                    std::lock_guard<std::mutex> updatesLock(UpdatesMutex[requestor->Id]);
                    updates = std::vector<Update>(UpdatesCache[requestor->Id]);
                }

                uint32_t remainingUpdates = static_cast<uint32_t>(updates.size());
                if (remainingUpdates == 0)
                {
                    responce.GetUpdates.RemainingUpdates = -1;
                    Protocol::SendResponce(*transport, responce);
                    return;
                }

                for (std::vector<Update>::reverse_iterator riter = updates.rbegin(); riter != updates.rend(); riter++)
                {
                    responce.GetUpdates.CurrentUpdate = *riter;
                    responce.GetUpdates.RemainingUpdates = --remainingUpdates;
                    Protocol::SendResponce(*transport, responce);
                }

                std::lock_guard<std::mutex> updatesLock(UpdatesMutex[requestor->Id]);
                UpdatesCache[requestor->Id].clear();
                return;
            }

            case RequestType::GetChatInfo:
            {
                if (request.CommitMessage.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

				std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
				if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                std::optional<ChatInfo> chat = findChatByQuery(requestor.value(), request.GetChatInfo.ChatQuery);
                if (!chat.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Chat not found"));
                    return;
                }

                const bool direct = (chat->OwnerId == SYSTEM_FROMID);
                Protocol::SendResponce(*transport, Responce::CreateGetChatInfo(Chat{ .Id = chat->Id, .Name = chat->Name, .Type = direct ? ChatType::Direct : ChatType::Group }));
                return;
            }

            case RequestType::GetChatHistory:
            {
                if (request.CommitMessage.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                std::optional<ChatInfo> chat = Database::findChatById(request.GetChatHistory.ChatId);
                if (!chat.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Chat not found"));
                    return;
                }

                Responce responce{};
                responce.Type = ResponceType::GetChatHistory;

                std::vector<Message> messages = Database::findMessagesByChat(chat.value());
                if (messages.size() == 0)
                {
                    responce.GetChatHistory.RemainingMessages = -1;
                    Protocol::SendResponce(*transport, responce);
                    return;
                }

                uint32_t remainingMessages = static_cast<uint32_t>(messages.size());
                for (std::vector<Message>::reverse_iterator riter = messages.rbegin(); riter != messages.rend(); riter++)
                {
                    responce.GetChatHistory.CurrentMessage = *riter;
                    responce.GetChatHistory.RemainingMessages = --remainingMessages;
                    Protocol::SendResponce(*transport, responce);
                }

                return;
            }

            case RequestType::GetChatMembers:
            {
                if (request.GetChatMembers.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                std::optional<ChatInfo> chat = Database::findChatById(request.GetChatMembers.ChatId);
                if (!chat.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Chat not found"));
                    return;
                }

                if (!isUserInChat(requestor.value(), chat.value()))
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("You are not a member of this chat"));
                    return;
                }

                std::vector<UserInfo> members = Database::findMembersByChat(chat.value());
                members.erase(
                    std::remove_if(members.begin(), members.end(), [](const UserInfo& u) { return u.Id == SYSTEM_FROMID; }),
                    members.end());

                Responce responce{};
                responce.Type = ResponceType::GetChatMembers;

                if (members.size() == 0)
                {
                    responce.GetChatMembers.RemainingUsers = -1;
                    Protocol::SendResponce(*transport, responce);
                    return;
                }

                uint32_t remainingUsers = static_cast<uint32_t>(members.size());
                for (std::vector<UserInfo>::reverse_iterator riter = members.rbegin(); riter != members.rend(); riter++)
                {
                    responce.GetChatMembers.CurrentUser = User{ .Id = riter->Id, .Name = riter->Name };
                    responce.GetChatMembers.RemainingUsers = static_cast<int32_t>(--remainingUsers);
                    Protocol::SendResponce(*transport, responce);
                }

                return;
            }

            case RequestType::GetChatList:
            {
                if (request.GetChatList.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                std::vector<ChatInfo> chats = Database::findChatsByUser(requestor.value());

                Responce responce{};
                responce.Type = ResponceType::GetChatList;

                if (chats.size() == 0)
                {
                    responce.GetChatList.RemainingChats = -1;
                    Protocol::SendResponce(*transport, responce);
                    return;
                }

                uint32_t remaining = static_cast<uint32_t>(chats.size());
                for (std::vector<ChatInfo>::reverse_iterator riter = chats.rbegin(); riter != chats.rend(); riter++)
                {
                    Chat outChat{ .Id = riter->Id, .Name = riter->Name, .Type = (riter->OwnerId == SYSTEM_FROMID) ? ChatType::Direct : ChatType::Group };
                    if (outChat.Type == ChatType::Direct)
                    {
                        std::optional<UserInfo> peer = Database::findDirectChatPeer(riter->Id, requestor->Id);
                        if (peer.has_value())
                            outChat.Name = peer->Name;
                    }

                    responce.GetChatList.CurrentChat = outChat;
                    responce.GetChatList.RemainingChats = static_cast<int32_t>(--remaining);
                    Protocol::SendResponce(*transport, responce);
                }

                return;
            }

			case RequestType::GetAddonCommands:
			{
				if (request.GetAddonCommands.UserToken != transport->AccessToken)
				{
					Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
					return;
				}

				std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
				if (!requestor.has_value())
				{
					Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account.", ERR_UNAUTHORIZED));
					return;
				}

				const std::vector<std::string> cmds = AddonManager::ListRegisteredCommands();
				if (cmds.empty())
				{
					Responce empty{};
					empty.Type = ResponceType::GetAddonCommands;
					empty.GetAddonCommands.RemainingCommands = -1;
					empty.GetAddonCommands.CurrentCommand = "";
					Protocol::SendResponce(*transport, empty);
					return;
				}

				uint32_t remaining = static_cast<uint32_t>(cmds.size());
				for (auto it = cmds.rbegin(); it != cmds.rend(); ++it)
				{
					Protocol::SendResponce(*transport, Responce::CreateGetAddonCommand(*it, static_cast<int32_t>(--remaining)));
				}

				return;
			}

            case RequestType::SpawnGroupChat:
            {
                if (request.SpawnGroupChat.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                if (request.SpawnGroupChat.Chatname.empty())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Chat name is required"));
                    return;
                }

                if (Database::findChatByName(request.SpawnGroupChat.Chatname).has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("A chat with this name already exists"));
                    return;
                }

                UserInfo owner = requestor.value();
                ChatInfo created = Database::createGroupChat(owner, request.SpawnGroupChat.Chatname);

                Protocol::SendResponce(*transport, Responce::CreateCreateChat(Chat{ .Id = created.Id, .Name = created.Name, .Type = ChatType::Group }));
                Log::Trace("Chats", std::string("Group chat '") + created.Name.c_str() + "' (" + std::to_string(created.Id) + ") created by " + requestor->Name.c_str());
                return;
            }

            case RequestType::JoinGroupChat:
            {
                if (request.JoinGroupChat.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                std::optional<ChatInfo> chat = findChatByQuery(requestor.value(), request.JoinGroupChat.ChatQuery);
                if (!chat.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Group chat not found"));
                    return;
                }

                if (isDirectChat(chat))
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Use @user or $id to open a direct chat; /join is for group chats only"));
                    return;
                }

                if (Database::isUserBannedFromChat(chat->Id, requestor->Id))
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("You are banned from this chat"));
                    return;
                }

                if (!isUserInChat(requestor.value(), chat.value()))
                    Database::joinChatMember(chat.value(), requestor.value());

                std::optional<ChatInfo> refreshed = Database::findChatById(chat->Id);
                if (!refreshed.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Chat not found"));
                    return;
                }

                Protocol::SendResponce(*transport, Responce::CreateJoinChat(Chat{ .Id = refreshed->Id, .Name = refreshed->Name, .Type = ChatType::Group }));
                Log::Trace("Chats", std::string("User '") + requestor->Name.c_str() + "' joined group chat '" + refreshed->Name.c_str() + "' (" + std::to_string(refreshed->Id) + ")");
                return;
            }

            case RequestType::CommitMessage:
            {
                if (request.CommitMessage.UserToken != transport->AccessToken)
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Invalid access token. Refresh session."));
                    return;
                }

                std::optional<UserInfo> requestor = Database::findUserById(findUserBySocket(transport));
                if (!requestor.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Unauthorized. Login into account."));
                    return;
                }

                std::optional<ChatInfo> chat = Database::findChatById(request.CommitMessage.ToChat);
                if (!chat.has_value())
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("Chat not found."));
                    return;
                }

                if (!isUserInChat(requestor.value(), chat.value()))
                {
                    Protocol::SendResponce(*transport, Responce::CreateError("You are not a member of this chat"));
                    return;
                }

                /*
                bool commandExecuted = tryHandleSlashCommand(transport, requestor.value(), *const_cast<CommitMessageRequest*>(&request.CommitMessage), chat.value());
                if (commandExecuted)
                {
                    Message message = Database::commitMessage(requestor.value(), request.CommitMessage);
                    Protocol::SendResponce(*transport, Responce::CreateCommitMessage(message));
                    Log::Trace("Messages", std::string("Committed message id=") + std::to_string(static_cast<long long>(message.Id)) +
                        " chat=" + std::to_string(message.DestChat.Id) +
                        " from=" + std::to_string(message.From.Id));

                    return;
                }
                */

                Message message = Database::commitMessage(requestor.value(), request.CommitMessage);
                Protocol::SendResponce(*transport, Responce::CreateCommitMessage(message));
                Log::Trace("Messages", std::string("Committed message id=") + std::to_string(static_cast<long long>(message.Id)) +
                    " chat=" + std::to_string(message.DestChat.Id) +
                    " from=" + std::to_string(message.From.Id));

                Update update{};
                update.Type = UpdateType::Message;
                update.MessageSent = message;

				std::vector<UserInfo> members = Database::findMembersByChat(chat.value());
				for (const UserInfo& member : members)
                {
                    if (member.Id == requestor->Id)
                        continue;

                    std::lock_guard<std::mutex> updatesLock(UpdatesMutex[member.Id]);
					UpdatesCache[member.Id].push_back(update);
                }

                bool commandExecuted = tryHandleSlashCommand(transport, requestor.value(), *const_cast<CommitMessageRequest*>(&request.CommitMessage), chat.value());
                return;
            }
        }
    }
    catch (disconnected_error)
    {
        return;
    }
    catch (const std::runtime_error& err)
    {
        Log::Error("Backend", std::string("Runtime error: ") + err.what());
        Protocol::SendResponce(*transport, Responce::CreateError(err.what()));
    }
}
