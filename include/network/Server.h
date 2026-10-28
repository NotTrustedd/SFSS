#ifndef PBL_PHASE2_SERVER_H
#define PBL_PHASE2_SERVER_H

#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <process.h>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

struct IncomingOffer {
    std::string id;
    std::string sender;
    std::string receiver;
    std::string filename;
    std::string nonce;
    std::string proof;
    std::uint64_t fileSize;
};

struct ReceivedTransfer {
    std::vector<std::uint8_t> encryptedFile;
    std::string error;
    bool success;
};

class Server {
public:
    Server();
    ~Server();

    void start(unsigned short port, const std::string& username);
    void stop();
    bool isRunning() const;
    unsigned short port() const;
    std::size_t pendingCount() const;
    std::vector<IncomingOffer> pendingOffers() const;
    bool takePending(IncomingOffer& offer);
    void resolve(const std::string& id,
                 bool accept,
                 const std::function<void(std::uint64_t, std::uint64_t)>& progress =
                     std::function<void(std::uint64_t, std::uint64_t)>());
    ReceivedTransfer waitForTransfer(const std::string& id);

private:
    struct PendingRequest;
    struct ConnectionArgs;
    static unsigned __stdcall acceptThreadEntry(void* context);
    static unsigned __stdcall connectionThreadEntry(void* context);
    void acceptLoop();
    void handleConnection(SOCKET socket);

    SOCKET listener_;
    std::atomic<bool> stopping_;
    std::atomic<bool> running_;
    unsigned short port_;
    std::string username_;
    HANDLE acceptThread_;
    CRITICAL_SECTION threadsMutex_;
    std::vector<HANDLE> connectionThreads_;
    mutable CRITICAL_SECTION requestsMutex_;
    std::queue<std::shared_ptr<PendingRequest> > requestQueue_;
    std::unordered_map<std::string, std::shared_ptr<PendingRequest> > requests_;
    CRITICAL_SECTION socketsMutex_;
    std::vector<SOCKET> activeSockets_;
};

#endif
