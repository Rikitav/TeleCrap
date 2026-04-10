#pragma once
#include <array>
#include <cstdint>

#include "Models.h"
#include "SocketHelper.h"

// RAII контейнер для дексриптора сокета
// автоматически закроет подключение при уничтожении контейнера
// не может быть скопирован или передан по значению (чревато ужасными послежствиями)
/// <summary>
/// RAII-обертка над socket-дескриптором и токеном доступа соединения.
/// </summary>
class Transport
{
	friend class Protocol;

	struct SecureSession
	{
		bool Enabled = false;
		std::array<std::uint8_t, 32> TxKey{};
		std::array<std::uint8_t, 32> RxKey{};
		std::uint64_t TxCounter = 0;
		std::uint64_t RxCounter = 0;
	};

	Transport(const SOCKET transportSocket, const token_t accessToken);

public:
	const SOCKET TransportSocket;
	const token_t AccessToken;
	mutable SecureSession Secure;

	Transport(const Transport&) = delete;
	Transport& operator=(const Transport&) = delete;
	/// <summary>
	/// Закрывает сокет и освобождает связанный транспорт.
	/// </summary>
	~Transport();

	operator const SOCKET*() const { return &TransportSocket; }

	/// <summary>
	/// Инициализирует платформенный сетевой стек.
	/// </summary>
	static void Init();
	/// <summary>
	/// Создает серверный транспорт (listener для handshake).
	/// </summary>
	static Transport* Server();
	/// <summary>
	/// Создает клиентский транспорт и выполняет handshake с сервером.
	/// </summary>
	static Transport* Client();
};