#include "password_vault.hpp"
#include "sdkconfig.h"
// Flash reads/commits from this PSRAM stack require XiP: neither instructions
// nor rodata may depend on the flash cache during SPI1 operations.
#if !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || !CONFIG_SPIRAM_RODATA || !CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY
#error "Vault PSRAM stack requires XiP instructions/rodata and external task stacks"
#endif
#include "apps/hid_keyboard_model.hpp"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "power_mgr.hpp"
#include "system_service.hpp"
#include "vault_builtin.hpp"
#include "vault_json.hpp"
#include "vault_interaction.hpp"
#include "vault_storage.hpp"
#include "vault_sync.hpp"
#include "vault_connection.hpp"
#include <algorithm>
#include <cstring>
#include <new>
namespace vault {
namespace {
std::atomic<bool> active{false};
unsigned failures = 0;
int64_t nextUnlock = 0; // worker-only, retained across app reopen
bool stopped(Job &job) {
  return job.cancelled.load() || (!sys::Power::isScreenOn() && job.operation != Operation::WebUnlock) ||
         sys::Power::sleepPending();
}
std::shared_ptr<Session> decode(Job &job, const Bytes &bundle, const Key &key) {
  if (stopped(job) || !framing(bundle.data(), bundle.size())) {
    job.error = "已取消或密码文件格式错误";
    return {};
  }
  SecretBytes plain;
  if (!decryptBundle(bundle, key, plain.value)) {
    job.error = "密码文件校验失败，原数据未修改";
    return {};
  }
  void *memory =
      heap_caps_malloc(sizeof(Session), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!memory) {
    job.error = "密钥内存不足";
    return {};
  }
  auto session = std::shared_ptr<Session>(new (memory) Session, [](Session *p) {
    p->~Session();
    heap_caps_free(p);
  });
  session->key.bytes = key.bytes;
  session->revision = read32(bundle.data() + 16);
  std::memcpy(session->id.data(), bundle.data() + 24, 16);
  std::memcpy(session->salt.data(), bundle.data() + 40, 16);
  if (!payload(plain.value, *session)) {
    job.error = "密码条目格式不受支持";
    return {};
  }
  session->token = builtin::Token;
  return session;
}
bool enter(Job &job) {
  if (!job.input || job.entry >= job.input->entries.size() || !job.link ||
      !job.permit || stopped(job) ||
      sys::BleHid::privateLinkGeneration() != job.link) {
    job.error = "回车已取消，请检查目标连接";
    return false;
  }
  // Explicit button only: never append Enter to account/password typing.
  // Use the same guarded press/release channel, not the general HID queue.
  if (sys::BleHid::sendPrivateKey(0x28, 0, job.link, job.permit) != ESP_OK) {
    job.error = "回车发送失败，请在目标设备核对";
    return false;
  }
  job.progress.store(1);
  return true;
}
bool type(Job &job) {
  if (!job.input || job.entry >= job.input->entries.size() || !job.link ||
      !job.permit) {
    job.error = "输入目标无效";
    return false;
  }
  const auto &entry = job.input->entries[job.entry];
  const auto &text = job.username ? entry.username : entry.password;
  if (text.empty() || text.size() > 128 ||
      !printable(text.data(), text.size())) {
    job.error = "仅支持1–128位可打印ASCII";
    return false;
  }
  // The account click/password confirmation is the user's send action.
  // Begin immediately; keep per-character target/cancellation checks below.
  for (size_t i = 0; i < text.size(); ++i) {
    if (stopped(job) || sys::BleHid::privateLinkGeneration() != job.link) {
      job.error = "输入中断，已输入部分请手动清除";
      return false;
    }
    auto key = apps::keys::ascii(text[i]);
    if (sys::BleHid::sendPrivateKey(key.code, key.modifiers, job.link,
                                    job.permit) != ESP_OK) {
      job.error = "蓝牙输入中断，已输入部分请手动清除";
      return false;
    }
    job.progress.store(i + 1);
    if (i + 1 < text.size())
      vTaskDelay(pdMS_TO_TICKS(interaction::KeyGapMs));
  }
  return true;
}
std::string hex(const uint8_t *p, size_t n) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out(n * 2, '0');
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = digits[p[i] >> 4];
    out[2 * i + 1] = digits[p[i] & 15];
  }
  return out;
}
bool unhex(const std::string &s, uint8_t *p, size_t n) {
  if (s.size() != n * 2 ||
      s.find_first_not_of("0123456789abcdef") != std::string::npos)
    return false;
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (size_t i = 0; i < n; ++i)
    p[i] = (digit(s[2 * i]) << 4) | digit(s[2 * i + 1]);
  return true;
}
bool verifyPin(Job &job, const Session &session) {
  if (!session.patternAuth) {
    job.error = "请先在设备上设置图形密码";
    return false;
  }
  uint8_t salt[16];
  Key expected, verifier;
  if (!unhex(session.pinSalt, salt, 16) ||
      !unhex(session.pinHash, expected.bytes.data(), 32) ||
      !derive(job.master.c_str(), job.master.size(), salt, verifier,
              job.cancelled) || stopped(job)) {
    job.error = "已取消或密码验证失败";
    return false;
  }
  if (!equal(verifier.bytes.data(), expected.bytes.data(), 32)) {
    failures = std::min(failures + 1, 5u);
    nextUnlock = esp_timer_get_time() + (1LL << failures) * 1000000;
    job.error = "图形不正确，请稍后再试";
    return false;
  }
  failures = 0;
  nextUnlock = 0;
  return true;
}
void copyConfig(const Session &from, Session &to) {
  to.url = from.url;
  to.ca = from.ca;
  to.token = from.token;
  to.pinSalt = from.pinSalt;
  to.pinHash = from.pinHash;
  to.patternAuth = from.patternAuth;
}
void run(Job &job) {
  if (stopped(job)) {
    job.error = "已取消";
    return;
  }
  if (job.operation == Operation::Type) {
    job.ok = type(job);
    sys::BleHid::cancelPrivateInput();
    return;
  }
  if (job.operation == Operation::Enter) {
    job.ok = enter(job);
    return;
  }
  if (job.operation == Operation::Setup || job.operation == Operation::Unlock || job.operation == Operation::WebUnlock ||
      job.operation == Operation::VerifyPin ||
      job.operation == Operation::VerifyChangePin ||
      job.operation == Operation::VerifyAccess ||
      job.operation == Operation::ChangePin) {
    if (esp_timer_get_time() < nextUnlock) {
      job.error = "请稍后再试";
      return;
    }
    if (!pattern::valid(job.master.data(), job.master.size())) {
      job.error = "请连接至少4个不同的点";
      return;
    }
  }
  if (job.operation == Operation::VerifyPin) {
    if (!job.input || job.entry >= job.input->entries.size()) {
      job.error = "请先解锁并选择密码";
      return;
    }
    // Recheck the existing device PIN without reloading/decrypting the vault
    // or replacing the session. This operation cannot write any partition.
    job.ok = verifyPin(job, *job.input) && !stopped(job);
    return;
  }
  if (job.operation == Operation::VerifyChangePin ||
      job.operation == Operation::VerifyAccess) {
    // This also works with an empty vault. No entry selection or flash writes.
    if (!job.input) {
      job.error = "请先解锁密码本";
      return;
    }
    job.ok = verifyPin(job, *job.input) && !stopped(job);
    return;
  }
  if (job.operation == Operation::ChangePin &&
      !pattern::valid(job.replacement.data(), job.replacement.size())) {
    job.error = "新图形至少连接4个点";
    return;
  }
  Disk disk;
  if (!load(disk, job.error, job.cancelled))
    return;
  job.exists = disk.slot >= 0;
  job.blank = disk.blank;
  if (job.operation == Operation::Load) {
    job.needsPatternSetup = job.blank && !job.exists;
    if (job.exists) {
      // Inspect only the authenticated local config. Do not expose entries or
      // write the vault until the owner draws and confirms a new pattern.
      Session configSession;
      std::memcpy(configSession.key.bytes.data(), builtin::Key, 32);
      std::memcpy(configSession.id.data(), disk.bundle.data() + 24, 16);
      std::memcpy(configSession.salt.data(), disk.bundle.data() + 40, 16);
      if (!configDecode(disk.config, configSession)) {
        job.error = "密码区校验失败，原数据已保留";
        return;
      }
      job.needsPatternSetup = !configSession.patternAuth;
    }
    job.ok = true;
    return;
  }
  // Stable storage key only. Sync-key rotation must not affect saved data/PIN.
  Key key;
  std::memcpy(key.bytes.data(), builtin::Key, 32);
  Bytes bundle, config;
  std::shared_ptr<Session> result;
  if (job.operation == Operation::Setup) {
    if (job.exists) {
      // One-time, explicitly authorized legacy PIN -> pattern transition.
      // Preserve the encrypted bundle/revision/URL/identity byte for byte.
      bundle = disk.bundle;
      result = decode(job, bundle, key);
      if (!result || !configDecode(disk.config, *result)) {
        job.error = "本机配置读取失败，原数据已保留";
        return;
      }
      if (result->patternAuth) {
        job.error = "图形已设置，请重新解锁";
        return;
      }
    } else {
      if (!job.blank) { job.error = "密码区校验失败，原数据已保留"; return; }
      if (!encryptEmpty(key, builtin::Id, builtin::Salt, bundle)) {
        job.error = "初始化加密失败";
        return;
      }
      result = decode(job, bundle, key);
      if (!result) return;
      result->url = builtin::Url;
    }
    uint8_t salt[16];
    esp_fill_random(salt, sizeof(salt));
    Key verifier;
    if (!derive(job.master.c_str(), job.master.size(), salt, verifier,
                job.cancelled)) {
      job.error = "密码设置取消或失败";
      return;
    }
    result->pinSalt = hex(salt, sizeof(salt));
    result->pinHash = hex(verifier.bytes.data(), 32);
    result->patternAuth = true;
  } else if (job.operation == Operation::Unlock || job.operation == Operation::WebUnlock) {
    if (!job.exists) {
      job.error = "请先在设备上设置图形密码";
      return;
    }
    result = decode(job, disk.bundle, key);
    if (!result)
      return;
    if (!configDecode(disk.config, *result)) {
      job.error = "本机配置无效，请检查密码区";
      return;
    }
    if (!verifyPin(job, *result))
      return;
    job.output = std::move(result);
    job.ok = !stopped(job);
    return;
  } else if (job.operation == Operation::ChangePin) {
    if (!job.input || !job.exists) {
      job.error = "请先解锁密码本";
      return;
    }
    // Recheck the PIN from disk, not a stale UI session; retain bundle bytes,
    // revision, URL and fixed sync identity. Only the local verifier changes.
    bundle = disk.bundle;
    result = decode(job, bundle, key);
    if (!result || !configDecode(disk.config, *result)) {
      job.error = "本机配置读取失败";
      return;
    }
    if (!verifyPin(job, *result))
      return;
    uint8_t salt[16];
    esp_fill_random(salt, sizeof(salt));
    Key verifier;
    if (!derive(job.replacement.c_str(), job.replacement.size(), salt, verifier,
                job.cancelled) || stopped(job)) {
      job.error = "修改密码已取消";
      return;
    }
    result->pinSalt = hex(salt, sizeof(salt));
    result->pinHash = hex(verifier.bytes.data(), 32);
  } else if (job.operation == Operation::Sync ||
             job.operation == Operation::SaveConfig) {
    if (!job.input || !job.exists) {
      job.error = "请先解锁密码本";
      return;
    }
    const auto &current = *job.input;
    if (job.operation == Operation::SaveConfig) {
      std::string origin;
      if (!normalizeOrigin(job.url, origin)) {
        job.error = "请输入HTTP或HTTPS服务地址，不含页面路径";
        return;
      }
      bundle = disk.bundle;
      result = decode(job, bundle, current.key);
      if (!result)
        return;
      copyConfig(current, *result);
      result->url = origin;
    } else {
      // Validate the entire encrypted bundle before either endpoint can be committed.
      auto attempt = [&](const ConnectionConfig &connection) {
        job.error.clear();bundle.clear();result.reset();
        if (!request(job, connection.url, current.ca, "/api/device/bundle",
                     builtin::Token, MaxBundle, bundle)) return false;
        if (!framing(bundle.data(), bundle.size()) ||
            !equal(bundle.data() + 24, current.id.data(), 16) ||
            !equal(bundle.data() + 40, current.salt.data(), 16)) {
          job.error = "密码文件格式错误或身份不符";return false;
        }
        result = decode(job, bundle, current.key);
        if (!result) return false;
        // A manual sync accepts the server contents regardless of revision.
        // Identity, framing and authenticated payload validation still apply.
        copyConfig(current, *result);
        result->url=connection.url;
        return true;
      };
      if (!attempt(ConnectionConfig{current.url})) {
        if (stopped(job)) return;
        const std::string primaryError = job.error;
        // Give the GUI a visible transition, with cooperative cancellation.
        job.syncStage.store(1);
        for(unsigned i=0;i<10 && !stopped(job);++i) vTaskDelay(pdMS_TO_TICKS(100));
        if(stopped(job)) return;
        ConnectionConfig fromSd;
        if(!ConnectionConfig::readSd(fromSd,job.error,job.sdAccess)) { job.error="同步失败："+primaryError+"\nSD: "+job.error;return; }
        job.syncStage.store(2);
        if(stopped(job)) return;
        if(!attempt(fromSd)) { job.error="同步失败："+job.error;return; } // exactly one SD retry
        job.sdConfigUsed=true;
      }
      // Only an identical encrypted file at the same endpoint is a no-op.
      // A lower revision or different same-revision file must be persisted.
      if (bundle == disk.bundle && result->url == current.url) {
        job.output = std::move(result);
        job.ok = !stopped(job);
        return;
      }
      // Same bundle at a NEW working address must also persist the new config.

    }
  } else {
    job.error = "无效操作";
    return;
  }
  if (stopped(job)) {
    job.error = "已取消";
    return;
  }
  if (!configEncode(*result, config)) {
    job.error = "本机配置保存失败";
    return;
  }
  // A cancellation/readback error after the last header write has an unknown
  // durable outcome. The UI must reload instead of keeping an old verifier.
  job.configWriteStarted = true;
  if (!commit(disk, bundle, config, job.error, job.cancelled))
    return;
  job.output = std::move(result);
  job.ok = !stopped(job);
}
void worker(void *arg) {
  auto handle = static_cast<std::shared_ptr<Job> *>(arg);
  auto job = *handle;
  delete handle;
  {
    sys::Power::WorkGuard guard;
    try {
      run(*job);
    } catch (const std::bad_alloc &) {
      job->error = "内存不足，请退出后重试";
      job->ok = false;
    } catch (...) {
      job->error = "密码任务失败";
      job->ok = false;
    }
    clear(job->replacement);
    clear(job->master);
    job->input.reset();
    if (stopped(*job)) {
      job->ok = false;
      job->output.reset();
    }
    job->done.store(true);
  }
  job.reset();
  active.store(false);
  vTaskDeleteWithCaps(nullptr);
}
} // namespace
StartResult start(const std::shared_ptr<Job> &job) {
  if (!job)
    return StartResult::NoMemory;
  if (sys::Power::sleepPending() || (!sys::Power::isScreenOn() && job->operation != Operation::WebUnlock))
    return StartResult::Unavailable;
  if (!sys::BleHid::privateInputIdle() || active.exchange(true))
    return StartResult::Busy;
  job->sdAccess=core::sd::requestAccess(true);
  auto argument = new (std::nothrow) std::shared_ptr<Job>(job);
  if (!argument) {
    active.store(false);
    return StartResult::NoMemory;
  }
  // Keep the 12KiB crypto/HTTP headroom without competing with Wi-Fi/BLE for
  // contiguous internal RAM. IDF still allocates the small TCB internally.
  // The stack is allocated on demand and released by vTaskDeleteWithCaps.
  if (xTaskCreateWithCaps(worker, "vault_job", 12288, argument, 1, nullptr,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    delete argument;
    active.store(false);
    return StartResult::NoMemory;
  }
  return StartResult::Started;
}
bool idle() { return !active.load() && sys::BleHid::privateInputIdle(); }
void cancel(const std::shared_ptr<Job> &job) {
  if (job)
    job->cancelled.store(true);
  sys::BleHid::cancelPrivateInput();
}
} // namespace vault
