#include "auth/User.h"

User::User(const std::string& username, const std::string& salt, const std::string& passwordHash)
    : username_(username), salt_(salt), passwordHash_(passwordHash) {}

const std::string& User::username() const { return username_; }
const std::string& User::salt() const { return salt_; }
const std::string& User::passwordHash() const { return passwordHash_; }
