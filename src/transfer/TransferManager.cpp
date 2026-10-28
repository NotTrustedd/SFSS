#include "transfer/TransferManager.h"
#include "network/Protocol.h"
#include <conio.h>
#include <iostream>
#include <stdexcept>

namespace {
void showProgress(std::uint64_t completed, std::uint64_t total) {
    if (_kbhit()) {
        const int key = _getch();
        if (key == 'c' || key == 'C') {
            throw std::runtime_error("Transfer cancelled by receiver.");
        }
    }
    const std::uint64_t percent = total == 0 ? 100 : completed * 100 / total;
    const std::size_t filled = static_cast<std::size_t>(percent / 5);
    std::cout << '\r' << '[' << std::string(filled, '#')
              << std::string(20 - filled, ' ') << "] " << percent << "% ("
              << completed << '/' << total << " bytes)" << std::flush;
    if (completed >= total) {
        std::cout << '\n';
    }
}

std::string baseFilename(const std::string& path) {
    const std::size_t separator = path.find_last_of("\\/");
    return path.substr(separator == std::string::npos ? 0 : separator + 1);
}

std::string readTrimmedLine(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    if (!std::getline(std::cin, value)) {
        throw std::runtime_error("Input ended unexpectedly.");
    }
    return value;
}
}

TransferManager::TransferManager(Server& server, HistoryManager& history)
    : server_(server), history_(history) {}

void TransferManager::record(const std::string& sender,
                             const std::string& receiver,
                             const std::string& filename,
                             unsigned long long size,
                             const std::string& status) {
    TransferRecord item;
    item.timestamp = Protocol::currentTimestamp();
    item.sender = sender;
    item.receiver = receiver;
    item.filename = filename;
    item.fileSize = size;
    item.status = status;
    history_.add(item);
}

std::vector<IncomingOffer> TransferManager::pendingOffers() const {
    return server_.pendingOffers();
}

