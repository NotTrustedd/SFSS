#include "network/Server.h"
#include "network/Protocol.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <ws2tcpip.h>

namespace {
class CriticalGuard {
public:
    explicit CriticalGuard(CRITICAL_SECTION& section) : section_(section) {
        EnterCriticalSection(&section_);
    }
    ~CriticalGuard() {
        LeaveCriticalSection(&section_);
    }
private:
    CRITICAL_SECTION& section_;
};

std::atomic<unsigned long> nextOfferId(1);

bool safeIncomingFilename(const std::string& filename) {
    if (filename.empty() || filename.size() > 180 || filename == "." ||
        filename == ".." || filename[0] == '.') {
        return false;
    }
    for (std::size_t i = 0; i < filename.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(filename[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
              c == '_' || c == '-' || c == '(' || c == ')')) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> splitLines(const std::string& value) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = value.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(value.substr(start));
            break;
        }
        lines.push_back(value.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

void closeAndRemoveSocket(std::vector<SOCKET>& sockets,
                         CRITICAL_SECTION& mutex,
                         SOCKET socket) {
    CriticalGuard lock(mutex);
    Protocol::closeSocket(socket);
    for (std::vector<SOCKET>::iterator it = sockets.begin(); it != sockets.end(); ++it) {
        if (*it == socket) {
            sockets.erase(it);
            break;
        }
    }
}
}

struct Server::PendingRequest {
    IncomingOffer offer;
    CRITICAL_SECTION mutex;
    HANDLE decisionEvent;
    HANDLE finishedEvent;
    bool decisionReady;
    bool accepted;
    bool finished;
    std::function<void(std::uint64_t, std::uint64_t)> progress;
    ReceivedTransfer result;

    PendingRequest() : decisionReady(false), accepted(false), finished(false) {
        InitializeCriticalSection(&mutex);
        decisionEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        finishedEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (decisionEvent == NULL || finishedEvent == NULL) {
            if (decisionEvent != NULL) {
                CloseHandle(decisionEvent);
            }
            if (finishedEvent != NULL) {
                CloseHandle(finishedEvent);
            }
            DeleteCriticalSection(&mutex);
            throw std::runtime_error("Could not create transfer synchronization events.");
        }
        result.success = false;
    }

    ~PendingRequest() {
        CloseHandle(decisionEvent);
        CloseHandle(finishedEvent);
        DeleteCriticalSection(&mutex);
    }
};

struct Server::ConnectionArgs {
    Server* server;
    SOCKET socket;
};

Server::Server()
    : listener_(INVALID_SOCKET), stopping_(false), running_(false), port_(0),
      acceptThread_(NULL) {
    InitializeCriticalSection(&threadsMutex_);
    InitializeCriticalSection(&requestsMutex_);
    InitializeCriticalSection(&socketsMutex_);
}

Server::~Server() {
    stop();
    DeleteCriticalSection(&socketsMutex_);
    DeleteCriticalSection(&requestsMutex_);
    DeleteCriticalSection(&threadsMutex_);
}

unsigned __stdcall Server::acceptThreadEntry(void* context) {
    static_cast<Server*>(context)->acceptLoop();
    return 0;
}

unsigned __stdcall Server::connectionThreadEntry(void* context) {
    std::unique_ptr<ConnectionArgs> args(static_cast<ConnectionArgs*>(context));
    args->server->handleConnection(args->socket);
    return 0;
}

void Server::start(unsigned short port, const std::string& username) {
    if (running_) {
        throw std::runtime_error("Peer listener is already running.");
    }
    if (!Protocol::validToken(username)) {
        throw std::invalid_argument("Invalid local username.");
    }

    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        throw std::runtime_error(Protocol::socketError("socket"));
    }
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in address;
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        const std::string error = Protocol::socketError("bind");
        Protocol::closeSocket(listener);
        throw std::runtime_error(error);
    }
    if (listen(listener, SOMAXCONN) == SOCKET_ERROR) {
        const std::string error = Protocol::socketError("listen");
        Protocol::closeSocket(listener);
        throw std::runtime_error(error);
    }

    listener_ = listener;
    port_ = port;
    username_ = username;
    stopping_ = false;
    running_ = true;
    unsigned int threadId = 0;
    const uintptr_t thread = _beginthreadex(
        NULL, 0, &Server::acceptThreadEntry, this, 0, &threadId);
    if (thread == 0) {
        const std::string error = "Could not start the Winsock listener thread.";
        running_ = false;
        Protocol::closeSocket(listener_);
        listener_ = INVALID_SOCKET;
        throw std::runtime_error(error);
    }
    acceptThread_ = reinterpret_cast<HANDLE>(thread);
}

