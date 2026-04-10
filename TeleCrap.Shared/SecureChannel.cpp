#include "pch.h"

#include <cstring>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>

#include "telecrap/SecureChannel.h"

static std::array<std::uint8_t, 12> buildNonce(std::uint64_t counter)
{
    std::array<std::uint8_t, 12> nonce{};
    nonce[4] = static_cast<std::uint8_t>((counter >> 56) & 0xFF);
    nonce[5] = static_cast<std::uint8_t>((counter >> 48) & 0xFF);
    nonce[6] = static_cast<std::uint8_t>((counter >> 40) & 0xFF);
    nonce[7] = static_cast<std::uint8_t>((counter >> 32) & 0xFF);
    nonce[8] = static_cast<std::uint8_t>((counter >> 24) & 0xFF);
    nonce[9] = static_cast<std::uint8_t>((counter >> 16) & 0xFF);
    nonce[10] = static_cast<std::uint8_t>((counter >> 8) & 0xFF);
    nonce[11] = static_cast<std::uint8_t>(counter & 0xFF);
    return nonce;
}

SecureChannel::KeyPair SecureChannel::GenerateX25519KeyPair()
{
    KeyPair out{};

    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (pctx == nullptr)
        throw std::runtime_error("OpenSSL: EVP_PKEY_CTX_new_id failed");

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen_init(pctx) <= 0 || EVP_PKEY_keygen(pctx, &pkey) <= 0)
    {
        EVP_PKEY_CTX_free(pctx);
        throw std::runtime_error("OpenSSL: X25519 keygen failed");
    }
    EVP_PKEY_CTX_free(pctx);

    size_t pubLen = out.PublicKey.size();
    if (EVP_PKEY_get_raw_public_key(pkey, out.PublicKey.data(), &pubLen) <= 0 || pubLen != out.PublicKey.size())
    {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("OpenSSL: get raw public key failed");
    }

    size_t privLen = out.PrivateKey.size();
    if (EVP_PKEY_get_raw_private_key(pkey, out.PrivateKey.data(), &privLen) <= 0 || privLen != out.PrivateKey.size())
    {
        EVP_PKEY_free(pkey);
        throw std::runtime_error("OpenSSL: get raw private key failed");
    }

    EVP_PKEY_free(pkey);
    return out;
}

std::array<std::uint8_t, 32> SecureChannel::DeriveSharedSecret(
    const std::array<std::uint8_t, 32>& privateKey,
    const std::uint8_t peerPublicKey[32])
{
    std::array<std::uint8_t, 32> out{};

    EVP_PKEY* priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, privateKey.data(), privateKey.size());
    EVP_PKEY* pub = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPublicKey, 32);
    if (priv == nullptr || pub == nullptr)
    {
        if (priv != nullptr) EVP_PKEY_free(priv);
        if (pub != nullptr) EVP_PKEY_free(pub);
        throw std::runtime_error("OpenSSL: create raw X25519 key failed");
    }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(priv, nullptr);
    if (ctx == nullptr || EVP_PKEY_derive_init(ctx) <= 0 || EVP_PKEY_derive_set_peer(ctx, pub) <= 0)
    {
        EVP_PKEY_free(priv);
        EVP_PKEY_free(pub);
        if (ctx != nullptr) EVP_PKEY_CTX_free(ctx);
        throw std::runtime_error("OpenSSL: X25519 derive init failed");
    }

    size_t outLen = out.size();
    if (EVP_PKEY_derive(ctx, out.data(), &outLen) <= 0 || outLen != out.size())
    {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(priv);
        EVP_PKEY_free(pub);
        throw std::runtime_error("OpenSSL: X25519 derive failed");
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(pub);
    return out;
}

