#include "vault_json.hpp"
#include "charset_conv.hpp"
#include "esp_random.h"
#include <cstring>
#include <set>
namespace vault {
namespace {
const char *text(cJSON *o, const char *name) {
  auto v = cJSON_GetObjectItemCaseSensitive(o, name);
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : nullptr;
}
bool number(cJSON *o, const char *name, double expected) {
  auto v = cJSON_GetObjectItemCaseSensitive(o, name);
  return cJSON_IsNumber(v) && v->valuedouble == expected;
}
void aad(const Session &s, uint8_t out[41]) {
  std::memcpy(out, "JYCONFIG2", 9);
  std::memcpy(out + 9, s.id.data(), 16);
  std::memcpy(out + 25, s.salt.data(), 16);
}
} // namespace
void wipeJson(cJSON *o) {
  if (!o)
    return;
  for (auto child = o->child; child; child = child->next)
    wipeJson(child);
  if (o->valuestring)
    wipe(o->valuestring, std::strlen(o->valuestring));
  if (o->string)
    wipe(o->string,
         std::strlen(
             o->string)); /* children are deleted once by the owner below */
}
Json::Json(const uint8_t *data, size_t size) {
  // cJSON stores C strings: embedded NUL escapes would silently truncate a
  // password. Reject them before parsing; WebCrypto never emits such data.
  if (!data || !size || std::memchr(data, 0, size) ||
      !core::isValidUtf8(reinterpret_cast<const char *>(data), size))
    return;
  // Bound JSON recursion before cJSON allocates or recurses; escaped text
  // such as a literal "\\u0000" is allowed, an actual NUL escape is not.
  unsigned depth = 0;
  bool quoted = false;
  for (size_t i = 0; i < size; ++i) {
    if (quoted) {
      if (data[i] == '\\') {
        if (i + 5 < size && data[i + 1] == 'u' && data[i + 2] == '0' &&
            data[i + 3] == '0' && data[i + 4] == '0' && data[i + 5] == '0')
          return;
        ++i;
      } else if (data[i] == '"')
        quoted = false;
    } else if (data[i] == '"')
      quoted = true;
    else if (data[i] == '{' || data[i] == '[') {
      if (++depth > 8)
        return;
    } else if (data[i] == '}' || data[i] == ']') {
      if (!depth)
        return;
      --depth;
    }
  }
  if (depth || quoted)
    return;
  const char *end = nullptr;
  value = cJSON_ParseWithLengthOpts(reinterpret_cast<const char *>(data), size,
                                    &end, false);
  if (value) {
    while (end < reinterpret_cast<const char *>(data) + size &&
           (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t'))
      ++end;
    if (end != reinterpret_cast<const char *>(data) + size) {
      wipeJson(value);
      cJSON_Delete(value);
      value = nullptr;
    }
  }
}
bool payload(const Bytes &data, Session &session) {
  Json doc(data.data(), data.size());
  if (!doc.value || !number(doc.value, "schema", 2))
    return false;
  auto entries = cJSON_GetObjectItemCaseSensitive(doc.value, "entries");
  if (!cJSON_IsArray(entries) || cJSON_GetArraySize(entries) > int(MaxEntries))
    return false;
  std::set<std::string> ids;
  session.entries.reserve(cJSON_GetArraySize(entries));
  cJSON *obj = nullptr;
  cJSON_ArrayForEach(obj, entries) {
    const char *id = text(obj, "id"), *title = text(obj, "title"),
               *user = text(obj, "username"), *pass = text(obj, "password"),
               *note = text(obj, "note"), *category = text(obj, "category"),
               *target = text(obj, "target");
    auto fav = cJSON_GetObjectItemCaseSensitive(obj, "favorite");
    if (!id || std::strlen(id) != 32 ||
        std::strspn(id, "0123456789abcdef") != 32 || !ids.insert(id).second ||
        !title || !user || !pass || !note || !category || !target ||
        !cJSON_IsBool(fav))
      return false;
    if (std::strcmp(category, "work") && std::strcmp(category, "app") &&
        std::strcmp(category, "web") && std::strcmp(category, "other"))
      return false;
    if (std::strlen(target) > 192)
      return false;
    size_t t = std::strlen(title), u = std::strlen(user), p = std::strlen(pass),
           n = std::strlen(note);
    if (!t || t > 96 || u > 128 || !p || p > 128 || n > MaxNoteBytes ||
        !printable(user, u) || !printable(pass, p))
      return false;
    bool visible = false;
    for (size_t i = 0; i < t; ++i) {
      if (uint8_t(title[i]) < 32 || uint8_t(title[i]) == 127)
        return false;
      if (title[i] != ' ')
        visible = true;
    }
    if (!visible)
      return false;
    session.entries.emplace_back();
    auto &e = session.entries.back();
    e.id = id;
    e.title = title;
    e.username = user;
    e.password = pass;
    e.note = note;
    e.category = category;
    e.target = target;
    e.favorite = cJSON_IsTrue(fav);
  }
  return true;
}
bool validOrigin(const std::string &url) {
  std::string normalized;
  return normalizeOrigin(url, normalized);
}
bool configDecode(const Bytes &config, Session &session) {
  if (config.size() < 28 || config.size() > 8192)
    return false;
  uint8_t a[41];
  aad(session, a);
  SecretBytes plain;
  if (!gcm(false, session.key, config.data(), a, sizeof(a), config.data() + 12,
           config.size() - 12, plain.value))
    return false;
  Json doc(plain.value.data(), plain.value.size());
  if (!doc.value || !number(doc.value, "schema", 2))
    return false;
  auto auth = cJSON_GetObjectItemCaseSensitive(doc.value, "auth");
  if (auth && (!cJSON_IsString(auth) || !auth->valuestring ||
               std::strcmp(auth->valuestring, "pattern-v1")))
    return false; // Unknown/corrupt credentials must never become a setup request.
  auto url = text(doc.value, "url"), salt = text(doc.value, "pin_salt"),
       hash = text(doc.value, "pin_hash");
  std::string origin;
  if (!url || !normalizeOrigin(url, origin) || !salt || !hash ||
      std::strlen(salt) != 32 ||
      std::strlen(hash) != 64 || std::strspn(salt, "0123456789abcdef") != 32 ||
      std::strspn(hash, "0123456789abcdef") != 64)
    return false;
  session.url = origin;
  session.pinSalt = salt;
  session.pinHash = hash;
  session.patternAuth = auth != nullptr;
  return true;
}
bool configEncode(const Session &session, Bytes &output) {
  if (!session.patternAuth) return false;
  cJSON *doc = cJSON_CreateObject();
  if (!doc)
    return false;
  bool ok = cJSON_AddNumberToObject(doc, "schema", 2) &&
            cJSON_AddStringToObject(doc, "auth", "pattern-v1") &&
            cJSON_AddStringToObject(doc, "url", session.url.c_str()) &&
            cJSON_AddStringToObject(doc, "pin_salt", session.pinSalt.c_str()) &&
            cJSON_AddStringToObject(doc, "pin_hash", session.pinHash.c_str());
  char *printed = ok ? cJSON_PrintUnformatted(doc) : nullptr;
  wipeJson(doc);
  cJSON_Delete(doc);
  if (!printed)
    return false;
  auto freeSecret = [](char *p) {
    if (p) {
      wipe(p, std::strlen(p));
      cJSON_free(p);
    }
  };
  std::unique_ptr<char, decltype(freeSecret)> printedOwner(printed, freeSecret);
  size_t size = std::strlen(printed);
  uint8_t a[41], nonce[12];
  aad(session, a);
  esp_fill_random(nonce, 12);
  Bytes cipher;
  ok = size < 8000 && gcm(true, session.key, nonce, a, sizeof(a),
                          reinterpret_cast<uint8_t *>(printed), size, cipher);
  printedOwner.reset();
  if (!ok)
    return false;
  output.assign(nonce, nonce + 12);
  output.insert(output.end(), cipher.begin(), cipher.end());
  return true;
}
} // namespace vault
