// Host replay of the production vault synchronization request. No real network,
// device, filesystem vault or deployment identity is used by this fixture.
#include "core/vault_crypto.hpp"
#include "core/vault_sha256.h"
#include "oracle.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NO_MEM = 0x101;
constexpr int ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_INVALID_STATE = 0x103;
constexpr int ESP_ERR_TIMEOUT = 0x107, ESP_ERR_HTTP_CONNECT = 0x7002;
constexpr int ESP_ERR_HTTP_FETCH_HEADER = 0x7004, ESP_ERR_HTTP_CONNECTING = 0x7006;
constexpr int ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME = 0x8001;
constexpr int ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST = 0x8004;
constexpr int ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT = 0x8006;
constexpr int ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT = 0x8009;
constexpr int ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED = 0x8015;
constexpr int ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED = 0x801a;
constexpr int MBEDTLS_X509_BADCERT_EXPIRED = 0x01, MBEDTLS_X509_BADCERT_FUTURE = 0x0200;
constexpr int MBEDTLS_X509_BADCERT_CN_MISMATCH = 0x04, MBEDTLS_X509_BADCERT_NOT_TRUSTED = 0x08;
constexpr int MBEDTLS_ERR_X509_CERT_VERIFY_FAILED = -0x2700;
constexpr int HTTP_EVENT_ON_HEADER = 3;
// Production messages contain numeric error codes only. Logging is a no-op in
// the transport replay; raw request headers and body never reach stdout.
#define ESP_LOGW(...) ((void)0)

