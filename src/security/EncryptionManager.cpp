#include "security/EncryptionManager.h"
#include <windows.h>
#include <wincrypt.h>
#include <cstdlib>
#include <stdexcept>

#ifndef CALG_SHA_256
#define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | 12)
#endif

namespace {
std::string toHex(const BYTE* bytes, DWORD count) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(static_cast<std::size_t>(count) * 2);
    for (DWORD i = 0; i < count; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0f]);
    }
    return result;
}

std::string sha256(const std::string& data) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        throw std::runtime_error("Could not initialize Windows cryptography.");
    }
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        throw std::runtime_error("Could not create SHA-256 hash.");
    }
    const BOOL hashed = data.empty() ||
        CryptHashData(hash, reinterpret_cast<BYTE*>(const_cast<char*>(data.data())),
                      static_cast<DWORD>(data.size()), 0);
    BYTE digest[32];
    DWORD digestSize = sizeof(digest);
    const BOOL retrieved = hashed &&
        CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!retrieved || digestSize != sizeof(digest)) {
        throw std::runtime_error("Could not calculate SHA-256 hash.");
    }
    return toHex(digest, digestSize);
}

std::string hmacSha256(const std::string& key, const std::string& message) {
    std::string normalizedKey = key;
    if (normalizedKey.size() > 64) {
        const std::string hex = sha256(normalizedKey);
        normalizedKey.clear();
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            normalizedKey.push_back(static_cast<char>(
                std::strtoul(hex.substr(i, 2).c_str(), NULL, 16)));
        }
    }
    normalizedKey.resize(64, '\0');
    std::string innerPad(64, '\0');
    std::string outerPad(64, '\0');
    for (std::size_t i = 0; i < 64; ++i) {
        innerPad[i] = static_cast<char>(normalizedKey[i] ^ 0x36);
        outerPad[i] = static_cast<char>(normalizedKey[i] ^ 0x5c);
    }

    std::string inner = sha256(innerPad + message);
    std::string innerBytes;
    innerBytes.reserve(inner.size() / 2);
    for (std::size_t i = 0; i < inner.size(); i += 2) {
        innerBytes.push_back(static_cast<char>(
            std::strtoul(inner.substr(i, 2).c_str(), NULL, 16)));
    }
    const std::string outer = sha256(outerPad + innerBytes);
    return outer;
}
}

std::string EncryptionManager::randomHex(std::size_t byteCount) {
    if (byteCount == 0 || byteCount > 1024) {
        throw std::invalid_argument("Random byte count is out of range.");
    }
    HCRYPTPROV provider = 0;
    if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        throw std::runtime_error("Could not initialize Windows random generator.");
    }
    std::vector<BYTE> bytes(byteCount);
    const BOOL generated = CryptGenRandom(provider, static_cast<DWORD>(bytes.size()), bytes.data());
    CryptReleaseContext(provider, 0);
    if (!generated) {
        throw std::runtime_error("Could not generate random bytes.");
    }
    return toHex(bytes.data(), static_cast<DWORD>(bytes.size()));
}

std::string EncryptionManager::passwordHash(const std::string& salt,
                                             const std::string& password) {
    return sha256(salt + password);
}

std::string EncryptionManager::sessionProof(const std::string& sessionKey,
                                             const std::string& nonce) {
    return hmacSha256(sessionKey, nonce);
}

bool EncryptionManager::verifySessionKey(const std::string& sessionKey,
                                          const std::string& nonce,
                                          const std::string& proof) {
    const std::string expected = sessionProof(sessionKey, nonce);
    if (expected.size() != proof.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(expected[i] ^ proof[i]);
    }
    return difference == 0;
}

std::vector<std::uint8_t> EncryptionManager::xorTransform(
    const std::vector<std::uint8_t>& data, const std::string& key) {
    if (key.empty()) {
        throw std::invalid_argument("Session key cannot be empty.");
    }
    std::vector<std::uint8_t> result(data);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] ^= static_cast<std::uint8_t>(key[i % key.size()]);
    }
    return result;
}
