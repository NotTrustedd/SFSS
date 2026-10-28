#include "auth/Authentication.h"
#include "network/Protocol.h"
#include "network/Server.h"
#include "security/EncryptionManager.h"
#include "security/FileManager.h"
#include "transfer/HistoryManager.h"
#include "transfer/TransferManager.h"
#include <fstream>
#include <iostream>
#include <process.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {

struct ReceiverContext {
    Server* server;
    TransferManager* transfers;
    HANDLE finished;
    std::string key;
    std::string error;
    std::string savedFile;
    bool success;
    bool reject;

    ReceiverContext()
        : server(NULL), transfers(NULL), finished(NULL), success(false),
          reject(false) {}
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

unsigned __stdcall acceptTransfer(void* argument) {
    ReceiverContext* context = static_cast<ReceiverContext*>(argument);
    try {
        IncomingOffer offer;
        bool available = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            std::vector<IncomingOffer> offers = context->transfers->pendingOffers();
            if (!offers.empty()) {
                offer = offers.front();
                available = true;
                break;
            }
            Sleep(100);
        }
        require(available, "Timed out waiting for the incoming request.");
        if (context->reject) {
            context->success = context->transfers->rejectIncoming(offer, context->error);
        } else {
            std::string invalidKeyError;
            std::string unusedPath;
            require(!context->transfers->acceptIncoming(
                        offer, "alice", "wrong-session-key",
                        [](std::uint64_t, std::uint64_t) {},
                        invalidKeyError, unusedPath) &&
                    invalidKeyError.find("session key did not match") != std::string::npos,
                    "The transfer manager accepted an invalid session key.");
            require(EncryptionManager::verifySessionKey(context->key, offer.nonce, offer.proof),
                    "The session-key proof did not validate.");
            context->success = context->transfers->acceptIncoming(
                offer, "alice", context->key,
                [](std::uint64_t, std::uint64_t) {}, context->error, context->savedFile);
        }
    } catch (const std::exception& exception) {
        context->error = exception.what();
    }
    SetEvent(context->finished);
    return 0;
}

void writePayload(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::out);
    require(static_cast<bool>(output), "Could not create the integration payload.");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(static_cast<bool>(output), "Could not write the integration payload.");
}

} // namespace

