#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class SecureChannel
{
public:
    struct KeyPair
    {
        std::array<std::uint8_t, 32> PrivateKey{};
        std::array<std::uint8_t, 32> PublicKey{};
    };

    static KeyPair GenerateX25519KeyPair();
    static std::array<std::uint8_t, 32> DeriveSharedSecret(
        const std::array<std::uint8_t, 32>& privateKey,
        const std::uint8_t peerPublicKey[32]);

    static void DeriveDirectionalKeys(
        const std::array<std::uint8_t, 32>& sharedSecret,
        std::uint64_t integrityTag,
        bool isServer,
        std::array<std::uint8_t, 32>& txKeyOut,
        std::array<std::uint8_t, 32>& rxKeyOut);

    static std::vector<std::uint8_t> EncryptAead(
        const std::array<std::uint8_t, 32>& key,
        std::uint64_t counter,
        const std::uint8_t* plaintext,
        std::size_t plaintextSize,
        std::array<std::uint8_t, 16>& tagOut);

    static std::vector<std::uint8_t> DecryptAead(
        const std::array<std::uint8_t, 32>& key,
        std::uint64_t counter,
        const std::uint8_t* ciphertext,
        std::size_t ciphertextSize,
        const std::uint8_t tag[16]);
};
