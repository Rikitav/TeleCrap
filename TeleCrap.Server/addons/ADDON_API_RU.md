# API для аддонов TeleCrap на Lua (Полный справочник)

Этот документ описывает полный программный интерфейс (API) для Lua-аддонов, предоставляемый сервером `TeleCrap.Server`.

Он включает в себя:
- Механизмы среды выполнения и модель загрузки
- Справочник моделей данных
- Глобальные функции
- Методы `CommandContext`
- Встроенные модули (`unsafe`, `state`, `http`)
- Полные примеры кода
- Модель безопасности и лучшие практики

---

## 1) Механизмы среды выполнения

### 1.1 Загрузка аддонов
- При запуске сервер создает директорию `addons` (если она отсутствует).
- Каждый файл `*.lua` в директории `addons` исполняется.
- Если в файле возникает ошибка, она записывается в лог, а запуск остальных скриптов продолжается.

### 1.2 Диспетчеризация команд
- Аддоны регистрируют команды со слэшем через `RegisterCommand(name, callback)`.
- Команда вызывается из чата как `/name аргументы...`.
- Обратный вызов (callback) получает один аргумент: `CommandContext`.

### 1.3 Модель исполнения
- Аддоны исполняются внутри серверного процесса.
- Состояние Lua (Lua state) является общим для всех аддонов.
- API допускает как безопасные, так и небезопасные операции.

---

## 2) Доступные стандартные библиотеки Lua

Открыты следующие стандартные библиотеки Lua:
- `base`
- `package`
- `math`
- `string`
- `table`

Следующие библиотеки **отключены**:
- `coroutine`
- `io`
- `os`
- `utf8`
- `debug`

Такая конфигурация намеренно разрешительная и считается небезопасной для недоверенного кода.

---

## 3) Типы данных, доступные в Lua

## `UserInfo`
- `Id: integer`
- `Name(): string`
- `PasswordHash: integer`

## `User`
- `Id: integer`
- `Name(): string`

## `ChatInfo`
- `Id: integer`
- `Name(): string`
- `OwnerId: integer`

## `Chat`
- `Id: integer`
- `Name(): string`
- `Type: ChatType`

## `Message`
- `Id: integer`
- `DestChat: Chat`
- `From: User`
- `Timestamp: integer`
- `Text: string`

## Перечисление `ChatType`
- `ChatType.Group`
- `ChatType.Direct`

---

## 4) Глобальные функции

## Логирование
- `LogInfo(tag, message)`
- `LogWarn(tag, message)`
- `LogError(tag, message)`
- `LogTrace(tag, message)`

Используйте их для диагностики аддонов и аудита действий.

## Время
- `UnixTimeMs() -> integer`

Возвращает время UNIX в миллисекундах.

## Регистрация команд
- `RegisterCommand(name, callback)`

Пример:
```lua
RegisterCommand("ping", function(ctx)
  ctx:SendSystemMessage("pong")
end)
```

## Поиск и работа с БД
- `FindUserById(userId) -> UserInfo|nil`
- `FindUserByName(username) -> UserInfo|nil`
- `FindChatById(chatId) -> ChatInfo|nil`
- `FindChatByName(chatName) -> ChatInfo|nil`
- `GetMembersByChatId(chatId) -> table<UserInfo>`
- `GetMessagesByChatId(chatId) -> table<Message>`
- `GetChatsByUserId(userId) -> table<ChatInfo>`
- `IsUserBannedFromChat(chatId, userId) -> boolean`
- `CommitSystemMessage(chatId, timestamp, text) -> Message|nil`

## Глобальные функции для "небезопасной" модификации чатов
- `CreateGroupChatUnsafe(ownerUserId, chatName) -> ChatInfo|nil`
- `JoinChatMemberUnsafe(chatId, userId) -> boolean`
- `BanUserInChatUnsafe(chatId, userId) -> boolean`
- `UnbanUserInChatUnsafe(chatId, userId) -> boolean`
- `RemoveMemberFromChatUnsafe(chatId, userId) -> boolean`
- `RenameChatUnsafe(chatId, newName) -> boolean`

Эти функции игнорируют стандартные проверки разрешений. Предназначены только для доверенной автоматизации.

---

## 5) Справочник CommandContext

Объект `CommandContext` передается в callback-функции зарегистрированных команд.

## Поля
- `ctx.Socket` (указатель на нативный транспорт, непрозрачный для скриптов)
- `ctx.Requestor` (`UserInfo`) — данные пользователя, вызвавшего команду.
- `ctx.Request` — входящий запрос на фиксацию сообщения.
- `ctx.Chat` (`ChatInfo`) — текущий чат.
- `ctx.Cmd` (`string`) — токен команды (например, `/ping`).
- `ctx.Args` (`string`) — необработанные аргументы команды.

