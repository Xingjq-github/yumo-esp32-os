#include "lan_web.hpp"
#include "lan_web_protocol.hpp"
#include "sd_media.hpp"
#include "password_vault.hpp"
#include "vault_json.hpp"
#include "vault_text.hpp"
#include "power_mgr.hpp"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

extern const unsigned char lanPageStart[] asm("_binary_lan_web_html_start");
extern const unsigned char lanPageEnd[] asm("_binary_lan_web_html_end");
namespace sys::lanweb {
namespace {
std::atomic<bool> wanted{false}, working{false}, serving{false};
std::atomic<uint32_t> address{0};
std::atomic<int> lastError{ESP_OK};
// Only the HTTP task and its supervisor (after httpd_stop) own auth state.
std::mutex authMutex;
struct BrowserSession {
  std::shared_ptr<vault::Session> unlocked;
  std::string token;
  int64_t expires = 0;
  void clear() { unlocked.reset(); vault::clear(token); expires = 0; }
};
std::array<BrowserSession, 4> sessions;
constexpr int64_t SessionUs = 5LL * 60 * 1000000;
bool active() { return wanted.load() && !Power::sleepPending(); }
bool cancelled(void *) { vTaskDelay(1); return !active(); }
void lockVault() {
  for (auto &session : sessions) session.clear();
}
BrowserSession *authenticated(httpd_req_t *req);
std::string header(httpd_req_t *req, const char *name) {
  const size_t n = httpd_req_get_hdr_value_len(req, name);
  if (!n || n > 256) return {};
  std::string out(n + 1, '\0');
  if (httpd_req_get_hdr_value_str(req, name, out.data(), out.size()) != ESP_OK) return {};
  out.resize(n); return out;
}
BrowserSession *authenticated(httpd_req_t *req) {
  auto cookie = header(req, "Cookie");
  BrowserSession *match = nullptr;
  const auto now = esp_timer_get_time();
  for (auto &session : sessions) {
    if (session.expires && now >= session.expires) session.clear();
    if (session.unlocked && cookieMatches(cookie, session.token)) match = &session;
  }
  vault::clear(cookie);
  return match;
}
void responseHeaders(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(req, "Connection", "close");
}
esp_err_t message(httpd_req_t *req, const char *status, const char *text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
  // Closing also discards any unconsumed oversized/unauthorized request body.
  return ESP_FAIL;
}
esp_err_t json(httpd_req_t *req, cJSON *value) {
  if (!value) return message(req, "503 Service Unavailable", "内存不足");
  char *text = cJSON_PrintUnformatted(value);
  vault::wipeJson(value); cJSON_Delete(value);
  if (!text) return message(req, "503 Service Unavailable", "内存不足");
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  auto result = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
  vault::wipe(text, std::strlen(text)); cJSON_free(text);
  return result;
}
std::string query(httpd_req_t *req, const char *key) {
  std::string encoded(3072, '\0');
  const char *q = std::strchr(req->uri, '?');
  if (!q || httpd_query_key_value(q + 1, key, encoded.data(), encoded.size()) != ESP_OK) return {};
  encoded.resize(std::strlen(encoded.c_str()));
  std::string out;
  if (!decode(encoded, out)) return {};
  return out;
}
bool receive(httpd_req_t *req, std::string &text, size_t limit) {
  if (req->content_len > limit) return false;
  text.resize(req->content_len);
  size_t got = 0;
  while (got < text.size()) {
    if (!active()) return false;
    const int n = httpd_req_recv(req, text.data() + got, text.size() - got);
    if (n <= 0) return false;
    got += n;
  }
  return true;
}
bool socketIpv4(int fd, bool remote, uint32_t &ip) {
  sockaddr_storage endpoint{}; socklen_t size = sizeof(endpoint);
  const int result = remote ? getpeername(fd, reinterpret_cast<sockaddr *>(&endpoint), &size) :
    getsockname(fd, reinterpret_cast<sockaddr *>(&endpoint), &size);
  if (result || size > sizeof(endpoint)) return false;
  if (endpoint.ss_family == AF_INET && size >= sizeof(sockaddr_in)) {
    sockaddr_in v4{}; std::memcpy(&v4, &endpoint, sizeof(v4));
    ip = v4.sin_addr.s_addr; return true;
  }
#if LWIP_IPV6
  if (endpoint.ss_family == AF_INET6 && size >= sizeof(sockaddr_in6)) {
    // ESP HTTPD uses a dual-stack socket: IPv4 clients are returned by lwIP
    // as ::ffff:a.b.c.d. Keep the full address before extracting its IPv4 part.
    sockaddr_in6 v6{}; std::memcpy(&v6, &endpoint, sizeof(v6));
    constexpr uint8_t mappedPrefix[12] = {0,0,0,0,0,0,0,0,0,0,0xff,0xff};
    const auto *bytes = reinterpret_cast<const uint8_t *>(&v6.sin6_addr);
    if (std::memcmp(bytes, mappedPrefix, sizeof(mappedPrefix))) return false;
    std::memcpy(&ip, bytes + sizeof(mappedPrefix), sizeof(ip)); return true;
  }
#endif
  return false;
}
bool admitted(httpd_req_t *req) {
  uint32_t local = 0, peer = 0;
  const int fd = httpd_req_to_sockfd(req);
  const uint32_t ip = address.load();
  if (!active() || !ip || !socketIpv4(fd, false, local) || local != ip) return false;
  if (!socketIpv4(fd, true, peer) ||
      (ntohl(peer) & 0xffffff00u) != (ntohl(ip) & 0xffffff00u)) return false;
  char host[16]{}; inet_ntop(AF_INET, &ip, host, sizeof(host));
  const auto h = header(req, "Host");
  if (h != host && h != std::string(host) + ":80") return false;
  const auto origin = header(req, "Origin");
  if (!origin.empty() && origin != std::string("http://") + h) return false;
  if (req->method != HTTP_GET && header(req, "X-Yumo-Request") != "1") return false;
  return true;
}
esp_err_t sdError(httpd_req_t *req, int error) {
  return message(req, error == ENOENT ? "404 Not Found" : error == EEXIST ? "409 Conflict" : "503 Service Unavailable",
    error == EEXIST ? "同名文件已存在，请更换名称" : error == ENOENT ? "文件或目录不存在" :
    error == EBUSY ? "SD卡正在使用或已弹出，请在设备端重新载入后重试" :
    error == ENOSPC ? "SD卡空间不足" : "SD卡不可用或操作失败");
}
esp_err_t files(httpd_req_t *req) {
  std::string source;
  if (!path(query(req, "path"), source, req->method == HTTP_GET)) return message(req, "400 Bad Request", "路径无效");
  core::sd::Session sd(core::sd::requestAccess(false));
  if (!sd) return sdError(req, sd.error());
  if (req->method != HTTP_GET && !sd.claimMutation()) return sdError(req, EBUSY);
  core::files::Request work; work.source = source;
  if (req->method == HTTP_GET) {
    const auto offset = query(req, "offset");
    if (!offset.empty() && (offset.size() > 6 || offset.find_first_not_of("0123456789") != std::string::npos))
      return message(req, "400 Bad Request", "分页无效");
    work.offset = offset.empty() ? 0 : std::strtoul(offset.c_str(), nullptr, 10);
  } else if (req->method == HTTP_DELETE) work.operation = core::files::Operation::Remove;
  else if (query(req, "action") == "mkdir") work.operation = core::files::Operation::Mkdir;
  else if (query(req, "action") == "rename") {
    work.operation = core::files::Operation::Rename;
    if (!path(query(req, "target"), work.target)) return message(req, "400 Bad Request", "新路径无效");
  } else return message(req, "400 Bad Request", "未知文件操作");
  const auto result = core::files::execute(core::sd::Root, work, {nullptr, cancelled, nullptr});
  sd.reportError(result.error);
  if (result.error) return sdError(req, result.error);
  auto out = cJSON_CreateObject();
  auto entries = cJSON_AddArrayToObject(out, "entries");
  for (const auto &entry : result.entries) {
    auto item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "name", entry.name.c_str());
    cJSON_AddBoolToObject(item, "directory", entry.directory);
    cJSON_AddNumberToObject(item, "bytes", static_cast<double>(entry.bytes));
    cJSON_AddItemToArray(entries, item);
  }
  cJSON_AddBoolToObject(out, "more", result.more);
  return json(req, out);
}
esp_err_t download(httpd_req_t *req) {
  std::string source;
  if (!path(query(req, "path"), source)) return message(req, "400 Bad Request", "路径无效");
  core::sd::Session sd(core::sd::requestAccess(false));
  if (!sd) return sdError(req, sd.error());
  struct stat st{};
  if (stat(source.c_str(), &st) || !S_ISREG(st.st_mode)) return sdError(req, ENOENT);
  FILE *file = fopen(source.c_str(), "rb");
  if (!file) return sdError(req, errno);
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment");
  // Custom byte count lets the browser verify a complete streamed download.
  char length[32]; std::snprintf(length, sizeof(length), "%llu", static_cast<unsigned long long>(st.st_size));
  httpd_resp_set_hdr(req, "X-File-Size", length);
  char buffer[4096]; uint64_t sent = 0; esp_err_t result = ESP_OK; int error = 0;
  while (active()) {
    const size_t n = fread(buffer, 1, sizeof(buffer), file);
    if (n && httpd_resp_send_chunk(req, buffer, n) != ESP_OK) { result = ESP_FAIL; break; }
    sent += n;
    if (n < sizeof(buffer)) { if (ferror(file)) error = EIO; break; }
    vTaskDelay(1);
  }
  if (fclose(file)) error = EIO;
  sd.reportError(error);
  if (!active() || error || sent != static_cast<uint64_t>(st.st_size)) return ESP_FAIL;
  return result == ESP_OK ? httpd_resp_send_chunk(req, nullptr, 0) : result;
}
esp_err_t upload(httpd_req_t *req) {
  std::string destination;
  if (!path(query(req, "path"), destination)) return message(req, "400 Bad Request", "文件名或路径无效");
  if (req->content_len > MaxUpload) return message(req, "413 Payload Too Large", "单个文件最大256 MiB");
  core::sd::Session sd(core::sd::requestAccess(false));
  if (!sd) return sdError(req, sd.error());
  if (!sd.claimMutation()) return sdError(req, EBUSY);
  struct stat st{};
  if (stat(destination.c_str(), &st) == 0) return sdError(req, EEXIST);
  if (errno != ENOENT) return sdError(req, errno);
  const auto parent = destination.substr(0, destination.rfind('/'));
  if (stat(parent.c_str(), &st) || !S_ISDIR(st.st_mode)) return sdError(req, ENOENT);
  char suffix[40]; std::snprintf(suffix, sizeof(suffix), "/.yumo-upload-%08lx.tmp", static_cast<unsigned long>(esp_random()));
  const std::string temporary = parent + suffix;
  const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0664);
  if (fd < 0) return sdError(req, errno);
  char buffer[4096]; size_t received = 0; int error = 0;
  while (received < req->content_len && active()) {
    const int n = httpd_req_recv(req, buffer, std::min(sizeof(buffer), req->content_len - received));
    if (n <= 0) { error = ECANCELED; break; }
    int written = 0;
    while (written < n) {
      int count = write(fd, buffer + written, n - written);
      if (count <= 0) { error = errno ? errno : EIO; break; }
      written += count;
    }
    if (error) break;
    received += n; vTaskDelay(1);
  }
  if (!active() || received != req->content_len) error = ECANCELED;
  if (!error && fsync(fd)) error = errno ? errno : EIO;
  if (close(fd) && !error) error = EIO;
  if (!error && rename(temporary.c_str(), destination.c_str())) error = errno ? errno : EIO;
  if (error) {
    const int cleanup = unlink(temporary.c_str());
    if (cleanup && errno != ENOENT) sd.reportError(EIO);
    sd.reportError(error);
    return sdError(req, error);
  }
  auto out = cJSON_CreateObject(); cJSON_AddNumberToObject(out, "bytes", received);
  return json(req, out);
}
esp_err_t login(httpd_req_t *req) {
  std::string pin;
  if (!receive(req, pin, vault::pattern::Maximum) || !vault::pattern::valid(pin.data(), pin.size())) {
    vault::clear(pin); return message(req, "400 Bad Request", "请绘制设备图形密码，至少连接4个点");
  }
  auto job = std::make_shared<vault::Job>(vault::Operation::WebUnlock);
  job->master = pin;
  vault::clear(pin);
  if (vault::start(job) != vault::StartResult::Started) return message(req, "503 Service Unavailable", "密码本忙，请稍后重试");
  const auto until = esp_timer_get_time() + 30000000;
  while (!job->done.load() && active() && esp_timer_get_time() < until) vTaskDelay(pdMS_TO_TICKS(20));
  if (!job->done.load() || !active()) { job->cancelled.store(true); return ESP_FAIL; }
  if (!job->ok || !job->output) return message(req, "401 Unauthorized", job->error.empty() ? "验证失败" : job->error.c_str());
  std::lock_guard<std::mutex> guard(authMutex);
  auto *slot = authenticated(req);
  if (!slot) slot = &*std::min_element(sessions.begin(), sessions.end(),
    [](const BrowserSession &a, const BrowserSession &b) { return a.expires < b.expires; });
  slot->clear(); slot->unlocked = std::move(job->output);
  uint8_t random[32]; esp_fill_random(random, sizeof(random));
  static constexpr char digits[] = "0123456789abcdef";
  for (auto b : random) { slot->token += digits[b >> 4]; slot->token += digits[b & 15]; }
  vault::wipe(random, sizeof(random)); slot->expires = esp_timer_get_time() + SessionUs;
  auto cookie = "yumo_session=" + slot->token + "; HttpOnly; SameSite=Strict; Path=/; Max-Age=300";
  httpd_resp_set_hdr(req, "Set-Cookie", cookie.c_str());
  auto result = httpd_resp_send(req, "{}", 2); vault::clear(cookie); return result;
}
esp_err_t passwords(httpd_req_t *req) {
  std::lock_guard<std::mutex> guard(authMutex);
  const auto *session = authenticated(req);
  if (!session) {
    return message(req, "401 Unauthorized", "请先绘制设备图形密码，或重新登录");
  }
  auto out = cJSON_CreateArray();
  for (const auto &entry : session->unlocked->entries) {
    auto item = cJSON_CreateObject();
    for (const auto &field : {std::pair<const char *, const std::string &>{"title", entry.title}, {"category", entry.category},
        {"target", entry.target}, {"password", entry.password}, {"note", entry.note}})
      cJSON_AddStringToObject(item, field.first, field.second.c_str());
    if (entry.category != "other") cJSON_AddStringToObject(item, "username", entry.username.c_str());
    auto text = vault::plainText(entry); cJSON_AddStringToObject(item, "text", text.c_str()); vault::clear(text);
    cJSON_AddItemToArray(out, item);
  }
  return json(req, out);
}
esp_err_t dispatch(httpd_req_t *req) {
  responseHeaders(req);
  if (!admitted(req)) return message(req, "403 Forbidden", "请连接设备热点，使用设备显示的地址访问");
  Power::WorkGuard work;
  try {
    const std::string uri(req->uri, std::strcspn(req->uri, "?"));
    if (uri == "/" && req->method == HTTP_GET) {
      httpd_resp_set_type(req, "text/html; charset=utf-8");
      httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'");
      return httpd_resp_send(req, reinterpret_cast<const char *>(lanPageStart), lanPageEnd - lanPageStart);
    }
    if (uri == "/api/files" && (req->method == HTTP_GET || req->method == HTTP_POST || req->method == HTTP_DELETE)) return files(req);
    if (uri == "/api/download" && req->method == HTTP_GET) return download(req);
    if (uri == "/api/upload" && req->method == HTTP_PUT) return upload(req);
    if (uri == "/api/login" && req->method == HTTP_POST) return login(req);
    if (uri == "/api/passwords" && req->method == HTTP_GET) return passwords(req);
    if (uri == "/api/logout" && req->method == HTTP_POST) {
      std::lock_guard<std::mutex> guard(authMutex);
      if (auto *session = authenticated(req)) session->clear();
      httpd_resp_set_hdr(req, "Set-Cookie", "yumo_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
      return httpd_resp_send(req, "{}", 2);
    }
    return message(req, "404 Not Found", "页面不存在");
  } catch (...) { return message(req, "503 Service Unavailable", "内存不足或服务繁忙"); }
}
void supervisor(void *) {
  httpd_handle_t server = nullptr;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size = 12288;
  config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  config.max_open_sockets = 4; config.lru_purge_enable = true;
  config.recv_wait_timeout = 2; config.send_wait_timeout = 2;
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.enable_so_linger = true; config.linger_timeout = 0;
  auto error = active() ? httpd_start(&server, &config) : ESP_ERR_INVALID_STATE;
  if (error == ESP_OK) {
    for (const auto method : {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE}) {
      httpd_uri_t uri{}; uri.uri = "/*"; uri.method = method; uri.handler = dispatch;
      error = httpd_register_uri_handler(server, &uri); if (error != ESP_OK) break;
    }
  }
  lastError.store(error); serving.store(error == ESP_OK);
  while (active() && error == ESP_OK) {
    { std::unique_lock<std::mutex> guard(authMutex, std::try_to_lock);
      if (guard.owns_lock()) for (auto &session : sessions)
        if (session.expires && esp_timer_get_time() >= session.expires) session.clear(); }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  serving.store(false);
  if (server) {
    // Keep AP/SD ownership until the HTTP task and its handles have stopped.
    while (httpd_stop(server) != ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
  }
  { std::lock_guard<std::mutex> guard(authMutex); lockVault(); }
  working.store(false);
  vTaskDeleteWithCaps(nullptr);
}
}
void enable(uint32_t ip) {
  address.store(ip);
  if (wanted.exchange(true)) return;
  if (working.exchange(true)) return;
  lastError.store(ESP_OK);
  if (xTaskCreateWithCaps(supervisor, "lan_web", 4096, nullptr, 1, nullptr,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    lastError.store(ESP_ERR_NO_MEM); working.store(false);
  }
}
void stop() { wanted.store(false); }
bool idle() { return !working.load(); }
bool ready() { return active() && serving.load(); }
esp_err_t error() { return lastError.load(); }
}
