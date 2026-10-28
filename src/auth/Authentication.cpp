#include "auth/Authentication.h"

Authentication::Authentication(const std::string& accountFile) : users_(accountFile) {}

void Authentication::registerUser(const std::string& username, const std::string& password) {
    users_.registerUser(username, password);
}

bool Authentication::login(const std::string& username, const std::string& password) {
    if (!currentUsername_.empty()) {
        return false;
    }
    if (!users_.authenticate(username, password)) {
        return false;
    }
    currentUsername_ = username;
    return true;
}

void Authentication::logout() {
    currentUsername_.clear();
}

bool Authentication::isLoggedIn() const {
    return !currentUsername_.empty();
}

const std::string& Authentication::currentUsername() const {
    return currentUsername_;
}