## Сообщения
- `ctx:SendSystemMessage(text)`
  - Отправляет сообщение от имени SYSTEM в текущий чат.
- `ctx:SendMessage(text)`
  - Отправляет сообщение от имени автора запроса (Requestor) в текущий чат.
- `ctx:SendSystemMessageToChatId(chatId, text) -> Message|nil`
  - Отправляет системное сообщение в чат по указанному ID.

## Проверки чата и личности
- `ctx:IsDirectChat() -> boolean`
- `ctx:IsRequestorOwner() -> boolean`
- `ctx:IsRequestorMember() -> boolean`
- `ctx:IsChatOwner(userId) -> boolean`
- `ctx:IsChatMember(userId) -> boolean`

## Вспомогательные функции поиска
- `ctx:FindUserById(userId) -> UserInfo|nil`
- `ctx:FindUserByName(name) -> UserInfo|nil`
- `ctx:FindChatById(chatId) -> ChatInfo|nil`
- `ctx:FindChatByName(name) -> ChatInfo|nil`

## Работа с коллекциями
- `ctx:GetMembers() -> table<UserInfo>`
- `ctx:GetMessages() -> table<Message>`

## Инструменты модерации
- `ctx:IsUserBanned(userId) -> boolean`
- `ctx:KickUser(userId) -> boolean`
- `ctx:BanUser(userId) -> boolean`
- `ctx:UnbanUser(userId) -> boolean`
- `ctx:RenameChat(newName) -> boolean`

**Примечания:**
- Эти методы являются разрешительными и не всегда принудительно исполняют логику владения командами уровня приложения.
- Автор аддона должен самостоятельно выполнять явные проверки (например, является ли пользователь владельцем или админом).

---

## 6) Встроенные модули Lua

Сервер внедряет встроенные модули и регистрирует их как загруженные:
- `require("unsafe")`
- `require("state")`
- `require("http")`

Они также доступны как глобальные переменные: `unsafe`, `state`, `http`.

## 6.1 Модуль `state` (общее состояние в ОЗУ)

Данные сохраняются, пока жив процесс сервера.

**API:**
- `state.set(key, value)`
- `state.get(key) -> any|nil`
- `state.has(key) -> boolean`
- `state.del(key)`
- `state.clear()`
- `state.keys() -> table<string>`

**Примеры использования:**
- Кулдауны (задержки) для команд
- Переключатели функций (feature flags)
- Счетчики и легкое кэширование

Пример:
```lua
local state = require("state")

RegisterCommand("count", function(ctx)
  local key = "count:" .. tostring(ctx.Chat.Id)
  local current = state.get(key) or 0
  current = current + 1
  state.set(key, current)
  ctx:SendSystemMessage("Счетчик: " .. tostring(current))
end)
```

## 6.2 Модуль `http`

HTTP реализован через системные команды оболочки (небезопасно по дизайну).

**API:**
- `http.get(url) -> string` (тело ответа или пустая строка при ошибке)
- `http.post_json(url, jsonBody) -> string`
- `http.download_to_file(url, filePath) -> boolean`

**Примечания:**
- На Windows используется PowerShell `Invoke-WebRequest`.
- На других ОС используется `curl`.
- Выполнение аддона может блокироваться на время выполнения сетевого запроса.

Пример:
```lua
local http = require("http")

RegisterCommand("joke", function(ctx)
  local body = http.get("https://official-joke-api.appspot.com/random_joke")
  if body == "" then
    ctx:SendSystemMessage("HTTP запрос не удался")
    return
  end
  ctx:SendSystemMessage("Сырой JSON: " .. body)
end)
```

## 6.3 Модуль `unsafe` (в данный момент отключен, но существует)

Высокопривилегированное выполнение локальных команд и доступ к файловой системе.

**API:**
- `unsafe.exec(command) -> integer`
- `unsafe.exec_capture(command) -> string`
- `unsafe.eval(luaCode) -> result`
- `unsafe.dofile(path) -> result`
- `unsafe.read_file(path) -> string`
- `unsafe.write_file(path, content) -> boolean`
- `unsafe.append_file(path, content) -> boolean`
- `unsafe.delete_file(path) -> boolean`
- `unsafe.mkdir(path) -> boolean`
- `unsafe.rename_path(fromPath, toPath) -> boolean`
- `unsafe.list_dir(path) -> table<string>`
- `unsafe.exists(path) -> boolean`
- `unsafe.cwd() -> string`
- `unsafe.sleep_ms(milliseconds)`

