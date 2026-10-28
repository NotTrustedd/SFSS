#ifndef PBL_PHASE2_PROTOCOL_H
#define PBL_PHASE2_PROTOCOL_H

#include <string>
#include <winsock2.h>

class WinsockRuntime {
public:
    WinsockRuntime();
    ~WinsockRuntime();
    WinsockRuntime(const WinsockRuntime&) = delete;
    WinsockRuntime& operator=(const WinsockRuntime&) = delete;
};

namespace Protocol {
    const unsigned int MAX_FRAME_SIZE = 65536;
    const unsigned long long MAX_TRANSFER_SIZE = 64ULL * 1024ULL * 1024ULL;

    void sendFrame(SOCKET socket, const std::string& payload);
    std::string receiveFrame(SOCKET socket);
    void closeSocket(SOCKET socket);
    std::string socketError(const std::string& operation);
    std::string currentTimestamp();
    bool validToken(const std::string& token);
}

#endif
