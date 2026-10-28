#include "network/Protocol.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <ws2tcpip.h>

WinsockRuntime::WinsockRuntime() {
    WSADATA data;
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        throw std::runtime_error("Winsock startup failed with code " + std::to_string(result));
    }
}

WinsockRuntime::~WinsockRuntime() {
    WSACleanup();
}

namespace Protocol {
namespace {
void transferAll(SOCKET socket, char* data, std::size_t size, bool sending) {
    std::size_t offset = 0;
    while (offset < size) {
        const int amount = static_cast<int>(size - offset);
        const int result = sending
            ? send(socket, data + offset, amount, 0)
            : recv(socket, data + offset, amount, 0);
        if (result == SOCKET_ERROR || result == 0) {
            throw std::runtime_error(socketError(sending ? "send" : "receive"));
        }
        offset += static_cast<std::size_t>(result);
    }
}
}

void sendFrame(SOCKET socket, const std::string& payload) {
    if (payload.size() > MAX_FRAME_SIZE) {
        throw std::runtime_error("Network frame exceeds the 64 KiB limit.");
    }
    std::uint32_t length = htonl(static_cast<std::uint32_t>(payload.size()));
    transferAll(socket, reinterpret_cast<char*>(&length), sizeof(length), true);
    if (!payload.empty()) {
        transferAll(socket, const_cast<char*>(payload.data()), payload.size(), true);
    }
}

std::string receiveFrame(SOCKET socket) {
    std::uint32_t networkLength = 0;
    transferAll(socket, reinterpret_cast<char*>(&networkLength), sizeof(networkLength), false);
    const std::uint32_t length = ntohl(networkLength);
    if (length > MAX_FRAME_SIZE) {
        throw std::runtime_error("Received network frame exceeds the 64 KiB limit.");
    }
    std::string payload(length, '\0');
    if (length != 0) {
        transferAll(socket, &payload[0], length, false);
    }
    return payload;
}

void closeSocket(SOCKET socket) {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
    }
}

std::string socketError(const std::string& operation) {
    return operation + " failed (Winsock error " + std::to_string(WSAGetLastError()) + ").";
}

std::string currentTimestamp() {
    const std::time_t now = std::time(NULL);
    const std::tm* converted = std::localtime(&now);
    if (converted == NULL) {
        throw std::runtime_error("Could not read the local date and time.");
    }
    const std::tm local = *converted;
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

bool validToken(const std::string& token) {
    if (token.empty() || token.size() > 32) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        const char c = token[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}
}
