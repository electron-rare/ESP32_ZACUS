// file_share_service.cpp - lightweight mDNS peer discovery and local file intake.
#include "app/file_share_service.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

#if defined(ARDUINO_ARCH_ESP32) && __has_include(<ESPmDNS.h>)
#include <ESPmDNS.h>
#define ZACUS_HAS_MDNS 1
#else
#define ZACUS_HAS_MDNS 0
#endif

#if defined(ARDUINO_ARCH_ESP32) && __has_include(<HTTPClient.h>)
#include <HTTPClient.h>
#include <WiFiClient.h>
#define ZACUS_HAS_HTTPCLIENT 1
#else
#define ZACUS_HAS_HTTPCLIENT 0
#endif

#include <cstring>

namespace {

constexpr const char* kShareRoot = "/apps/shared";
constexpr const char* kIncomingRoot = "/apps/shared/incoming";
constexpr const char* kServiceType = "_zacus";
constexpr const char* kServiceProto = "_tcp";
constexpr uint16_t kServicePort = 8081U;
constexpr size_t kTransferChunkSize = 1024U;

void appendHexByte(String& out, uint8_t value) {
  constexpr char kHex[] = "0123456789ABCDEF";
  out += '%';
  out += kHex[(value >> 4U) & 0x0FU];
  out += kHex[value & 0x0FU];
}

String urlEncodeSimple(const char* text) {
  String out;
  if (text == nullptr) {
    return out;
  }
  for (size_t i = 0U; text[i] != '\0'; ++i) {
    const uint8_t ch = static_cast<uint8_t>(text[i]);
    const bool safe =
        (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
    if (safe) {
      out += static_cast<char>(ch);
    } else {
      appendHexByte(out, ch);
    }
  }
  return out;
}

bool ensureDir(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (LittleFS.exists(path)) {
    return true;
  }
  return LittleFS.mkdir(path);
}

}  // namespace

bool FileShareService::begin(const char* host_name, const char* instance_name) {
  copyText(host_name_, sizeof(host_name_), host_name);
  copyText(instance_name_, sizeof(instance_name_), instance_name);
  if (host_name_[0] == '\0') {
    copyText(host_name_, sizeof(host_name_), "zacus-freenove");
  }
  if (instance_name_[0] == '\0') {
    copyText(instance_name_, sizeof(instance_name_), "zacus-device");
  }
  ensureSharedDirs();
#if ZACUS_HAS_MDNS
  if (MDNS.begin(host_name_)) {
    MDNS.setInstanceName(instance_name_);
    MDNS.addService(kServiceType, kServiceProto, kServicePort);
    started_ = true;
    Serial.printf("[SHARE] mdns on host=%s instance=%s service=%s.%s port=%u\n",
                  host_name_,
                  instance_name_,
                  kServiceType,
                  kServiceProto,
                  static_cast<unsigned int>(kServicePort));
  } else {
    started_ = false;
    Serial.println("[SHARE] mdns init failed");
  }
#else
  started_ = false;
  Serial.println("[SHARE] mdns unavailable");
#endif
  next_discovery_ms_ = millis() + 1000U;
  return true;
}

void FileShareService::update(uint32_t now_ms) {
  if (static_cast<int32_t>(now_ms - next_discovery_ms_) < 0) {
    return;
  }
  next_discovery_ms_ = now_ms + 10000U;
#if ZACUS_HAS_MDNS
  if (started_) {
    (void)MDNS.queryService(kServiceType, kServiceProto);
  }
#endif
}

uint8_t FileShareService::discoverPeers(PeerInfo* out_peers, uint8_t max_peers) const {
  if (out_peers == nullptr || max_peers == 0U) {
    return 0U;
  }
  uint8_t count = 0U;
#if ZACUS_HAS_MDNS
  const int found = MDNS.queryService(kServiceType, kServiceProto);
  if (found <= 0) {
    return 0U;
  }
  for (int i = 0; i < found && count < max_peers; ++i) {
    PeerInfo& peer = out_peers[count];
    copyText(peer.instance, sizeof(peer.instance), MDNS.hostname(i).c_str());
    copyText(peer.host, sizeof(peer.host), MDNS.hostname(i).c_str());
    copyText(peer.ip, sizeof(peer.ip), MDNS.IP(i).toString().c_str());
    peer.port = static_cast<uint16_t>(MDNS.port(i));
    ++count;
  }
#else
  (void)out_peers;
  (void)max_peers;
#endif
  return count;
}

bool FileShareService::saveIncomingFile(const char* relative_path,
                                        const uint8_t* data,
                                        size_t length,
                                        String* out_saved_path) const {
  if (relative_path == nullptr || relative_path[0] == '\0' || data == nullptr || length == 0U) {
    return false;
  }
  if (!ensureSharedDirs()) {
    return false;
  }
  String path = relative_path;
  path.trim();
  if (path.isEmpty()) {
    return false;
  }
  if (path.startsWith("/")) {
    path = path.substring(1);
  }
  path.replace("..", "_");
  path.replace("\\", "/");
  String full_path = String(kIncomingRoot) + "/" + path;
  const int slash = full_path.lastIndexOf('/');
  if (slash > 0) {
    const String parent = full_path.substring(0, static_cast<unsigned int>(slash));
    if (!LittleFS.exists(parent.c_str())) {
      LittleFS.mkdir(parent.c_str());
    }
  }
  File file = LittleFS.open(full_path.c_str(), "w");
  if (!file) {
    return false;
  }
  const size_t written = file.write(data, length);
  file.close();
  if (written != length) {
    return false;
  }
  if (out_saved_path != nullptr) {
    *out_saved_path = full_path;
  }
  return true;
}

bool FileShareService::listSharedFiles(String* out_json) const {
  if (out_json == nullptr) {
    return false;
  }
  out_json->remove(0);
  if (!ensureSharedDirs()) {
    return false;
  }
  File folder = LittleFS.open(kIncomingRoot, "r");
  if (!folder || !folder.isDirectory()) {
    *out_json = "[]";
    return true;
  }

  DynamicJsonDocument doc(4096);
  JsonArray files = doc.to<JsonArray>();
  File entry = folder.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (!name.startsWith("/")) {
        name = "/" + name;
      }
      JsonObject item = files.createNestedObject();
      item["path"] = name;
      item["size"] = static_cast<uint32_t>(entry.size());
    }
    entry.close();
    entry = folder.openNextFile();
  }
  serializeJson(files, *out_json);
  folder.close();
  return true;
}

