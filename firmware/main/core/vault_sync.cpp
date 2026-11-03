#include "vault_sync.hpp"
#include "connectivity.hpp"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "mbedtls/x509.h"
#include <cerrno>
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_mgr.hpp"
#include "vault_json.hpp"
#include "vault_deploy_key.hpp"
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <ctime>
namespace vault {
namespace {
struct ClientOwner {
  esp_http_client_handle_t value;
  ~ClientOwner() {
    if (value) {
      esp_http_client_close(value);
      esp_http_client_cleanup(value);
    }
  }
};
struct SecretHeader {
  std::string value;
  ~SecretHeader() { clear(value); }
};
struct SyncProof {
  char value[65]{};
  unsigned received = 0;
  bool matched = false;
  ~SyncProof() { wipe(value, sizeof(value)); }
};
bool prepareProof(SyncProof &proof) {
  static constexpr char domain[] = "JYVAULT-DEPLOY-KEY-V2";
  uint8_t input[sizeof(domain) - 1 + sizeof(builtin::DeployKey)];
  static_assert(sizeof(builtin::DeployKey) == 32);
  std::memcpy(input, domain, sizeof(domain) - 1);
  std::memcpy(input + sizeof(domain) - 1, builtin::DeployKey, 32);
  Key digest;
  const bool ok = hash(input, sizeof(input), digest.bytes.data());
  wipe(input, sizeof(input));
  if (!ok) return false;
  static constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    proof.value[2 * i] = digits[digest.bytes[i] >> 4];
    proof.value[2 * i + 1] = digits[digest.bytes[i] & 15];
  }
  return true;
}
esp_err_t responseHeader(esp_http_client_event_t *event) {
  if (event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data ||
      !event->header_key || strcasecmp(event->header_key, "X-Vault-Key"))
    return ESP_OK;
  auto &proof = *static_cast<SyncProof *>(event->user_data);
  ++proof.received;
  proof.matched = proof.received == 1 && event->header_value &&
      std::strlen(event->header_value) == 64 &&
      equal(reinterpret_cast<const uint8_t *>(proof.value),
            reinterpret_cast<const uint8_t *>(event->header_value), 64);
  return ESP_OK;
}
struct TransferBuffer {
  uint8_t value[2048]{};
  ~TransferBuffer() { wipe(value, sizeof(value)); }
};
void connectionError(Job &job, esp_http_client_handle_t client, bool https) {
  int tlsCode = 0, certFlags = 0;
  const auto tlsError = esp_http_client_get_and_clear_last_tls_error(
      client, &tlsCode, &certFlags);
  const int socketError = esp_http_client_get_errno(client);
  // Only numeric diagnostics: never log credentials or response data.
  ESP_LOGW("vault_sync", "connect failed https=%d tls=%d code=%d cert=%d socket=%d",
           int(https), int(tlsError), tlsCode, certFlags, socketError);
  if (tlsError == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME)
    job.error = "域名解析失败，请检查Wi-Fi网络";
  else if (certFlags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE))
    job.error = "HTTPS证书日期无效，请检查设备时间";
  else if (certFlags & MBEDTLS_X509_BADCERT_CN_MISMATCH)
    job.error = "HTTPS证书与服务域名不匹配";
  else if (certFlags || tlsCode == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
    job.error = "HTTPS证书不受信任，请检查服务证书链";
  else if (tlsError == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT ||
           tlsError == ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT ||
           socketError == ETIMEDOUT)
    job.error = "连接超时，请检查公网服务和网络";
  else if (https && tlsError == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
    job.error = "HTTPS握手失败，请检查服务TLS配置";
  else
    job.error = "连接失败，请检查服务地址、端口和网络";
}
} // namespace
bool request(Job &job, const std::string &origin, const std::string &ca,
             const std::string &path, const std::string &token, size_t maximum,
             Bytes &output) {
  clear(output);
  struct OutputGuard {
    Bytes &value;
    bool keep = false;
    ~OutputGuard() { if (!keep) clear(value); }
  } outputGuard{output};
  if (job.cancelled.load() || !sys::Power::isScreenOn() ||
      sys::Power::sleepPending()) {
    job.error = "已取消";
    return false;
  }
  if (sys::Connectivity::snapshot().state != sys::WifiState::Connected) {
    job.error = "Wi-Fi未连接，请手动连接";
    return false;
  }
  std::string endpoint;
  if (!normalizeOrigin(origin, endpoint)) {
    job.error = "服务地址无效，请填写HTTP或HTTPS地址";
    return false;
  }
  const bool https = endpoint.rfind("https://", 0) == 0;
  if (https && std::time(nullptr) < 1704067200) {
    job.error = "设备时间未校准，请联网校时后重试";
    return false;
  }
  SyncProof proof;
  if (!prepareProof(proof)) {
    job.error = "同步密钥校验失败，请重试";
    return false;
  }
  const std::string url = endpoint + path;
  esp_http_client_config_t config{};
  config.url = url.c_str();
  config.event_handler = responseHeader;
  config.user_data = &proof;
  config.cert_pem = ca.empty() ? nullptr : ca.c_str();
  // Public HTTPS uses the maintained CA bundle; an explicit CA takes priority.
  if (https && ca.empty())
    config.crt_bundle_attach = esp_crt_bundle_attach;
  config.timeout_ms = 10000;
  config.disable_auto_redirect = true;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
  config.skip_cert_common_name_check = false;
  auto client = esp_http_client_init(&config);
  if (!client) {
    job.error = "网络内存不足";
    return false;
  }
  ClientOwner owner{client};
  SecretHeader authorization;
  auto &bearer = authorization.value;
  if (!token.empty()) {
    bearer = "Bearer " + token;
    if (esp_http_client_set_header(client, "Authorization", bearer.c_str()) != ESP_OK) {
      job.error = "同步请求初始化失败";
      return false;
    }
  }
  if (esp_http_client_set_header(client, "X-Vault-Key", proof.value) != ESP_OK ||
      esp_http_client_set_header(client, "Accept", "application/octet-stream") != ESP_OK ||
      esp_http_client_set_header(client, "Accept-Encoding", "identity") != ESP_OK) {
    job.error = "同步请求初始化失败";
    return false;
  }
  bool ok = false;
  const int64_t deadline = esp_timer_get_time() + 30000000;
  auto continuing = [&]() {
    if (job.cancelled.load() || !sys::Power::isScreenOn() ||
        sys::Power::sleepPending()) {
      job.error = "同步已取消";
      return false;
    }
    if (esp_timer_get_time() >= deadline) {
      job.error = "同步超时，请重试";
      return false;
    }
    return true;
  };
  do {
    if (!continuing()) break;
    if (esp_http_client_open(client, 0) != ESP_OK) {
      connectionError(job, client, https);
      break;
    }
    if (!continuing()) break;
    auto length = esp_http_client_fetch_headers(client);
    if (!continuing()) break;
    int status = esp_http_client_get_status_code(client);
    if (length < 0) {
      job.error = "服务响应中断或超时，请重试";
      break;
    }
    if (status != 200) {
      job.error = status == 401   ? "设备同步身份不匹配"
                  : status == 403 ? "同步密钥不匹配，请更新双端程序"
                  : status == 404 ? "请先在服务端登录并保存密码"
                  : status >= 300 && status < 400
                      ? "地址发生跳转，请填写最终HTTPS服务地址"
                  : status == 502 || status == 503 || status == 504
                      ? "公网转发或服务暂不可用，请稍后重试"
                      : "同步请求失败，HTTP " + std::to_string(status);
      break;
    }
    if (!proof.matched || proof.received != 1) {
      job.error = "服务端密钥校验失败，请更新服务";
      break;
    }
    if (length > static_cast<int64_t>(maximum)) {
      job.error = "响应长度无效或超过上限";
      break;
    }
    output.clear();
    if (length)
      output.reserve(length);
    TransferBuffer transfer;
    auto &buffer = transfer.value;
    while (!job.cancelled.load() && sys::Power::isScreenOn() &&
           !sys::Power::sleepPending() && esp_timer_get_time() < deadline) {
      int n = esp_http_client_read(client, reinterpret_cast<char *>(buffer),
                                   sizeof(buffer));
      // Blocking I/O may return after screen-off, cancellation or the deadline.
      // Recheck before treating EOF as a successful complete response.
      if (!continuing()) break;
      if (n < 0) {
        job.error = "下载中断";
        break;
      }
      if (n == 0) {
        ok = esp_http_client_is_complete_data_received(client);
        if (!ok)
          job.error = "下载不完整";
        break;
      }
      if (output.size() + n > maximum) {
        job.error = "下载超过上限";
        break;
      }
      output.insert(output.end(), buffer, buffer + n);
      wipe(buffer, sizeof(buffer));
      vTaskDelay(1);
    }
    wipe(buffer, sizeof(buffer));
    if (!ok && job.error.empty())
      job.error = "同步取消或超时";
  } while (false);
  // Client owns copies of both authentication headers; cleanup releases
  // it promptly. ESP-IDF's allocator cannot guarantee zeroing that copy.
  clear(bearer);
  if (!ok)
    clear(output);
  outputGuard.keep = ok;
  return ok;
}
} // namespace vault
