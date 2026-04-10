#pragma once
#include <optional>
#include <string>
#include <vector>

#include <telecrap/Models.h>
#include <telecrap/Request.h>
#include <telecrap/Transport.h>
#include <telecrap/Update.h>

#include "ChatHistory.h"

struct OnlineClientInfo
{
    token_t AccessToken = 0;
    userid_t UserId = 0;
    bool HasUser = false;
    u_short PeerPort = 0;
    std::string PeerAddress;
};

/// <summary>
/// Центральный orchestrator серверной логики: управляет сессиями и маршрутизацией запросов.
/// </summary>
class Backend
{
public:
    /// <summary>
    /// Запускает backend и переводит его в состояние приема запросов.
    /// </summary>
    static void start();

    /// <summary>
    /// Останавливает backend и завершает обработку новых запросов.
    /// </summary>
    static void stop();

    /// <summary>
    /// Возвращает признак активного состояния backend.
    /// </summary>
	static bool isActive();

    /// <summary>
    /// Регистрирует сокет клиента в списке активных подключений.
    /// </summary>
    static void addClientSocket(const Transport* transport);

    /// <summary>
    /// Удаляет сокет клиента из списка активных подключений.
    /// </summary>
    static void removeClientSocket(const Transport* transport);

    /// <summary>
    /// Привязывает userId к транспорту после успешной авторизации.
    /// </summary>
    static void assignUserInfo(const Transport* transport, userid_t userId);

    /// <summary>
    /// Отвязывает пользователя от транспорта.
    /// </summary>
    static void designUserInfo(const Transport* transport);

    /// <summary>
    /// Ищет userId по транспорту активного подключения.
    /// </summary>
    static std::optional<userid_t> findUserBySocket(const Transport* transport);

    /// <summary>
    /// Ищет транспорт по userId онлайн-пользователя.
    /// </summary>
    static std::optional<Transport*> findSocketByUser(const userid_t userId);

    /// <summary>
    /// Разрешает поисковый запрос чата в конкретный ChatInfo для пользователя.
    /// </summary>
    static std::optional<ChatInfo> findChatByQuery(const UserInfo& requestor, const fixed_string<CHATNAME_MAXLENGTH>& chatQuery);
    
    /// <summary>
    /// Обрабатывает входящий Request от клиента и отправляет ответы/обновления.
    /// </summary>
	static void processRequest(const Transport* transport, const Request& request);

    /// <summary>
    /// Рассылает update всем участникам чата, кроме указанного пользователя.
    /// </summary>
    static void pushMessageToChatMembersExcept(const ChatInfo& chat, const Update& update, userid_t exceptUserId);

    /// <summary>
    /// Рассылает update всем участникоам чата, включая отправителя
    /// </summary>
    static void pushUpdateToChatMembersAll(const ChatInfo& chat, const Update& update);

    /// <summary>
    /// Возвращает снимок текущих сетевых подключений.
    /// </summary>
    static std::vector<OnlineClientInfo> listOnlineClients();

    /// <summary>
    /// Разрывает соединение по peer-port клиента.
    /// </summary>
    static bool dropConnectionByPeerPort(u_short peerPort);

    /// <summary>
    /// Рассылает системный alert всем авторизованным пользователям онлайн.
    /// </summary>
    static size_t pushGlobalAlert(const std::string& text);
};
