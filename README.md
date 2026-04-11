# TeleCrap

Клиент-серверный чат с терминальным интерфейсом (TUI) и собственным бинарным протоколом поверх TCP - "Secure Crap over TCP" (сокращенно "SCoT"). Сервер хранит данные в SQLite, поддерживает Lua-аддоны; общий слой (`TeleCrap.Shared`) отвечает за сокеты, сериализацию запросов/ответов и защищённый канал на базе OpenSSL (X25519, HKDF, AEAD).

## Состав репозитория

| Каталог | Назначение |
|---------|------------|
| `TeleCrap.Shared` | Протокол, `Transport`, `SocketHelper`, `SecureChannel`, логирование, консоль |
| `TeleCrap.Server` | TCP-сервер, БД, бизнес-логика, менеджер аддонов, CLI |
| `TeleCrap.Client` | TUI-клиент, локальный кэш чатов |
| `third-party` | SQLite, Lua 5.4 + sol2, исходники OpenSSL (сборка через CMake), при необходимости PDCurses для TUI на Windows |

## Требования

- **CMake** 3.20 или новее  
- **Компилятор C++20** (MSVC с поддержкой `/std:c++20` или GCC/Clang)  
- **Perl** — для конфигурации и сборки встроенного OpenSSL из `third-party/openssl-3.6.2`  
- **Windows (сборка OpenSSL из исходников):** Visual Studio Build Tools, `nmake` в `PATH`  
- **Linux:** `make`, стандартный набор build-essential; для TUI с ncurses — пакет разработки `ncurses` (если включён бэкенд curses, см. ниже)

При отсутствии каталога с исходниками OpenSSL в ожидаемом пути CMake попытается найти системный OpenSSL (`find_package`); иначе конфигурация завершится с подсказкой.

## Сборка

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Артефакты по умолчанию:

- сервер: `TeleCrap_server` (см. `OUTPUT_NAME` в `TeleCrap.Server/CMakeLists.txt`);
- клиент: `telecrap_tui`.

На Windows удобно открыть корень репозитория в Visual Studio и собрать сгенерированное решение.

### Опции CMake (клиент TUI)

В корневом `CMakeLists.txt` заданы опции:

- `TELECRAP_TUI_USE_CURSES` — TUI на ncurses (Unix) или PDCurses (Windows при `TELECRAP_USE_BUNDLED_PDCURSES`).  
- `TELECRAP_USE_BUNDLED_PDCURSES` — собирать встроенный PDCurses для Windows.

## Конфигурация

### Сервер (`TeleCrap.Server/settings.cfg`)

| Параметр | Описание |
|----------|----------|
| `ListenPort` | Порт прослушивания |
| `DbConnection` | Путь к файлу SQLite |

Файл должен лежать рядом с исполняемым файлом сервера (или скопирован туда post-build шагом CMake).

### Клиент (`TeleCrap.Client/settings.cfg`)

| Секция / ключ | Описание |
|-----------------|----------|
| `Network` / `ServerIP`, `ServerPort` | Адрес и порт сервера |
| `User` / `Username`, `Password` | Учётные данные (можно оставить пустыми и ввести в процессе работы, если так заложено в клиенте) |

## Запуск

1. Запустить сервер с корректным `settings.cfg` и доступной БД.  
2. Запустить клиент `telecrap_tui`, указав тот же порт, что и у сервера.

Подсказки по клавишам и командам отображаются в строке статуса TUI (в т.ч. выход по ESC, `/help`, история ввода).

## Лицензия и сторонний код

В `third-party` лежат исходники сторонних библиотек на их условиях. Перед распространением бинарника проверьте лицензии OpenSSL, SQLite, Lua, PDCurses/ncurses и прочих зависимостей.