int main() {
    try {
        WinsockRuntime winsock;
        FileManager::ensureDirectory("tests");
        FileManager::ensureDirectory("received");
        const char* accountPath = "tests\\integration_accounts.txt";
        const char* senderHistoryPath = "tests\\integration_sender_history.txt";
        const char* receiverHistoryPath = "tests\\integration_receiver_history.txt";
        const char* inputPath = "tests\\integration_payload.bin";
        DeleteFileA(accountPath);
        DeleteFileA(senderHistoryPath);
        DeleteFileA(receiverHistoryPath);
        DeleteFileA("received\\integration_payload.bin");
        DeleteFileA("received\\integration_payload (1).bin");

        Authentication registration(accountPath);
        registration.registerUser("alice", "integration-key-a");
        registration.registerUser("bob", "integration-key-b");
        Authentication alice(accountPath);
        Authentication bob(accountPath);
        require(alice.login("alice", "integration-key-a"), "Alice login failed.");
        require(bob.login("bob", "integration-key-b"), "Bob login failed.");
        Authentication invalid(accountPath);
        require(!invalid.login("alice", "incorrect-password"),
                "Invalid password was incorrectly accepted.");

        std::vector<std::uint8_t> payload(180000);
        for (std::size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<std::uint8_t>((i * 31 + 7) % 256);
        }
        writePayload(inputPath, payload);

        HistoryManager senderHistory(senderHistoryPath);
        HistoryManager receiverHistory(receiverHistoryPath);
        Server receiverServer;
        unsigned short listeningPort = 55000;
        bool listenerStarted = false;
        for (unsigned short candidate = 55000; candidate < 55100; ++candidate) {
            try {
                receiverServer.start(candidate, "alice");
                listeningPort = candidate;
                listenerStarted = true;
                break;
            } catch (const std::exception&) {
            }
        }
        require(listenerStarted, "Could not reserve a free test TCP port.");

        TransferManager receiverTransfers(receiverServer, receiverHistory);
        Server senderServer;
        TransferManager senderTransfers(senderServer, senderHistory);
        ReceiverContext receiverContext;
        receiverContext.server = &receiverServer;
        receiverContext.transfers = &receiverTransfers;
        receiverContext.key = "integration-session-key";
        receiverContext.finished = CreateEventA(NULL, TRUE, FALSE, NULL);
        require(receiverContext.finished != NULL, "Could not create receiver test event.");

        unsigned int threadId = 0;
        uintptr_t rawThread = _beginthreadex(
            NULL, 0, &acceptTransfer, &receiverContext, 0, &threadId);
        require(rawThread != 0, "Could not create the receiver test thread.");
        HANDLE receiverThread = reinterpret_cast<HANDLE>(rawThread);

        senderTransfers.sendFileChecked("bob", "alice", "127.0.0.1",
                                        listeningPort, inputPath,
                                        receiverContext.key);
        const DWORD waitResult = WaitForSingleObject(receiverContext.finished, 20000);
        require(waitResult == WAIT_OBJECT_0, "Receiver did not complete before timeout.");
        WaitForSingleObject(receiverThread, INFINITE);
        CloseHandle(receiverThread);
        require(receiverContext.success, "Receiver failed: " + receiverContext.error);

        FileManager files;
        const std::vector<std::uint8_t> received = files.readFile(receiverContext.savedFile);
        require(received == payload, "Received file bytes do not match the source.");
        const std::vector<TransferRecord> senderRecords = senderHistory.chronological();
        const std::vector<TransferRecord> receiverRecords = receiverHistory.chronological();
        require(senderRecords.size() == 1 && senderRecords[0].status == "Sent",
                "Sender history did not record the successful transfer.");
        require(receiverRecords.size() == 1 && receiverRecords[0].status == "Received",
                "Receiver history did not record the successful transfer.");
        require(alice.isLoggedIn() && bob.isLoggedIn(),
                "Authenticated sessions were not preserved through transfer.");

        ResetEvent(receiverContext.finished);
        receiverContext.success = false;
        receiverContext.reject = true;
        receiverContext.error.clear();
        rawThread = _beginthreadex(NULL, 0, &acceptTransfer, &receiverContext, 0, &threadId);
        require(rawThread != 0, "Could not create the rejection test thread.");
        receiverThread = reinterpret_cast<HANDLE>(rawThread);
        bool senderSawRejection = false;
        try {
            senderTransfers.sendFileChecked("bob", "alice", "127.0.0.1",
                listeningPort, inputPath, receiverContext.key);
        } catch (const std::exception& exception) {
            senderSawRejection =
                std::string(exception.what()) ==
                "The receiver rejected the transfer or session key.";
        }
        require(senderSawRejection, "Sender did not receive a clear rejection response.");
        require(WaitForSingleObject(receiverContext.finished, 20000) == WAIT_OBJECT_0,
                "Receiver did not finish processing the rejected request.");
        WaitForSingleObject(receiverThread, INFINITE);
        CloseHandle(receiverThread);
        require(receiverContext.success, "Receiver could not reject the request: " +
                                         receiverContext.error);

        const std::vector<TransferRecord> finalSenderRecords = senderHistory.chronological();
        const std::vector<TransferRecord> finalReceiverRecords = receiverHistory.chronological();
        bool senderLoggedRejection = false;
        bool receiverLoggedRejection = false;
        for (std::size_t i = 0; i < finalSenderRecords.size(); ++i) {
            senderLoggedRejection = senderLoggedRejection ||
                                    finalSenderRecords[i].status == "Rejected";
        }
        for (std::size_t i = 0; i < finalReceiverRecords.size(); ++i) {
            receiverLoggedRejection = receiverLoggedRejection ||
                                      finalReceiverRecords[i].status == "Rejected";
        }
        require(senderLoggedRejection && receiverLoggedRejection,
                "The rejected transfer was not recorded on both peers.");
        require(receiverServer.pendingCount() == 0,
                "Completed requests remained in the pending-request count.");

        receiverServer.stop();
        CloseHandle(receiverContext.finished);
        DeleteFileA(inputPath);
        DeleteFileA(receiverContext.savedFile.c_str());
        DeleteFileA(accountPath);
        DeleteFileA(senderHistoryPath);
        DeleteFileA(receiverHistoryPath);
        std::cout << "PASS: authentication, invalid-key rejection, accepted 180000-byte "
                     "P2P transfer, file integrity, request rejection, and history records.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
