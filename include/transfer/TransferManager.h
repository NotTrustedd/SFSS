#ifndef PBL_PHASE2_TRANSFER_MANAGER_H
#define PBL_PHASE2_TRANSFER_MANAGER_H

#include "network/Client.h"
#include "network/Server.h"
#include "security/EncryptionManager.h"
#include "security/FileManager.h"
#include "transfer/HistoryManager.h"
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

class TransferManager {
public:
    TransferManager(Server& server, HistoryManager& history);

    void sendFile(const std::string& sender,
                  const std::string& receiver,
                  const std::string& address,
                  unsigned short port,
                  const std::string& filePath,
                  const std::string& sessionKey);
    void sendFileChecked(const std::string& sender,
                         const std::string& receiver,
                         const std::string& address,
                         unsigned short port,
                         const std::string& filePath,
                         const std::string& sessionKey,
                         volatile LONG* cancelRequested = NULL);
    void processIncoming(const std::string& receiver);
    std::vector<IncomingOffer> pendingOffers() const;
    bool rejectIncoming(const IncomingOffer& offer, std::string& error);
    bool acceptIncoming(const IncomingOffer& offer,
                        const std::string& receiver,
                        const std::string& sessionKey,
                        const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                        std::string& error,
                        std::string& savedFile);

private:
    void record(const std::string& sender,
                const std::string& receiver,
                const std::string& filename,
                unsigned long long size,
                const std::string& status);

    Server& server_;
    HistoryManager& history_;
    Client client_;
    FileManager files_;
};

#endif
