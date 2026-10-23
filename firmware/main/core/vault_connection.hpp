#pragma once
#include "sd_media.hpp"
#include <string>
namespace vault {
// Transport settings only: identities/keys and the device PIN never come from
// SD.
struct ConnectionConfig {
  std::string url;
  static constexpr const char *SdPath = "/sdcard/config/vault.json";
  static bool parse(const std::string &json, ConnectionConfig &out,
                    std::string &error);
  static bool readSd(ConnectionConfig &out, std::string &error,core::sd::Access access);
};
} // namespace vault