**Критическое предупреждение:**
- Эти функции могут выполнять произвольные команды от имени пользователя, запустившего сервер.
- Считайте любые аргументы команд "загрязненными" (tainted).
- Никогда не предоставляйте доступ к этим функциям обычным пользователям чата напрямую.

---

## 7) Пример полноценного аддона

```lua
local state = require("state")
local http = require("http")

local function is_owner(ctx)
  return ctx:IsRequestorOwner()
end

RegisterCommand("echo", function(ctx)
  if ctx.Args == "" then
    ctx:SendSystemMessage("Использование: /echo <текст>")
    return
  end
  ctx:SendMessage(ctx.Args)
end)

RegisterCommand("members", function(ctx)
  local members = ctx:GetMembers()
  local names = {}
  for i = 1, #members do
    names[#names + 1] = members[i].Name
  end
  ctx:SendSystemMessage("Участники: " .. table.concat(names, ", "))
end)

RegisterCommand("renamex", function(ctx)
  if not is_owner(ctx) then
    ctx:SendSystemMessage("Только для владельца")
    return
  end
  if ctx.Args == "" then
    ctx:SendSystemMessage("Использование: /renamex <новое-имя>")
    return
  end
  local ok = ctx:RenameChat(ctx.Args)
  ctx:SendSystemMessage(ok and "Переименовано" or "Ошибка переименования")
end)

RegisterCommand("weatherraw", function(ctx)
  local city = ctx.Args ~= "" and ctx.Args or "London"
  local url = "https://wttr.in/" .. city .. "?format=3"
  local body = http.get(url)
  if body == "" then
    ctx:SendSystemMessage("Не удалось получить данные о погоде")
    return
  end
  ctx:SendSystemMessage(body)
end)

RegisterCommand("cooldown", function(ctx)
  local key = "cooldown:" .. tostring(ctx.Requestor.Id)
  local now = UnixTimeMs()
  local prev = state.get(key) or 0
  if now - prev < 10000 then
    ctx:SendSystemMessage("Подождите, действует кулдаун")
    return
  end
  state.set(key, now)
  ctx:SendSystemMessage("Действие выполнено")
end)
```

---

## 8) Модель безопасности и угрозы

Данное API намеренно обладает высокими привилегиями и поддерживает небезопасные операции.

**Основные риски:**
- Удаленное выполнение команд через `unsafe.exec*`.
- Произвольное чтение/запись/удаление файлов.
- Повреждение данных через небезопасные функции мутации чатов.
- Блокировка серверного потока при длительных операциях ввода-вывода или HTTP.
- Эскалация привилегий, если аддон слепо доверяет вводу из чата.

**Минимальные правила безопасности:**
- Ограничивайте опасные команды конкретными ID пользователей.
- Валидируйте и очищайте (sanitize) все входные данные из чата.
- Используйте кулдауны для ресурсозатратных операций.
- Ведите логи аудита для каждого опасного действия.
- Отдавайте предпочтение `SendSystemMessage` и неизменяемым хелперам для обычных функций.

---

## 9) Практические паттерны

### 9.1 Админ-команда с проверкой владельца
```lua
RegisterCommand("banx", function(ctx)
  if not ctx:IsRequestorOwner() then
    ctx:SendSystemMessage("Только для владельца")
    return
  end
  local targetId = tonumber(ctx.Args)
  if not targetId then
    ctx:SendSystemMessage("Использование: /banx <userId>")
    return
  end
  local ok = ctx:BanUser(targetId)
  ctx:SendSystemMessage(ok and "Забанен" or "Ошибка")
end)
```

### 9.2 Общий кэш
```lua
local state = require("state")
local function cache_get(key)
  return state.get("cache:" .. key)
end
local function cache_set(key, value)
  state.set("cache:" .. key, value)
end
```

---

## 10) Заметки о совместимости

- API привязано из C++ с помощью библиотеки **Sol2**.
- Таблицы, возвращаемые методами запросов, являются массивами Lua с индексацией от 1.
- Состояние `state` сохраняется при перезагрузке аддона, пока работает процесс.
- Поведение `http` и `unsafe` зависит от доступности системных утилит и оболочки ОС.

---

## 11) Рекомендуемые следующие шаги

- Создание манифеста разрешений для каждого аддона.
- Добавление очереди асинхронных задач для длительных HTTP/shell операций.
- Добавление безопасного модуля для кодирования/декодирования JSON.
- Добавление структурированных хуков событий (при запуске, при входе в чат, при сообщении и т.д.).