void SecureChannel::DeriveDirectionalKeys(
    const std::array<std::uint8_t, 32>& sharedSecret,
    std::uint64_t integrityTag,
    bool isServer,
    std::array<std::uint8_t, 32>& txKeyOut,
    std::array<std::uint8_t, 32>& rxKeyOut)
{
    std::array<std::uint8_t, 8> salt{};
    salt[0] = static_cast<std::uint8_t>((integrityTag >> 56) & 0xFF);
    salt[1] = static_cast<std::uint8_t>((integrityTag >> 48) & 0xFF);
    salt[2] = static_cast<std::uint8_t>((integrityTag >> 40) & 0xFF);
    salt[3] = static_cast<std::uint8_t>((integrityTag >> 32) & 0xFF);
    salt[4] = static_cast<std::uint8_t>((integrityTag >> 24) & 0xFF);
    salt[5] = static_cast<std::uint8_t>((integrityTag >> 16) & 0xFF);
    salt[6] = static_cast<std::uint8_t>((integrityTag >> 8) & 0xFF);
    salt[7] = static_cast<std::uint8_t>(integrityTag & 0xFF);

    const char* info = "SecureCrapOverTCP-X25519-AES256GCM-v1";
    std::array<std::uint8_t, 64> okm{};

    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (kdf == nullptr)
        throw std::runtime_error("OpenSSL: HKDF fetch failed");

    EVP_KDF_CTX* kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (kctx == nullptr)
        throw std::runtime_error("OpenSSL: HKDF context failed");

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, const_cast<std::uint8_t*>(sharedSecret.data()), sharedSecret.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt.data(), salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, const_cast<char*>(info), std::strlen(info)),
        OSSL_PARAM_construct_end()
    };

    if (EVP_KDF_derive(kctx, okm.data(), okm.size(), params) <= 0)
    {
        EVP_KDF_CTX_free(kctx);
        throw std::runtime_error("OpenSSL: HKDF derive failed");
    }
    EVP_KDF_CTX_free(kctx);

    if (isServer)
    {
        std::memcpy(txKeyOut.data(), okm.data() + 32, 32);
        std::memcpy(rxKeyOut.data(), okm.data(), 32);
    }
    else
    {
        std::memcpy(txKeyOut.data(), okm.data(), 32);
        std::memcpy(rxKeyOut.data(), okm.data() + 32, 32);
    }
}

std::vector<std::uint8_t> SecureChannel::EncryptAead(
    const std::array<std::uint8_t, 32>& key,
    std::uint64_t counter,
    const std::uint8_t* plaintext,
    std::size_t plaintextSize,
    std::array<std::uint8_t, 16>& tagOut)
{
    const std::array<std::uint8_t, 12> nonce = buildNonce(counter);
    std::vector<std::uint8_t> ciphertext(plaintextSize);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
        throw std::runtime_error("OpenSSL: cipher ctx alloc failed");

    int outLen = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) <= 0 ||
        EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) <= 0 ||
        EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen, plaintext, static_cast<int>(plaintextSize)) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("OpenSSL: AES-GCM encrypt failed");
    }

    int finalLen = 0;
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + outLen, &finalLen) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tagOut.size()), tagOut.data()) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("OpenSSL: AES-GCM finalize failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(static_cast<std::size_t>(outLen + finalLen));
    return ciphertext;
}

std::vector<std::uint8_t> SecureChannel::DecryptAead(
    const std::array<std::uint8_t, 32>& key,
    std::uint64_t counter,
    const std::uint8_t* ciphertext,
    std::size_t ciphertextSize,
    const std::uint8_t tag[16])
{
    const std::array<std::uint8_t, 12> nonce = buildNonce(counter);
    std::vector<std::uint8_t> plaintext(ciphertextSize);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
        throw std::runtime_error("OpenSSL: cipher ctx alloc failed");

    int outLen = 0;
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) <= 0 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) <= 0 ||
        EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), nonce.data()) <= 0 ||
        EVP_DecryptUpdate(ctx, plaintext.data(), &outLen, ciphertext, static_cast<int>(ciphertextSize)) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("OpenSSL: AES-GCM decrypt failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, const_cast<std::uint8_t*>(tag)) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("OpenSSL: AES-GCM set tag failed");
    }

    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + outLen, &finalLen) <= 0)
    {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("OpenSSL: integrity check failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(static_cast<std::size_t>(outLen + finalLen));
    return plaintext;
}