void Server::stop() {
    stopping_ = true;
    if (listener_ != INVALID_SOCKET) {
        shutdown(listener_, SD_BOTH);
        Protocol::closeSocket(listener_);
    }

    {
        CriticalGuard requestsLock(requestsMutex_);
        for (std::unordered_map<std::string, std::shared_ptr<PendingRequest> >::iterator it =
                 requests_.begin(); it != requests_.end(); ++it) {
            CriticalGuard requestLock(it->second->mutex);
            if (!it->second->decisionReady) {
                it->second->decisionReady = true;
                it->second->accepted = false;
            }
            SetEvent(it->second->decisionEvent);
        }
    }
    {
        CriticalGuard socketsLock(socketsMutex_);
        for (std::size_t i = 0; i < activeSockets_.size(); ++i) {
            shutdown(activeSockets_[i], SD_BOTH);
        }
    }

    if (acceptThread_ != NULL) {
        WaitForSingleObject(acceptThread_, INFINITE);
        CloseHandle(acceptThread_);
        acceptThread_ = NULL;
    }
    listener_ = INVALID_SOCKET;
    {
        CriticalGuard threadsLock(threadsMutex_);
        for (std::size_t i = 0; i < connectionThreads_.size(); ++i) {
            WaitForSingleObject(connectionThreads_[i], INFINITE);
            CloseHandle(connectionThreads_[i]);
        }
        connectionThreads_.clear();
    }
    {
        CriticalGuard requestsLock(requestsMutex_);
        requestQueue_ = std::queue<std::shared_ptr<PendingRequest> >();
        requests_.clear();
    }
    running_ = false;
    port_ = 0;
    username_.clear();
}

bool Server::isRunning() const {
    return running_;
}

unsigned short Server::port() const {
    return port_;
}

std::size_t Server::pendingCount() const {
    CriticalGuard lock(requestsMutex_);
    std::size_t count = 0;
    std::queue<std::shared_ptr<PendingRequest> > snapshot = requestQueue_;
    while (!snapshot.empty()) {
        const std::shared_ptr<PendingRequest> request = snapshot.front();
        {
            CriticalGuard requestLock(request->mutex);
            if (!request->finished && !request->decisionReady) {
                ++count;
            }
        }
        snapshot.pop();
    }
    return count;
}

std::vector<IncomingOffer> Server::pendingOffers() const {
    CriticalGuard lock(requestsMutex_);
    std::vector<IncomingOffer> offers;
    std::queue<std::shared_ptr<PendingRequest> > snapshot = requestQueue_;
    while (!snapshot.empty()) {
        const std::shared_ptr<PendingRequest> request = snapshot.front();
        {
            CriticalGuard requestLock(request->mutex);
            if (!request->finished && !request->decisionReady) {
                offers.push_back(request->offer);
            }
        }
        snapshot.pop();
    }
    return offers;
}

bool Server::takePending(IncomingOffer& offer) {
    CriticalGuard lock(requestsMutex_);
    while (!requestQueue_.empty()) {
        const std::shared_ptr<PendingRequest> request = requestQueue_.front();
        requestQueue_.pop();
        bool finished;
        {
            CriticalGuard requestLock(request->mutex);
            finished = request->finished;
        }
        if (finished) {
            requests_.erase(request->offer.id);
            continue;
        }
        offer = request->offer;
        return true;
    }
    return false;
}

void Server::resolve(
    const std::string& id, bool accept,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    std::shared_ptr<PendingRequest> request;
    {
        CriticalGuard lock(requestsMutex_);
        const std::unordered_map<std::string, std::shared_ptr<PendingRequest> >::iterator it =
            requests_.find(id);
        if (it == requests_.end()) {
            throw std::runtime_error("Incoming request is no longer available.");
        }
        request = it->second;
    }
    {
        CriticalGuard lock(request->mutex);
        if (request->decisionReady) {
            throw std::runtime_error("Incoming request was already answered.");
        }
        request->accepted = accept;
        request->progress = progress;
        request->decisionReady = true;
        SetEvent(request->decisionEvent);
    }
}

