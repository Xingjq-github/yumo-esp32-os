#pragma once
#include "password_vault.hpp"
namespace vault {
bool request(Job &job, const std::string &url, const std::string &ca,
             const std::string &path, const std::string &token, size_t maximum,
             Bytes &output);
} // namespace vault
