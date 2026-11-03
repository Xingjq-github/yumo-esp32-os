#pragma once
#include "cJSON.h"
#include "password_vault.hpp"
#include "vault_origin.hpp"
namespace vault {
void wipeJson(cJSON *obj);
struct Json {
  cJSON *value = nullptr;
  explicit Json(const uint8_t *data, size_t size);
  ~Json() {
    wipeJson(value);
    cJSON_Delete(value);
  }
};
bool payload(const Bytes &data, Session &session);
bool configDecode(const Bytes &data, Session &session);
bool configEncode(const Session &session, Bytes &output);
bool validOrigin(const std::string &url);
} // namespace vault