namespace vault {
struct Job { std::atomic<bool> cancelled{false}; std::string error; };
namespace builtin {
// Public, deterministic test vector. Never include vault_deploy_key.hpp here.
constexpr uint8_t DeployKey[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
}
}

namespace fixture {
enum class Proof { Valid, Missing, Wrong, Duplicate, Short, Uppercase, Null, DifferentCaseHeader };
struct Scenario {
    bool connected = true, screen = true, sleeping = false;
    bool hashFail = false, initFail = false, complete = true;
    bool cancelOpen = false, cancelHeaders = false, screenOffOpen = false, sleepHeaders = false;
    int status = 200, openError = ESP_OK, tlsError = ESP_OK;
    int tlsCode = 0, tlsFlags = 0, socketErrno = 0;
    int headerFailAt = 0, failReadAt = 0, cancelReadAt = 0;
    int screenOffReadAt = 0, sleepReadAt = 0;
    int64_t contentLength = -2, nowUs = 1000000, readDelayUs = 0;
    int64_t openDelayUs = 0, headersDelayUs = 0;
    std::time_t unixTime = 1793426400;
    Proof proof = Proof::Valid;
    std::vector<uint8_t> body{'s','y','n','t','h','e','t','i','c','-','v','a','u','l','t'};
    size_t chunk = 5, offset = 0;
    int inits = 0, opens = 0, headers = 0, fetches = 0, reads = 0;
    int closes = 0, cleanups = 0, follows = 0, live = 0, hashes = 0;
    int tlsQueries = 0, bundles = 0;
    int timeout = 0;
    bool noRedirect = false, skipName = false, hadBundle = false, hadCa = false;
    bool globalCa = false;
    std::string url, ca;
    std::map<std::string, std::string> requestHeaders;
    vault::Job *job = nullptr;
} state;
unsigned checks = 0;
const char *caseName = "not-started";
void check(bool passed, int line, const char *condition) {
    ++checks;
    if (!passed) {
        std::fprintf(stderr, "%s:%d: %s\n", caseName, line, condition);
        std::exit(1);
    }
}
}
#define CHECK(condition) fixture::check(bool(condition), __LINE__, #condition)

namespace sys {
enum class WifiState { Disconnected, Connected };
struct WifiSnapshot { WifiState state; };
struct Connectivity {
    static WifiSnapshot snapshot() { return {fixture::state.connected ? WifiState::Connected : WifiState::Disconnected}; }
};
struct Power {
    static bool isScreenOn() { return fixture::state.screen; }
    static bool sleepPending() { return fixture::state.sleeping; }
};
}

struct esp_http_client_event_t {
    int event_id;
    void *user_data;
    char *header_key;
    char *header_value;
};
struct esp_http_client_config_t {
    const char *url = nullptr, *cert_pem = nullptr, *common_name = nullptr;
    esp_err_t (*event_handler)(esp_http_client_event_t *) = nullptr;
    void *user_data = nullptr;
    int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
    bool disable_auto_redirect = false, skip_cert_common_name_check = false;
    bool use_global_ca_store = false;
    esp_err_t (*crt_bundle_attach)(void *) = nullptr;
};
struct FakeClient { esp_http_client_config_t config; };
using esp_http_client_handle_t = FakeClient *;
esp_err_t esp_crt_bundle_attach(void *) { ++fixture::state.bundles; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    auto &s = fixture::state;
    ++s.inits;
    s.url = config->url ? config->url : "";
    s.ca = config->cert_pem ? config->cert_pem : "";
    s.hadCa = config->cert_pem != nullptr;
    s.hadBundle = config->crt_bundle_attach == esp_crt_bundle_attach;
    s.noRedirect = config->disable_auto_redirect;
    s.skipName = config->skip_cert_common_name_check;
    s.globalCa = config->use_global_ca_store;
    s.timeout = config->timeout_ms;
    CHECK(config->common_name == nullptr);
    if (s.initFail) return nullptr;
    ++s.live;
    return new FakeClient{*config};
}
esp_err_t esp_http_client_close(esp_http_client_handle_t client) {
    CHECK(client != nullptr); ++fixture::state.closes; return ESP_OK;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    CHECK(client != nullptr); ++fixture::state.cleanups; --fixture::state.live;
    delete client; return ESP_OK;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *name, const char *value) {
    auto &s = fixture::state;
    if (++s.headers == s.headerFailAt) return ESP_FAIL;
    s.requestHeaders[name] = value;
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int writeLength) {
    auto &s = fixture::state;
    CHECK(writeLength == 0); ++s.opens;
    if (client->config.crt_bundle_attach) client->config.crt_bundle_attach(nullptr);
    s.nowUs += s.openDelayUs;
    if (s.cancelOpen) s.job->cancelled = true;
    if (s.screenOffOpen) s.screen = false;
    return s.openError;
}
void headerEvent(esp_http_client_handle_t client, const char *name, const char *value) {
    esp_http_client_event_t event{HTTP_EVENT_ON_HEADER, client->config.user_data,
                                 const_cast<char *>(name), const_cast<char *>(value)};
    CHECK(client->config.event_handler != nullptr);
    CHECK(client->config.event_handler(&event) == ESP_OK);
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
    auto &s = fixture::state; ++s.fetches;
    headerEvent(client, "Content-Type", "application/octet-stream");
    const std::string expected = FIXTURE_EXPECTED_PROOF;
    switch (s.proof) {
    case fixture::Proof::Valid: headerEvent(client, "X-Vault-Key", expected.c_str()); break;
    case fixture::Proof::Missing: break;
    case fixture::Proof::Wrong: headerEvent(client, "X-Vault-Key", std::string(64, '0').c_str()); break;
    case fixture::Proof::Duplicate:
        headerEvent(client, "X-Vault-Key", expected.c_str());
        headerEvent(client, "x-vault-key", expected.c_str()); break;
    case fixture::Proof::Short: headerEvent(client, "X-Vault-Key", expected.substr(0, 63).c_str()); break;
    case fixture::Proof::Uppercase: {
        std::string upper = expected;
        for (char &c : upper) if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
        headerEvent(client, "X-Vault-Key", upper.c_str()); break;
    }
    case fixture::Proof::Null: headerEvent(client, "X-Vault-Key", nullptr); break;
    case fixture::Proof::DifferentCaseHeader: headerEvent(client, "x-VaUlT-kEy", expected.c_str()); break;
    }
    if (s.status >= 300 && s.status <= 399)
        headerEvent(client, "Location", "https://untrusted.invalid/device/vault");
    s.nowUs += s.headersDelayUs;
    if (s.cancelHeaders) s.job->cancelled = true;
    if (s.sleepHeaders) s.sleeping = true;
    return s.contentLength == -2 ? static_cast<int64_t>(s.body.size()) : s.contentLength;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return fixture::state.status; }
int esp_http_client_read(esp_http_client_handle_t, char *destination, int length) {
    auto &s = fixture::state;
    ++s.reads;
    s.nowUs += s.readDelayUs;
    if (s.cancelReadAt == s.reads) s.job->cancelled = true;
    if (s.screenOffReadAt == s.reads) s.screen = false;
    if (s.sleepReadAt == s.reads) s.sleeping = true;
    if (s.failReadAt == s.reads) return -1;
    const size_t n = std::min({s.chunk, s.body.size() - s.offset, static_cast<size_t>(length)});
    if (n) std::memcpy(destination, s.body.data() + s.offset, n);
    s.offset += n;
    return static_cast<int>(n);
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t) {
    return fixture::state.complete && fixture::state.offset == fixture::state.body.size();
}
esp_err_t esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t, int *code, int *flags) {
    auto &s = fixture::state; ++s.tlsQueries;
    if (code) *code = s.tlsCode;
    if (flags) *flags = s.tlsFlags;
    return s.tlsError;
}
int esp_http_client_get_errno(esp_http_client_handle_t) { return fixture::state.socketErrno; }
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t) {
    ++fixture::state.follows; return ESP_OK;
}
int64_t esp_timer_get_time() { return fixture::state.nowUs; }
void vTaskDelay(int ticks) { fixture::state.nowUs += ticks * 1000; }
namespace vault {
bool hash(const void *input, size_t n, uint8_t output[32]) {
    ++fixture::state.hashes;
    if (fixture::state.hashFail) return false;
    vault_sha256_state context;
    vault_sha256_init(&context);
    return vault_sha256_process(&context, static_cast<const unsigned char *>(input),
                                static_cast<unsigned long>(n)) == 0 &&
           vault_sha256_done(&context, output) == 0;
}
}
#include "vault_origin.inc"
// Redirect only the time symbol inside the otherwise unchanged production
// implementation. The real C++ headers have already been included above.
namespace std { time_t vaultFixtureTime(time_t *output) {
    if (output) *output = fixture::state.unixTime;
    return fixture::state.unixTime;
} }
#define time vaultFixtureTime
#include "vault_sync.inc"
#undef time

