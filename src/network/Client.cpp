#include "network/Client.h"
#include "network/Protocol.h"
#include <iostream>
#include <conio.h>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <windows.h>

namespace {
struct CancelMonitorArgs {
    SOCKET socket;
    volatile LONG* cancelled;
    volatile LONG* cancelRequested;
    volatile LONG* finished;
    CRITICAL_SECTION* sendMutex;
};

void sendFrameLocked(SOCKET socket,
                     const std::string& payload,
                     CRITICAL_SECTION& mutex) {
    EnterCriticalSection(&mutex);
    try {
        Protocol::sendFrame(socket, payload);
    } catch (...) {
        LeaveCriticalSection(&mutex);
        throw;
    }
    LeaveCriticalSection(&mutex);
}

unsigned __stdcall cancelMonitor(void* context) {
    CancelMonitorArgs* args = static_cast<CancelMonitorArgs*>(context);
    while (InterlockedCompareExchange(args->finished, 0, 0) == 0) {
        bool cancel = args->cancelRequested != NULL &&
            InterlockedCompareExchange(args->cancelRequested, 0, 0) != 0;
        if (!cancel && _kbhit()) {
            const int key = _getch();
            cancel = key == 'c' || key == 'C';
        }
        if (cancel) {
            InterlockedExchange(args->cancelled, 1);
            EnterCriticalSection(args->sendMutex);
            try {
                Protocol::sendFrame(args->socket, std::string());
            } catch (...) {
            }
            LeaveCriticalSection(args->sendMutex);
            shutdown(args->socket, SD_BOTH);
            break;
        }
        Sleep(50);
    }
    return 0;
}

SOCKET connectTo(const std::string& address, unsigned short port) {
    sockaddr_in destination;
    std::memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = inet_addr(address.c_str());
    if (destination.sin_addr.s_addr == INADDR_NONE) {
        throw std::runtime_error("Enter a valid dotted-decimal IPv4 address.");
    }

    SOCKET socket = INVALID_SOCKET;
    socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        throw std::runtime_error(Protocol::socketError("socket"));
    }
    if (connect(socket, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) ==
        SOCKET_ERROR) {
        const std::string error = Protocol::socketError("connect");
        Protocol::closeSocket(socket);
        socket = INVALID_SOCKET;
        throw std::runtime_error(error);
    }
    return socket;
}

void printProgress(std::size_t sent, std::size_t total) {
    const std::size_t percent = total == 0 ? 100 : sent * 100 / total;
    const std::size_t filled = percent / 5;
    std::cout << '\r' << '[' << std::string(filled, '#') << std::string(20 - filled, ' ')
              << "] " << percent << "% (" << sent << '/' << total << " bytes)"
              << std::flush;
}
}

bool Client::isReachable(const std::string& address, unsigned short port) const {
    SOCKET socket = INVALID_SOCKET;
    try {
        socket = connectTo(address, port);
        Protocol::sendFrame(socket, "PING");
        const bool available = Protocol::receiveFrame(socket) == "PONG";
        Protocol::closeSocket(socket);
        return available;
    } catch (const std::exception&) {
        Protocol::closeSocket(socket);
        return false;
    }
}

void Client::sendTransfer(const std::string& address,
                          unsigned short port,
                          const std::string& sender,
                          const std::string& receiver,
                          const std::string& filename,
                          const std::string& nonce,
                          const std::string& proof,
                          const std::vector<std::uint8_t>& encryptedFile,
                          volatile LONG* cancelRequested) const {
    SOCKET socket = connectTo(address, port);
    volatile LONG cancelled = 0;
    volatile LONG monitorFinished = 0;
    CRITICAL_SECTION sendMutex;
    InitializeCriticalSection(&sendMutex);
    HANDLE monitorHandle = NULL;
    try {
        std::ostringstream offer;
        offer << "OFFER\n" << sender << '\n' << receiver << '\n' << filename << '\n'
              << encryptedFile.size() << '\n' << nonce << '\n' << proof;
        Protocol::sendFrame(socket, offer.str());
    } catch (...) {
        Protocol::closeSocket(socket);
        DeleteCriticalSection(&sendMutex);
        throw;
    }

    CancelMonitorArgs monitorArgs = {
        socket, &cancelled, cancelRequested, &monitorFinished, &sendMutex
    };
    unsigned int threadId = 0;
    const uintptr_t monitorThread = _beginthreadex(
        NULL, 0, &cancelMonitor, &monitorArgs, 0, &threadId);
    if (monitorThread == 0) {
        Protocol::closeSocket(socket);
        DeleteCriticalSection(&sendMutex);
        throw std::runtime_error("Could not start transfer cancellation monitor.");
    }
    monitorHandle = reinterpret_cast<HANDLE>(monitorThread);
    try {
        std::cout << "Press C to cancel this transfer.\n";
        const std::string response = Protocol::receiveFrame(socket);
        if (InterlockedCompareExchange(&cancelled, 0, 0) != 0) {
            throw std::runtime_error("Transfer cancelled by sender.");
        }
        if (response == "CANCELLED") {
            throw std::runtime_error("Transfer cancelled by receiver.");
        }
        if (response.compare(0, 6, "ERROR\n") == 0) {
            throw std::runtime_error("Receiver rejected transfer request: " +
                                     response.substr(6));
        }
        if (response != "READY") {
            throw std::runtime_error(response == "REJECT"
                ? "The receiver rejected the transfer or session key."
                : "Unexpected response from the receiver.");
        }

        const std::size_t chunkSize = Protocol::MAX_FRAME_SIZE;
        std::size_t sent = 0;
        while (sent < encryptedFile.size()) {
            const std::size_t amount = (encryptedFile.size() - sent < chunkSize)
                ? encryptedFile.size() - sent : chunkSize;
            const std::string chunk(
                reinterpret_cast<const char*>(encryptedFile.data() + sent), amount);
            sendFrameLocked(socket, chunk, sendMutex);
            sent += amount;
            printProgress(sent, encryptedFile.size());
        }
        if (encryptedFile.empty()) {
            printProgress(0, 0);
        }
        std::cout << '\n';
        sendFrameLocked(socket, "DONE", sendMutex);
        const std::string completion = Protocol::receiveFrame(socket);
        if (completion == "CANCELLED") {
            throw std::runtime_error("Transfer cancelled by receiver.");
        }
        if (completion != "DONE") {
            throw std::runtime_error("Receiver did not confirm transfer completion.");
        }
        InterlockedExchange(&monitorFinished, 1);
        WaitForSingleObject(monitorHandle, INFINITE);
        CloseHandle(monitorHandle);
        Protocol::closeSocket(socket);
        DeleteCriticalSection(&sendMutex);
    } catch (...) {
        InterlockedExchange(&monitorFinished, 1);
        WaitForSingleObject(monitorHandle, INFINITE);
        CloseHandle(monitorHandle);
        Protocol::closeSocket(socket);
        DeleteCriticalSection(&sendMutex);
        if (InterlockedCompareExchange(&cancelled, 0, 0) != 0) {
            throw std::runtime_error("Transfer cancelled by sender.");
        }
        throw;
    }
}
