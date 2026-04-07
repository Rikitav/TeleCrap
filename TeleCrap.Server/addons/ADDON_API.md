# TeleCrap Lua Addon API (Complete Reference)

This document describes the full Lua addon API exposed by `TeleCrap.Server`.

It includes:
- Runtime mechanics and loading model
- Data model reference
- Global functions
- `CommandContext` methods
- Built-in modules (`unsafe`, `state`, `http`)
- Full examples
- Security model and best practices

---

## 1) Runtime Mechanics

### 1.1 Addon loading
- Server creates `addons` directory on startup (if missing).
- Every `*.lua` file in `addons` is executed.
- If a file throws, error is logged and startup continues for other scripts.

### 1.2 Command dispatch
- Addons register slash commands through `RegisterCommand(name, callback)`.
- Command is invoked from chat as `/name args...`.
- Callback receives one argument: `CommandContext`.

### 1.3 Execution model
- Addons execute inside server process.
- Lua state is shared for all addons.
- API allows both safe and unsafe operations.

---

## 2) Available Lua Standard Libraries

The following standard Lua libraries are opened:
- `base`
- `package`
- `math`
- `string`
- `table`

The following standard Lua libraries are disabled:
- `coroutine`
- `io`
- `os`
- `utf8`
- `debug`

This is intentionally permissive and considered unsafe for untrusted code.

---

## 3) Data Types Exposed to Lua

## `UserInfo`
- `Id: integer`
- `Name(): string`

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

## `ChatType` enum
- `ChatType.Group`
- `ChatType.Direct`

---

## 4) Global Functions

## Logging
- `LogInfo(tag, message)`
- `LogWarn(tag, message)`
- `LogError(tag, message)`
- `LogTrace(tag, message)`

Use these for addon diagnostics and audit trails.

## Time
- `UnixTimeMs() -> integer`

Returns UNIX time in milliseconds.

## Command registration
- `RegisterCommand(name, callback)`

Example:
```lua
RegisterCommand("ping", function(ctx)
  ctx:SendSystemMessage("pong")
end)
```

## Lookup / DB helpers
- `FindUserById(userId) -> UserInfo|nil`
- `FindUserByName(username) -> UserInfo|nil`
- `FindChatById(chatId) -> ChatInfo|nil`
- `FindChatByName(chatName) -> ChatInfo|nil`
- `GetMembersByChatId(chatId) -> table<UserInfo>`
- `GetMessagesByChatId(chatId) -> table<Message>`
- `GetChatsByUserId(userId) -> table<ChatInfo>`
- `IsUserBannedFromChat(chatId, userId) -> boolean`
- `CommitSystemMessage(chatId, timestamp, text) -> Message|nil`

## Unsafe chat mutation globals
- `CreateGroupChatUnsafe(ownerUserId, chatName) -> ChatInfo|nil`
- `JoinChatMemberUnsafe(chatId, userId) -> boolean`
- `BanUserInChatUnsafe(chatId, userId) -> boolean`
- `UnbanUserInChatUnsafe(chatId, userId) -> boolean`
- `RemoveMemberFromChatUnsafe(chatId, userId) -> boolean`
- `RenameChatUnsafe(chatId, newName) -> boolean`

These bypass normal permission checks. Intended for trusted automation only.

---

## 5) CommandContext Reference

`CommandContext` is passed into registered command callbacks.

## Fields
- `ctx.Socket` (native transport pointer, opaque for scripts)
- `ctx.Requestor` (`UserInfo`)
- `ctx.Request` (incoming commit-message request)
- `ctx.Chat` (`ChatInfo`)
- `ctx.Cmd` (`string`) command token (for example `/ping`)
- `ctx.Args` (`string`) raw command arguments

## Messaging
- `ctx:SendSystemMessage(text)`
  - Commits a SYSTEM-authored message to current chat and pushes update.
- `ctx:SendMessage(text)`
  - Commits requestor-authored message to current chat and pushes update.
- `ctx:SendSystemMessageToChatId(chatId, text) -> Message|nil`
  - Sends system message to arbitrary chat id.

## Chat / identity checks
- `ctx:IsDirectChat() -> boolean`
- `ctx:IsRequestorOwner() -> boolean`
- `ctx:IsRequestorMember() -> boolean`
- `ctx:IsChatOwner(userId) -> boolean`
- `ctx:IsChatMember(userId) -> boolean`

## Lookup helpers
- `ctx:FindUserById(userId) -> UserInfo|nil`
- `ctx:FindUserByName(name) -> UserInfo|nil`
- `ctx:FindChatById(chatId) -> ChatInfo|nil`
- `ctx:FindChatByName(name) -> ChatInfo|nil`

## Collection helpers
- `ctx:GetMembers() -> table<UserInfo>`
- `ctx:GetMessages() -> table<Message>`

## Moderation helpers
- `ctx:IsUserBanned(userId) -> boolean`
- `ctx:KickUser(userId) -> boolean`
- `ctx:BanUser(userId) -> boolean`
- `ctx:UnbanUser(userId) -> boolean`
- `ctx:RenameChat(newName) -> boolean`

Notes:
- These methods are permissive and do not enforce all app-level command ownership policy.
- Addon author should perform checks explicitly (owner/admin checks).

---

## 6) Built-In Lua Modules

The server injects built-in modules and registers them as loaded:
- `require("unsafe")`
- `require("state")`
- `require("http")`

They are also available as globals:
- `unsafe`
- `state`
- `http`

## 6.1 `state` module (in-RAM shared state)

Persistent while server process is alive.