namespace fixture {
void reset(const char *name) { state = Scenario{}; caseName = name; checks = 0; }
void finish() {
    CHECK(state.live == 0);
    CHECK(state.closes == state.cleanups);
    CHECK(state.cleanups == (state.inits && !state.initFail ? 1 : 0));
    CHECK(state.opens <= 1 && state.follows == 0);
    std::printf("{\"name\":\"%s\",\"passed\":true,\"checks\":%u,\"requests\":%d,\"reads\":%d}\n",
                caseName, checks, state.opens, state.reads);
}
bool call(vault::Job &job, vault::Bytes &output,
          const std::string &origin = "https://vault.example.com/",
          const std::string &ca = "", size_t maximum = 4096,
          const std::string &token = "synthetic-test-token") {
    state.job = &job;
    output.assign(8, 0x5a); // Early failures must erase any previous response.
    return vault::request(job, origin, ca, "/api/device/bundle", token, maximum, output);
}
void success(const std::string &origin, const std::string &expectedUrl,
             const std::string &ca = "", bool bundle = true) {
    vault::Job job; vault::Bytes output;
    CHECK(call(job, output, origin, ca));
    CHECK(job.error.empty());
    CHECK(output == state.body);
    CHECK(state.url == expectedUrl);
    CHECK(state.timeout == 10000 && state.noRedirect && !state.skipName);
    CHECK(state.hadBundle == bundle && state.hadCa == !ca.empty());
    CHECK(state.ca == ca && !state.globalCa);
    CHECK(state.requestHeaders["Authorization"] == "Bearer synthetic-test-token");
    CHECK(state.requestHeaders["X-Vault-Key"] == FIXTURE_EXPECTED_PROOF);
    CHECK(state.requestHeaders["Accept"] == "application/octet-stream");
    CHECK(state.requestHeaders["Accept-Encoding"] == "identity");
    CHECK(state.hashes == 1);
    finish();
}
void failure(const std::string &errorPart, const std::string &origin = "https://vault.example.com/",
             size_t maximum = 4096, bool cancelled = false) {
    vault::Job job; job.cancelled = cancelled; vault::Bytes output;
    CHECK(!call(job, output, origin, "", maximum));
    CHECK(output.empty());
    CHECK(!job.error.empty());
    if (!errorPart.empty()) CHECK(job.error.find(errorPart) != std::string::npos);
    finish();
}
}