ReceivedTransfer Server::waitForTransfer(const std::string& id) {
    std::shared_ptr<PendingRequest> request;
    {
        CriticalGuard lock(requestsMutex_);
        const std::unordered_map<std::string, std::shared_ptr<PendingRequest> >::iterator it =
            requests_.find(id);
        if (it == requests_.end()) {
            throw std::runtime_error("Incoming request is no longer available.");
        }
        request = it->second;
    }
    if (WaitForSingleObject(request->finishedEvent, INFINITE) != WAIT_OBJECT_0) {
        throw std::runtime_error("Could not wait for the incoming transfer.");
    }
    ReceivedTransfer result;
    {
        CriticalGuard lock(request->mutex);
        result = request->result;
    }
    {
        CriticalGuard lock(requestsMutex_);
        requests_.erase(id);
    }
    return result;
}

void Server::acceptLoop() {
    while (!stopping_) {
        {
            CriticalGuard lock(threadsMutex_);
            for (std::vector<HANDLE>::iterator it = connectionThreads_.begin();
                 it != connectionThreads_.end();) {
                if (WaitForSingleObject(*it, 0) == WAIT_OBJECT_0) {
                    CloseHandle(*it);
                    it = connectionThreads_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener_, &readable);
        timeval timeout = {0, 250000};
        const int ready = select(0, &readable, NULL, NULL, &timeout);
        if (ready == 0) {
            continue;
        }
        if (ready == SOCKET_ERROR) {
            if (!stopping_) {
                std::cerr << Protocol::socketError("select") << '\n';
                running_ = false;
            }
            break;
        }
        sockaddr_in peerAddress;
        int peerLength = sizeof(peerAddress);
        const SOCKET client = accept(listener_,
            reinterpret_cast<sockaddr*>(&peerAddress), &peerLength);
        if (client == INVALID_SOCKET) {
            if (!stopping_) {
                std::cerr << Protocol::socketError("accept") << '\n';
                running_ = false;
            }
            break;
        }
        if (stopping_) {
            Protocol::closeSocket(client);
            break;
        }
        DWORD initialTimeout = 15000;
        if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&initialTimeout),
                       sizeof(initialTimeout)) == SOCKET_ERROR) {
            std::cerr << Protocol::socketError("setsockopt receive timeout") << '\n';
            Protocol::closeSocket(client);
            continue;
        }
        {
            CriticalGuard lock(socketsMutex_);
            activeSockets_.push_back(client);
        }
        ConnectionArgs* args = new ConnectionArgs();
        args->server = this;
        args->socket = client;
        unsigned int threadId = 0;
        const uintptr_t thread = _beginthreadex(
            NULL, 0, &Server::connectionThreadEntry, args, 0, &threadId);
        if (thread == 0) {
            delete args;
            std::cerr << "Could not start a peer connection handler.\n";
            closeAndRemoveSocket(activeSockets_, socketsMutex_, client);
            continue;
        }
        {
            CriticalGuard lock(threadsMutex_);
            connectionThreads_.push_back(reinterpret_cast<HANDLE>(thread));
        }
    }
}