API:
- `state.set(key, value)`
- `state.get(key) -> any|nil`
- `state.has(key) -> boolean`
- `state.del(key)`
- `state.clear()`
- `state.keys() -> table<string>`

Use cases:
- per-command cooldowns
- feature flags
- counters and lightweight caches

Example:
```lua
local state = require("state")

RegisterCommand("count", function(ctx)
  local key = "count:" .. tostring(ctx.Chat.Id)
  local current = state.get(key) or 0
  current = current + 1
  state.set(key, current)
  ctx:SendSystemMessage("Counter: " .. tostring(current))
end)
```

## 6.2 `http` module

HTTP implemented via shell-backed system commands (unsafe by design).

API:
- `http.get(url) -> string` (response body or empty string on failure)
- `http.post_json(url, jsonBody) -> string`
- `http.download_to_file(url, filePath) -> boolean`

Notes:
- On Windows uses PowerShell `Invoke-WebRequest`.
- On non-Windows uses `curl`.
- This can block addon execution while network request runs.

Example:
```lua
local http = require("http")

RegisterCommand("joke", function(ctx)
  local body = http.get("https://official-joke-api.appspot.com/random_joke")
  if body == "" then
    ctx:SendSystemMessage("HTTP request failed")
    return
  end
  ctx:SendSystemMessage("Raw JSON: " .. body)
end)
```

## 6.3 `unsafe` module (currently disabled, but its exists)

High-privilege local execution and filesystem access.

API:
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

Critical warning:
- These functions can execute arbitrary commands as server process user.
- Treat all command arguments as tainted.
- Never expose these directly to untrusted chat users.

Example:
```lua
local unsafe = require("unsafe")

RegisterCommand("whereami", function(ctx)
  ctx:SendSystemMessage("CWD: " .. unsafe.cwd())
end)
```

---

## 7) Example Addon: Full Featured

```lua
local state = require("state")
local http = require("http")

local function is_owner(ctx)
  return ctx:IsRequestorOwner()
end

RegisterCommand("echo", function(ctx)
  if ctx.Args == "" then
    ctx:SendSystemMessage("Usage: /echo <text>")
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
  ctx:SendSystemMessage("Members: " .. table.concat(names, ", "))
end)

RegisterCommand("renamex", function(ctx)
  if not is_owner(ctx) then
    ctx:SendSystemMessage("Owner only")
    return
  end
  if ctx.Args == "" then
    ctx:SendSystemMessage("Usage: /renamex <new-name>")
    return
  end
  local ok = ctx:RenameChat(ctx.Args)
  ctx:SendSystemMessage(ok and "Renamed" or "Rename failed")
end)

RegisterCommand("weatherraw", function(ctx)
  local city = ctx.Args ~= "" and ctx.Args or "London"
  local url = "https://wttr.in/" .. city .. "?format=3"
  local body = http.get(url)
  if body == "" then
    ctx:SendSystemMessage("Failed to fetch weather")
    return
  end
  ctx:SendSystemMessage(body)
end)

RegisterCommand("cooldown", function(ctx)
  local key = "cooldown:" .. tostring(ctx.Requestor.Id)
  local now = UnixTimeMs()
  local prev = state.get(key) or 0
  if now - prev < 10000 then
    ctx:SendSystemMessage("Cooldown active")
    return
  end
  state.set(key, now)
  ctx:SendSystemMessage("Action executed")
end)
```

---

## 8) Security Model and Threat Notes

This API is intentionally high-privilege and supports unsafe operations.

Main risks:
- Remote command execution via `unsafe.exec*`
- Arbitrary file read/write/delete
- Data corruption via unsafe chat mutation functions
- Blocking server thread on long I/O or HTTP
- Privilege escalation if addon trusts chat input blindly

Minimum safe practices:
- Restrict dangerous commands to specific user IDs.
- Validate and sanitize all chat-provided input.
- Rate-limit and cooldown expensive operations.
- Keep audit logs for every dangerous action.
- Prefer `SendSystemMessage` and read-only helpers for normal features.

---

## 9) Practical Patterns

## 9.1 Owner-guarded admin command
```lua
RegisterCommand("banx", function(ctx)
  if not ctx:IsRequestorOwner() then
    ctx:SendSystemMessage("Owner only")
    return
  end
  local targetId = tonumber(ctx.Args)
  if not targetId then
    ctx:SendSystemMessage("Usage: /banx <userId>")
    return
  end
  local ok = ctx:BanUser(targetId)
  ctx:SendSystemMessage(ok and "Banned" or "Failed")
end)
```

## 9.2 Shared cache
```lua
local state = require("state")
local function cache_get(key)
  return state.get("cache:" .. key)
end
local function cache_set(key, value)
  state.set("cache:" .. key, value)
end
```

## 9.3 Optional user lookup
```lua
RegisterCommand("idof", function(ctx)
  local u = FindUserByName(ctx.Args)
  if not u then
    ctx:SendSystemMessage("Not found")
    return
  end
  ctx:SendSystemMessage("User id: " .. tostring(u.Id))
end)
```

---

## 10) Compatibility Notes

- API is bound from C++ with Sol2.
- Tables returned by query methods are 1-based Lua arrays.
- `state` survives addon reload only while process is running.
- `http` and `unsafe` behavior depends on OS shell/tool availability.

---

## 11) Suggested Next Steps

- Create per-addon permission manifest.
- Add asynchronous job queue for long-running HTTP/shell tasks.
- Add safe JSON encode/decode helper module.
- Add structured event hooks (on startup, on join, on message, etc.).