int main() {
    using namespace fixture;
    reset("https_public_bundle_with_trailing_slash");
    success("https://vault.example.com/", "https://vault.example.com/api/device/bundle");
    reset("https_public_bundle_without_slash");
    success("https://vault.example.com", "https://vault.example.com/api/device/bundle");
    reset("https_custom_ca_preserved");
    success("https://private.example:9443/", "https://private.example:9443/api/device/bundle", "synthetic custom CA", false);
    reset("legacy_http_ip_port");
    success("http://192.168.1.10:8787", "http://192.168.1.10:8787/api/device/bundle", "", false);
    reset("http_ip_port_trailing_slash");
    success("http://192.168.1.10:8787/", "http://192.168.1.10:8787/api/device/bundle", "", false);
    reset("http_domain_preserved");
    success("http://vault.example:9080", "http://vault.example:9080/api/device/bundle", "", false);
    reset("https_clock_too_old"); state.unixTime = 1704067199; failure("时间"); CHECK(state.inits == 0);
    reset("https_clock_boundary"); state.unixTime = 1704067200;
    success("https://vault.example", "https://vault.example/api/device/bundle");
    reset("http_does_not_require_clock"); state.unixTime = 0;
    success("http://192.168.1.10:8787", "http://192.168.1.10:8787/api/device/bundle", "", false);
    const char *invalidOrigins[] = {"https://vault.example/path", "https://vault.example/?a=1",
        "https://user:pass@vault.example", "https://vault.example#fragment", "https://vault.example:0"};
    for (const char *origin : invalidOrigins) {
        reset("invalid_origin_rejected_before_request"); failure("地址", origin); CHECK(state.inits == 0);
    }
    for (int status : {301, 302, 303, 307, 308}) {
        reset("redirect_rejected_without_second_request"); state.status = status;
        failure("跳转"); CHECK(state.opens == 1 && state.reads == 0);
    }
    reset("unauthorized_401"); state.status = 401; failure("身份"); CHECK(state.reads == 0);
    reset("forbidden_403"); state.status = 403; failure("密钥"); CHECK(state.reads == 0);
    reset("empty_vault_404"); state.status = 404; failure("保存密码"); CHECK(state.reads == 0);
    for (int status : {502, 503, 504}) {
        reset("upstream_failure"); state.status = status; failure("转发"); CHECK(state.reads == 0);
    }
    const Proof badProofs[] = {Proof::Missing, Proof::Wrong, Proof::Duplicate, Proof::Short, Proof::Uppercase, Proof::Null};
    const char *proofNames[] = {"missing_proof", "wrong_proof", "duplicate_proof", "short_proof", "uppercase_proof", "null_proof"};
    for (size_t i = 0; i < 6; ++i) {
        reset(proofNames[i]); state.proof = badProofs[i]; failure("密钥校验"); CHECK(state.reads == 0);
    }
    reset("case_insensitive_proof_header"); state.proof = Proof::DifferentCaseHeader;
    success("https://vault.example", "https://vault.example/api/device/bundle");
    reset("complete_multibuffer_body"); state.body.resize(4096); state.chunk = 2048;
    for (size_t i = 0; i < state.body.size(); ++i) state.body[i] = static_cast<uint8_t>(i);
    success("https://vault.example", "https://vault.example/api/device/bundle");
    reset("complete_chunked_body"); state.contentLength = 0;
    success("https://vault.example", "https://vault.example/api/device/bundle");
    reset("truncated_body_is_cleared"); state.contentLength = 100; state.complete = false; failure("不完整");
    reset("negative_header_length"); state.contentLength = -1; failure("响应中断"); CHECK(state.reads == 0);
    reset("oversized_content_length"); state.contentLength = 4097; failure("上限"); CHECK(state.reads == 0);
    reset("oversized_chunked_body"); state.contentLength = 0; state.body.resize(4097); state.chunk = 2048;
    failure("上限"); CHECK(state.reads == 3);
    reset("body_read_failure_clears_partial_output"); state.failReadAt = 2; failure("中断");
    reset("cancelled_before_request"); failure("取消", "https://vault.example", 4096, true); CHECK(state.inits == 0);
    reset("screen_off_before_request"); state.screen = false; failure("取消"); CHECK(state.inits == 0);
    reset("sleep_pending_before_request"); state.sleeping = true; failure("取消"); CHECK(state.inits == 0);
    reset("wifi_disconnected"); state.connected = false; failure("Wi-Fi"); CHECK(state.inits == 0);
    reset("cancel_during_transfer_clears_partial_output"); state.cancelReadAt = 2; failure("取消"); CHECK(state.reads == 2);
    reset("screen_off_during_transfer_clears_partial_output"); state.screenOffReadAt = 2; failure("取消"); CHECK(state.reads == 2);
    reset("sleep_during_transfer_clears_partial_output"); state.sleepReadAt = 2; failure("取消"); CHECK(state.reads == 2);
    reset("slow_transfer_30_second_deadline"); state.readDelayUs = 10000000; failure("超时"); CHECK(state.reads == 3);
    reset("proof_hash_failure"); state.hashFail = true; failure("密钥校验"); CHECK(state.inits == 0);
    reset("client_allocation_failure"); state.initFail = true; failure("内存"); CHECK(state.opens == 0);
    for (int nth : {1, 2, 3, 4}) {
        reset("request_header_failure_cleans_client"); state.headerFailAt = nth; failure("初始化"); CHECK(state.opens == 0);
    }
    reset("no_token_does_not_add_authorization"); {
        vault::Job job; vault::Bytes output;
        CHECK(call(job, output, "https://vault.example", "", 4096, ""));
        CHECK(state.requestHeaders.count("Authorization") == 0);
        CHECK(state.requestHeaders["X-Vault-Key"] == FIXTURE_EXPECTED_PROOF); finish();
    }
    reset("dns_failure_has_distinct_message"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.tlsError = ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME; failure("域名"); CHECK(state.reads == 0);
    reset("certificate_failure_has_distinct_message"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.tlsError = ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED; state.tlsFlags = 4;
    failure("证书"); CHECK(state.reads == 0);
    reset("tls_handshake_failure"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.tlsError = ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED; state.tlsCode = -0x7200;
    failure("TLS"); CHECK(state.reads == 0);
    reset("connection_failure_has_distinct_message"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.tlsError = ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST; state.socketErrno = ECONNREFUSED;
    failure("连接"); CHECK(state.reads == 0);
    for (int flags : {MBEDTLS_X509_BADCERT_EXPIRED, MBEDTLS_X509_BADCERT_FUTURE}) {
        reset("certificate_time_invalid"); state.openError = ESP_ERR_HTTP_CONNECT;
        state.tlsFlags = flags; failure("日期"); CHECK(state.reads == 0);
    }
    reset("certificate_not_trusted"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.tlsFlags = MBEDTLS_X509_BADCERT_NOT_TRUSTED; failure("证书不受信任");
    reset("certificate_verify_error_without_flags"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.tlsCode = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED; failure("证书不受信任");
    for (int error : {ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT, ESP_ERR_ESP_TLS_SERVER_HANDSHAKE_TIMEOUT}) {
        reset("transport_timeout"); state.openError = ESP_ERR_HTTP_CONNECT;
        state.tlsError = error; failure("超时");
    }
    reset("socket_timeout"); state.openError = ESP_ERR_HTTP_CONNECT;
    state.socketErrno = ETIMEDOUT; failure("超时");
    // A blocking transport read may complete exactly as the user cancels or
    // the device begins sleeping. EOF must not turn cancellation into success.
    reset("cancel_at_eof_discards_complete_response"); state.cancelReadAt = 4; failure("取消");
    reset("screen_off_at_eof_discards_complete_response"); state.screenOffReadAt = 4; failure("取消");
    reset("sleep_at_eof_discards_complete_response"); state.sleepReadAt = 4; failure("取消");
    reset("deadline_reached_during_final_read"); state.chunk = 10; state.readDelayUs = 10000000;
    failure("超时");
    reset("cancel_during_open_skips_headers"); state.cancelOpen = true; failure("取消"); CHECK(state.fetches == 0);
    reset("screen_off_during_open_skips_headers"); state.screenOffOpen = true; failure("取消"); CHECK(state.fetches == 0);
    reset("deadline_during_open_skips_headers"); state.openDelayUs = 30000000; failure("超时"); CHECK(state.fetches == 0);
    reset("cancel_during_headers_skips_body"); state.cancelHeaders = true; failure("取消"); CHECK(state.reads == 0);
    reset("sleep_during_headers_skips_body"); state.sleepHeaders = true; failure("取消"); CHECK(state.reads == 0);
    reset("deadline_during_headers_skips_body"); state.headersDelayUs = 30000000; failure("超时"); CHECK(state.reads == 0);
    return 0;
}