void Server::handleConnection(SOCKET socket) {
    std::shared_ptr<PendingRequest> request;
    ReceivedTransfer result;
    result.success = false;
    try {
        const std::string rawOffer = Protocol::receiveFrame(socket);
        if (rawOffer == "PING") {
            Protocol::sendFrame(socket, "PONG");
            closeAndRemoveSocket(activeSockets_, socketsMutex_, socket);
            return;
        }
        const std::vector<std::string> fields = splitLines(rawOffer);
        if (fields.size() != 7) {
            throw std::runtime_error("Transfer offer must contain exactly seven fields.");
        }
        if (fields[0] != "OFFER") {
            throw std::runtime_error("Transfer offer is missing the OFFER command.");
        }
        if (!Protocol::validToken(fields[1])) {
            throw std::runtime_error("Transfer offer contains an invalid sender username.");
        }
        if (fields[2] != username_) {
            throw std::runtime_error("Offer is addressed to '" + fields[2] +
                                     "', but this listener belongs to '" + username_ + "'.");
        }
        if (!safeIncomingFilename(fields[3])) {
            throw std::runtime_error("Transfer offer contains an unsupported filename.");
        }
        if (fields[5].size() != 32) {
            throw std::runtime_error("Transfer offer contains an invalid session nonce.");
        }
        if (fields[6].size() != 64) {
            throw std::runtime_error("Transfer offer contains an invalid session proof.");
        }
        std::istringstream sizeInput(fields[4]);
        unsigned long long size = 0;
        sizeInput >> size;
        if (!sizeInput || !sizeInput.eof() || size > Protocol::MAX_TRANSFER_SIZE) {
            throw std::runtime_error("Invalid transfer size.");
        }

        request.reset(new PendingRequest());
        request->offer.id = std::to_string(nextOfferId.fetch_add(1));
        request->offer.sender = fields[1];
        request->offer.receiver = fields[2];
        request->offer.filename = fields[3];
        request->offer.fileSize = size;
        request->offer.nonce = fields[5];
        request->offer.proof = fields[6];
        {
            CriticalGuard lock(requestsMutex_);
            if (stopping_) {
                throw std::runtime_error("Listener is stopping.");
            }
            try {
                requests_[request->offer.id] = request;
                requestQueue_.push(request);
            } catch (...) {
                requests_.erase(request->offer.id);
                throw;
            }
        }

        for (;;) {
            const DWORD state = WaitForSingleObject(request->decisionEvent, 250);
            if (state == WAIT_OBJECT_0) {
                break;
            }
            if (state != WAIT_TIMEOUT) {
                throw std::runtime_error("Could not wait for transfer approval.");
            }
            if (stopping_) {
                break;
            }
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket, &readable);
            timeval timeout = {0, 0};
            const int ready = select(0, &readable, NULL, NULL, &timeout);
            if (ready == SOCKET_ERROR) {
                throw std::runtime_error(Protocol::socketError("select"));
            }
            if (ready > 0) {
                const std::string control = Protocol::receiveFrame(socket);
                if (control.empty()) {
                    throw std::runtime_error("Sender cancelled before receiver approval.");
                }
                throw std::runtime_error("Unexpected data before receiver approval.");
            }
        }
        {
            CriticalGuard lock(request->mutex);
            if (stopping_ && !request->decisionReady) {
                request->decisionReady = true;
                request->accepted = false;
            }
        }
        if (!request->accepted) {
            Protocol::sendFrame(socket, "REJECT");
            result.error = "Transfer request rejected.";
        } else {
            DWORD transferTimeout = 30000;
            if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&transferTimeout),
                           sizeof(transferTimeout)) == SOCKET_ERROR ||
                setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                           reinterpret_cast<const char*>(&transferTimeout),
                           sizeof(transferTimeout)) == SOCKET_ERROR) {
                throw std::runtime_error(Protocol::socketError("setsockopt transfer timeout"));
            }
            Protocol::sendFrame(socket, "READY");
            std::vector<std::uint8_t> received;
            received.reserve(static_cast<std::size_t>(request->offer.fileSize));
            while (received.size() < request->offer.fileSize) {
                const std::string chunk = Protocol::receiveFrame(socket);
                if (chunk.empty()) {
                    throw std::runtime_error("Sender cancelled during transfer.");
                }
                const std::size_t remaining =
                    static_cast<std::size_t>(request->offer.fileSize) - received.size();
                if (chunk.size() > remaining) {
                    throw std::runtime_error("Received an invalid file chunk.");
                }
                received.insert(received.end(), chunk.begin(), chunk.end());
                if (request->progress) {
                    request->progress(received.size(), request->offer.fileSize);
                }
            }
            const std::string completion = Protocol::receiveFrame(socket);
            if (completion.empty()) {
                throw std::runtime_error("Sender cancelled during transfer.");
            }
            if (completion != "DONE") {
                throw std::runtime_error("Sender did not finish the transfer correctly.");
            }
            Protocol::sendFrame(socket, "DONE");
            result.encryptedFile.swap(received);
            result.success = true;
        }
    } catch (const std::exception& error) {
        if (request) {
            result.success = false;
            result.error = error.what();
            if (result.error == "Transfer cancelled by receiver.") {
                try {
                    Protocol::sendFrame(socket, "CANCELLED");
                } catch (const std::exception&) {
                }
            }
        } else if (!stopping_) {
            std::cerr << "Rejected connection: " << error.what() << '\n';
            try {
                Protocol::sendFrame(socket, std::string("ERROR\n") + error.what());
            } catch (const std::exception& responseError) {
                std::cerr << "Could not report connection error to peer: "
                          << responseError.what() << '\n';
            }
        }
    }

    if (request) {
        {
            CriticalGuard lock(request->mutex);
            request->result = result;
            request->finished = true;
            SetEvent(request->finishedEvent);
        }
    }
    closeAndRemoveSocket(activeSockets_, socketsMutex_, socket);
}
