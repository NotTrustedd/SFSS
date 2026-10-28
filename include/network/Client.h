#ifndef PBL_PHASE2_CLIENT_H
#define PBL_PHASE2_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

class Client {
public:
    bool isReachable(const std::string& address, unsigned short port) const;
    void sendTransfer(const std::string& address,
                      unsigned short port,
                      const std::string& sender,
                      const std::string& receiver,
                      const std::string& filename,
                      const std::string& nonce,
                      const std::string& proof,
                      const std::vector<std::uint8_t>& encryptedFile,
                      volatile LONG* cancelRequested = NULL) const;
};

#endif
