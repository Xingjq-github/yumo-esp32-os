#include "vault_connection.hpp"
#include "vault_json.hpp"
#include "sd_media.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
namespace vault {
bool ConnectionConfig::parse(const std::string &json, ConnectionConfig &out,
                             std::string &error) {
  if (json.empty() || json.size() > 1024) {
    error = "SD配置为空或超过1KB";
    return false;
  }
  const size_t bom = json.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
  Json doc(reinterpret_cast<const uint8_t *>(json.data() + bom),
           json.size() - bom);
  if (!doc.value || !cJSON_IsObject(doc.value)) {
    error = "SD配置不是有效JSON";
    return false;
  }
  auto version = cJSON_GetObjectItemCaseSensitive(doc.value, "schema");
  auto url = cJSON_GetObjectItemCaseSensitive(doc.value, "url");
  unsigned fields = 0;
  for (auto p = doc.value->child; p; p = p->next) {
    if (!p->string ||
        (std::strcmp(p->string, "schema") && std::strcmp(p->string, "url"))) {
      error = "SD配置只允许schema和url";
      return false;
    }
    ++fields;
  }
  std::string origin;
  if (fields != 2 || !cJSON_IsNumber(version) || version->valuedouble != 1 ||
      !cJSON_IsString(url) || !url->valuestring ||
      !normalizeOrigin(url->valuestring, origin)) {
    error = "SD配置错误：schema须为1，url须为HTTP或HTTPS服务地址";
    return false;
  }
  out.url = origin;
  return true;
}
bool ConnectionConfig::readSd(ConnectionConfig &out, std::string &error,core::sd::Access access) {
  core::sd::Session card(access);
  if (!card) { error = "SD卡不可用，请插卡后重试同步"; return false; }
  FILE *file = std::fopen(SdPath, "rb");
  if (!file) {
    card.reportError(errno);
    error = "无法读取SD卡config/vault.json";
    return false;
  }
  char data[1025];
  const size_t size = std::fread(data, 1, sizeof(data), file);
  const bool failed = std::ferror(file);
  const bool closeFailed = std::fclose(file) != 0;
  if (failed || closeFailed) {
    card.reportError(EIO);
    error = "SD配置读取失败";
    return false;
  }
  return parse(std::string(data, size), out, error);
}
} // namespace vault
