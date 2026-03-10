// file_share_service.h - Bonjour/mDNS discovery + simple file transfer hooks.
#pragma once

#include <Arduino.h>
#include <FS.h>

class StorageManager;
class NetworkManager;

class FileShareService {
 public:
  struct PeerInfo {
    char instance[48] = "";
    char host[48] = "";
    char ip[20] = "";
    uint16_t port = 0U;
  };

  static constexpr uint8_t kMaxPeers = 8U;

  bool begin(const char* host_name, const char* instance_name);
  void update(uint32_t now_ms);

  uint8_t discoverPeers(PeerInfo* out_peers, uint8_t max_peers) const;
  bool saveIncomingFile(const char* relative_path, const uint8_t* data, size_t length, String* out_saved_path) const;
  bool listSharedFiles(String* out_json) const;
  bool downloadFile(const char* requested_path, File* out_file, String* out_full_path) const;
  bool pullFromPeer(const char* peer_host,
                    uint16_t peer_port,
                    const char* remote_path,
                    const char* local_path,
                    const char* bearer_token,
                    String* out_saved_path,
                    String* out_error) const;

 private:
  static void copyText(char* out, size_t out_size, const char* text);
  bool ensureSharedDirs() const;
  bool resolveIncomingPath(const char* requested_path, String* out_full_path) const;

  bool started_ = false;
  uint32_t next_discovery_ms_ = 0U;
  char host_name_[40] = "zacus-freenove";
  char instance_name_[40] = "zacus-device";
};
