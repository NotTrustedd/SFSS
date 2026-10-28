#ifndef PBL_PHASE2_ENCRYPTION_MANAGER_H
#define PBL_PHASE2_ENCRYPTION_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

class EncryptionManager {
public:
    static std::string randomHex(std::size_t byteCount);
    static std::string passwordHash(const std::string& salt, const std::string& password);
    static std::string sessionProof(const std::string& sessionKey, const std::string& nonce);
    static bool verifySessionKey(const std::string& sessionKey,
                                 const std::string& nonce,
                                 const std::string& proof);
    static std::vector<std::uint8_t> xorTransform(const std::vector<std::uint8_t>& data,
                                                   const std::string& key);
};

#endif