bool TransferManager::rejectIncoming(const IncomingOffer& offer, std::string& error) {
    try {
        server_.resolve(offer.id, false);
        server_.waitForTransfer(offer.id);
        record(offer.sender, offer.receiver, offer.filename, offer.fileSize, "Rejected");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool TransferManager::acceptIncoming(const IncomingOffer& offer,
                                    const std::string& receiver,
                                    const std::string& sessionKey,
                                    const std::function<void(std::uint64_t, std::uint64_t)>& progress,
                                    std::string& error,
                                    std::string& savedFile) {
    if (!EncryptionManager::verifySessionKey(sessionKey, offer.nonce, offer.proof)) {
        error = "Shared session key did not match; the request remains pending.";
        return false;
    }
    try {
        server_.resolve(offer.id, true, progress);
        ReceivedTransfer received = server_.waitForTransfer(offer.id);
        if (!received.success) {
            error = received.error;
            record(offer.sender, receiver, offer.filename, offer.fileSize,
                   received.error.find("cancel") != std::string::npos ? "Cancelled" : "Failed");
            return false;
        }

        const std::vector<std::uint8_t> plain =
            EncryptionManager::xorTransform(received.encryptedFile, sessionKey);
        savedFile = files_.saveReceivedFile(offer.filename, plain);
        record(offer.sender, receiver, offer.filename, offer.fileSize, "Received");
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        record(offer.sender, receiver, offer.filename, offer.fileSize,
               std::string(exception.what()).find("cancel") != std::string::npos ? "Cancelled" : "Failed");
        return false;
    }
}

void TransferManager::sendFile(const std::string& sender,
                               const std::string& receiver,
                               const std::string& address,
                               unsigned short port,
                               const std::string& filePath,
                               const std::string& sessionKey) {
    try {
        sendFileChecked(sender, receiver, address, port, filePath, sessionKey);
    } catch (const std::exception& error) {
        std::cerr << "Transfer failed: " << error.what() << '\n';
    }
}

void TransferManager::sendFileChecked(const std::string& sender,
                                      const std::string& receiver,
                                      const std::string& address,
                                      unsigned short port,
                                      const std::string& filePath,
                                      const std::string& sessionKey,
                                      volatile LONG* cancelRequested) {
    const std::string filename = baseFilename(filePath);
    unsigned long long size = 0;
    try {
        if (!Protocol::validToken(receiver)) {
            throw std::invalid_argument("Invalid receiver username.");
        }
        if (sessionKey.size() < 8 || sessionKey.size() > 1024) {
            throw std::invalid_argument("Session key must be between 8 and 1024 characters.");
        }
        FileManager::safeFilename(filename);
        const std::vector<std::uint8_t> plain = files_.readFile(filePath);
        size = plain.size();
        const std::vector<std::uint8_t> encrypted =
            EncryptionManager::xorTransform(plain, sessionKey);
        const std::string nonce = EncryptionManager::randomHex(16);
        const std::string proof = EncryptionManager::sessionProof(sessionKey, nonce);
        std::cout << "Waiting for " << receiver << " to accept the request...\n";
        client_.sendTransfer(address, port, sender, receiver, filename,
                             nonce, proof, encrypted, cancelRequested);
        record(sender, receiver, filename, size, "Sent");
        std::cout << "Transfer completed successfully.\n";
    } catch (const std::exception& error) {
        const std::string message(error.what());
        const std::string status =
            message == "Transfer cancelled by sender." ||
            message == "Transfer cancelled by receiver." ? "Cancelled" :
            message == "The receiver rejected the transfer or session key."
                ? "Rejected" : "Failed";
        record(sender, receiver, filename, size, status);
        throw;
    }
}

void TransferManager::processIncoming(const std::string& receiver) {
    IncomingOffer offer;
    if (!server_.takePending(offer)) {
        std::cout << "No incoming requests are waiting.\n";
        return;
    }

    std::cout << "\nIncoming request from " << offer.sender << ": " << offer.filename
              << " (" << offer.fileSize << " bytes)\n";
    const std::string decision = readTrimmedLine("Accept this transfer? (y/n): ");
    if (decision != "y" && decision != "Y") {
        server_.resolve(offer.id, false);
        server_.waitForTransfer(offer.id);
        record(offer.sender, receiver, offer.filename, offer.fileSize, "Rejected");
        std::cout << "Transfer rejected.\n";
        return;
    }

    std::string sessionKey;
    bool verified = false;
    for (int attempt = 1; attempt <= 3 && !verified; ++attempt) {
        sessionKey = readTrimmedLine("Enter the shared session key: ");
        verified = EncryptionManager::verifySessionKey(
            sessionKey, offer.nonce, offer.proof);
        if (!verified) {
            std::cout << "Session key did not match (" << attempt << "/3).\n";
        }
    }
    if (!verified) {
        server_.resolve(offer.id, false);
        server_.waitForTransfer(offer.id);
        record(offer.sender, receiver, offer.filename, offer.fileSize, "Rejected");
        std::cout << "Transfer rejected because the session key was not verified.\n";
        return;
    }

    try {
        server_.resolve(offer.id, true, showProgress);
        ReceivedTransfer received = server_.waitForTransfer(offer.id);
        if (!received.success) {
            const bool cancelled =
                received.error == "Transfer cancelled by receiver." ||
                received.error == "Sender cancelled before receiver approval." ||
                received.error == "Sender cancelled during transfer.";
            record(offer.sender, receiver, offer.filename, offer.fileSize,
                   cancelled ? "Cancelled" : "Failed");
            std::cerr << "Transfer failed: " << received.error << '\n';
            return;
        }
        const std::vector<std::uint8_t> plain =
            EncryptionManager::xorTransform(received.encryptedFile, sessionKey);
        const std::string outputPath = files_.saveReceivedFile(offer.filename, plain);
        record(offer.sender, receiver, offer.filename, offer.fileSize, "Received");
        std::cout << "Saved decrypted file: " << outputPath << '\n';
    } catch (const std::exception& error) {
        const bool cancelled =
            std::string(error.what()) == "Transfer cancelled by receiver.";
        record(offer.sender, receiver, offer.filename, offer.fileSize,
               cancelled ? "Cancelled" : "Failed");
        std::cerr << "Could not complete incoming transfer: " << error.what() << '\n';
    }
}