bool FileShareService::downloadFile(const char* requested_path, File* out_file, String* out_full_path) const {
  if (out_file == nullptr) {
    return false;
  }
  *out_file = File();
  String full_path;
  if (!resolveIncomingPath(requested_path, &full_path)) {
    return false;
  }
  *out_file = LittleFS.open(full_path.c_str(), "r");
  if (!(*out_file) || out_file->isDirectory()) {
    return false;
  }
  if (out_full_path != nullptr) {
    *out_full_path = full_path;
  }
  return true;
}

bool FileShareService::pullFromPeer(const char* peer_host,
                                    uint16_t peer_port,
                                    const char* remote_path,
                                    const char* local_path,
                                    const char* bearer_token,
                                    String* out_saved_path,
                                    String* out_error) const {
  if (out_saved_path != nullptr) {
    out_saved_path->remove(0);
  }
  if (out_error != nullptr) {
    out_error->remove(0);
  }
  if (peer_host == nullptr || peer_host[0] == '\0' || remote_path == nullptr || remote_path[0] == '\0') {
    if (out_error != nullptr) {
      *out_error = "invalid_pull_args";
    }
    return false;
  }
#if !ZACUS_HAS_HTTPCLIENT
  (void)peer_port;
  (void)local_path;
  (void)bearer_token;
  if (out_error != nullptr) {
    *out_error = "httpclient_unavailable";
  }
  return false;
#else
  String target_rel = (local_path != nullptr && local_path[0] != '\0') ? String(local_path) : String(remote_path);
  String target_path;
  if (!resolveIncomingPath(target_rel.c_str(), &target_path)) {
    if (out_error != nullptr) {
      *out_error = "invalid_local_path";
    }
    return false;
  }

  const int slash = target_path.lastIndexOf('/');
  if (slash > 0) {
    const String parent = target_path.substring(0, static_cast<unsigned int>(slash));
    if (!LittleFS.exists(parent.c_str()) && !LittleFS.mkdir(parent.c_str())) {
      if (out_error != nullptr) {
        *out_error = "mkdir_failed";
      }
      return false;
    }
  }

  File dst = LittleFS.open(target_path.c_str(), "w");
  if (!dst) {
    if (out_error != nullptr) {
      *out_error = "open_dst_failed";
    }
    return false;
  }

  const uint16_t port = (peer_port == 0U) ? 80U : peer_port;
  String url = "http://";
  url += peer_host;
  url += ":";
  url += String(static_cast<unsigned int>(port));
  url += "/api/share/download?path=";
  url += urlEncodeSimple(remote_path);

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    dst.close();
    LittleFS.remove(target_path.c_str());
    if (out_error != nullptr) {
      *out_error = "http_begin_failed";
    }
    return false;
  }
  if (bearer_token != nullptr && bearer_token[0] != '\0') {
    String auth = "Bearer ";
    auth += bearer_token;
    http.addHeader("Authorization", auth);
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    dst.close();
    LittleFS.remove(target_path.c_str());
    if (out_error != nullptr) {
      *out_error = String("http_") + String(code);
    }
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int remaining = http.getSize();
  uint8_t buffer[kTransferChunkSize];
  while (http.connected() && (remaining > 0 || remaining == -1)) {
    const size_t available = stream->available();
    if (available == 0U) {
      delay(1);
      continue;
    }
    const size_t to_read = (available > sizeof(buffer)) ? sizeof(buffer) : available;
    const size_t got = stream->readBytes(reinterpret_cast<char*>(buffer), to_read);
    if (got == 0U) {
      continue;
    }
    if (dst.write(buffer, got) != got) {
      http.end();
      dst.close();
      LittleFS.remove(target_path.c_str());
      if (out_error != nullptr) {
        *out_error = "write_failed";
      }
      return false;
    }
    if (remaining > 0) {
      remaining -= static_cast<int>(got);
    }
  }

  http.end();
  dst.close();
  if (out_saved_path != nullptr) {
    *out_saved_path = target_path;
  }
  return true;
#endif
}

bool FileShareService::resolveIncomingPath(const char* requested_path, String* out_full_path) const {
  if (requested_path == nullptr || requested_path[0] == '\0' || out_full_path == nullptr) {
    return false;
  }
  String path = requested_path;
  path.trim();
  if (path.isEmpty()) {
    return false;
  }
  path.replace("\\", "/");
  path.replace("..", "_");
  if (path.startsWith("/apps/shared/incoming/")) {
    *out_full_path = path;
    return true;
  }
  if (path.startsWith("/")) {
    path.remove(0, 1);
  }
  *out_full_path = String(kIncomingRoot) + "/" + path;
  return true;
}

void FileShareService::copyText(char* out, size_t out_size, const char* text) {
  if (out == nullptr || out_size == 0U) {
    return;
  }
  if (text == nullptr) {
    out[0] = '\0';
    return;
  }
  std::strncpy(out, text, out_size - 1U);
  out[out_size - 1U] = '\0';
}

bool FileShareService::ensureSharedDirs() const {
  return ensureDir("/apps") && ensureDir(kShareRoot) && ensureDir(kIncomingRoot);
}
