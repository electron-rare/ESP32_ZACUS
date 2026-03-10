# CHECKLIST D'INTÉGRATION DÉTAILLÉE - ENDPOINT PAR ENDPOINT
**Bearer Token Authentication - ESP32_ZACUS**  
**Version**: 1.0  
**Utilisé pour**: Validation que chaque endpoint est sécurisé

---

## 🔍 AVANT DE DÉMARRER

- [ ] `auth_service.h` créé dans `include/auth/`
- [ ] `auth_service.cpp` créé dans `src/auth/`
- [ ] `#include "auth/auth_service.h"` ajouté en haut de main.cpp
- [ ] `AuthService::init();` appelé dans `setup()` après Serial.begin()
- [ ] Compilation reussies: `pio run -e freenove_esp32s3 2>&1 | grep -i error`
- [ ] Fonction helper `validateAuthHeader()` créée dans main.cpp
- [ ] Flasher firmware et boot OK

---

## ✅ PHASE 2: ENDPOINTS CAMÉRA (Section ~2100 dans main.cpp)

Localiser la section:
```cpp
g_web_server.on("/api/camera/status", HTTP_GET, []() {
```

### Endpoint 2.1: `/api/camera/status` - GET

```cpp
AVANT:
g_web_server.on("/api/camera/status", HTTP_GET, []() {
  webSendCameraStatus();
});

APRÈS:
g_web_server.on("/api/camera/status", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webSendCameraStatus();
});
```

- [ ] Code modifié
- [ ] Compilation OK
- [ ] Test sans token: `curl http://IP/api/camera/status` → **401**
- [ ] Test avec token: `curl -H "Authorization: Bearer TOKEN" http://IP/api/camera/status` → **200**

### Endpoint 2.2: `/api/camera/on` - POST

```cpp
AVANT:
g_web_server.on("/api/camera/on", HTTP_POST, []() {
  const bool ok = g_camera.start();
  webSendResult("CAM_ON", ok);
});

APRÈS:
g_web_server.on("/api/camera/on", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = g_camera.start();
  webSendResult("CAM_ON", ok);
});
```

- [ ] Code modifié
- [ ] Compilation OK
- [ ] Test sans token: `curl -X POST http://IP/api/camera/on` → **401**
- [ ] Serial log: `[WEB] AUTH_DENIED status=Missing Authorization header`

### Endpoint 2.3: `/api/camera/off` - POST

```cpp
AVANT:
g_web_server.on("/api/camera/off", HTTP_POST, []() {
  g_camera.stop();
  webSendResult("CAM_OFF", true);
});

APRÈS:
g_web_server.on("/api/camera/off", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  g_camera.stop();
  webSendResult("CAM_OFF", true);
});
```

- [ ] Code modifié
- [ ] Test curl avec token → **200**

### Endpoint 2.4: `/api/camera/snapshot.jpg` - GET

```cpp
AVANT:
g_web_server.on("/api/camera/snapshot.jpg", HTTP_GET, []() {
  String out_path;
  if (!g_camera.snapshotToFile(nullptr, &out_path)) {
    g_web_server.send(500, "application/json", "{\"ok\":false,\"error\":\"camera_snapshot_failed\"}");
    return;
  }
  // ... reste du code ...
});

APRÈS:
g_web_server.on("/api/camera/snapshot.jpg", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (avant autre logic)
  String out_path;
  if (!g_camera.snapshotToFile(nullptr, &out_path)) {
    g_web_server.send(500, "application/json", "{\"ok\":false,\"error\":\"camera_snapshot_failed\"}");
    return;
  }
  // ... reste du code ...
});
```

- [ ] Code modifié (après g_web_server.on, avant String out_path declaration)
- [ ] Test sans token → **401**
- [ ] Test avec token → **200** (ou 500 si caméra non disponible)

**✅ Phase 2 Complétée**: 4 endpoints caméra sécurisés

---

## ✅ PHASE 3: ENDPOINTS MÉDIAS (Section ~2130 dans main.cpp)

### Endpoint 3.1: `/api/media/files` - GET

```cpp
g_web_server.on("/api/media/files", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webSendMediaFiles();
});
```

- [ ] Code modifié
- [ ] Test curl

### Endpoint 3.2: `/api/media/play` - POST

```cpp
g_web_server.on("/api/media/play", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  String path = g_web_server.arg("path");
  StaticJsonDocument<256> request_json;
  if (webParseJsonBody(&request_json) && path.isEmpty()) {
    path = request_json["path"] | request_json["file"] | "";
  }
  const bool ok = !path.isEmpty() && g_media.play(path.c_str(), &g_audio);
  webSendResult("MEDIA_PLAY", ok);
});
```

- [ ] Code modifié
- [ ] Test sans token → **401**
- [ ] Test avec token → **200**

### Endpoint 3.3: `/api/media/stop` - POST

```cpp
g_web_server.on("/api/media/stop", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = g_media.stop(&g_audio);
  webSendResult("MEDIA_STOP", ok);
});
```

- [ ] Code modifié

### Endpoint 3.4: `/api/media/record/start` - POST

```cpp
g_web_server.on("/api/media/record/start", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  uint16_t seconds = static_cast<uint16_t>(g_web_server.arg("seconds").toInt());
  String filename = g_web_server.arg("filename");
  StaticJsonDocument<256> request_json;
  if (webParseJsonBody(&request_json)) {
    if (request_json["seconds"].is<unsigned int>()) {
      seconds = static_cast<uint16_t>(request_json["seconds"].as<unsigned int>());
    }
    if (filename.isEmpty()) {
      filename = request_json["filename"] | "";
    }
  }
  const bool ok = g_media.startRecording(seconds, filename.isEmpty() ? nullptr : filename.c_str());
  webSendResult("REC_START", ok);
});
```

- [ ] Code modifié ⚠️ **Important: ajouter APRÈS lambda opening, avant uint16_t**

### Endpoint 3.5: `/api/media/record/stop` - POST

```cpp
g_web_server.on("/api/media/record/stop", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = g_media.stopRecording();
  webSendResult("REC_STOP", ok);
});
```

- [ ] Code modifié

### Endpoint 3.6: `/api/media/record/status` - GET

```cpp
g_web_server.on("/api/media/record/status", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webSendMediaRecordStatus();
});
```

- [ ] Code modifié
- [ ] Compilation OK
- [ ] Test de tous 6 endpoints

**✅ Phase 3 Complétée**: 6 endpoints médias sécurisés

---

## ✅ PHASE 4: ENDPOINTS RÉSEAU - WiFi (Section ~2170 dans main.cpp)

### Endpoint 4.1: `/api/network/wifi` - GET

```cpp
g_web_server.on("/api/network/wifi", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webSendWifiStatus();
});
```

- [ ] Code modifié

### Endpoint 4.2: `/api/wifi/connect` - POST

```cpp
g_web_server.on("/api/wifi/connect", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT String ssid)
  String ssid = g_web_server.arg("ssid");
  String password = g_web_server.arg("password");
  if (password.isEmpty()) {
    password = g_web_server.arg("pass");
  }
  StaticJsonDocument<768> request_json;
  if (webParseJsonBody(&request_json)) {
    if (ssid.isEmpty()) {
      ssid = request_json["ssid"] | "";
    }
    if (password.isEmpty()) {
      password = request_json["pass"] | request_json["password"] | "";
    }
  }
  if (ssid.isEmpty()) {
    webSendResult("WIFI_CONNECT", false);
    return;
  }
  const bool ok = g_network.connectSta(ssid.c_str(), password.c_str());
  webSendResult("WIFI_CONNECT", ok);
});
```

- [ ] Code modifié (après lambda opening)
- [ ] Test sans token → **401** (TRÈS IMPORTANT - hijacking prevention)

### Endpoint 4.3: `/api/network/wifi/connect` - POST (alias)

```cpp
g_web_server.on("/api/network/wifi/connect", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  String ssid = g_web_server.arg("ssid");
  // ... same as above ...
});
```

- [ ] Code modifié

### Endpoint 4.4: `/api/wifi/disconnect` - POST

```cpp
g_web_server.on("/api/wifi/disconnect", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webScheduleStaDisconnect();
  webSendResult("WIFI_DISCONNECT", true);
});
```

- [ ] Code modifié

### Endpoint 4.5: `/api/network/wifi/disconnect` - POST (alias)

```cpp
g_web_server.on("/api/network/wifi/disconnect", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webScheduleStaDisconnect();
  webSendResult("WIFI_DISCONNECT", true);
});
```

- [ ] Code modifié

### Endpoint 4.6: `/api/network/wifi/reconnect` - POST

```cpp
g_web_server.on("/api/network/wifi/reconnect", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = webReconnectLocalWifi();
  webSendResult("WIFI_RECONNECT", ok);
});
```

- [ ] Code modifié
- [ ] Compilation OK

**✅ WiFi Phase OK**: 6 endpoints WiFi sécurisés

---

## ✅ PHASE 4b: ENDPOINTS RÉSEAU - ESP-NOW (Section ~2175 dans main.cpp)

### Endpoint 4.7: `/api/network/espnow` - GET

```cpp
g_web_server.on("/api/network/espnow", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webSendEspNowStatus();
});
```

- [ ] Code modifié

### Endpoint 4.8: `/api/network/espnow/peer` - GET

```cpp
g_web_server.on("/api/network/espnow/peer", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  webSendEspNowPeerList();
});
```

- [ ] Code modifié
- [ ] Test: aucun client ne peut voir les pairs sans token

### Endpoint 4.9: `/api/espnow/send` - POST

```cpp
g_web_server.on("/api/espnow/send", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT String target)
  String target = g_web_server.arg("target");
  String payload = g_web_server.arg("payload");
  if (target.isEmpty()) {
    target = g_web_server.arg("mac");
  }
  StaticJsonDocument<768> request_json;
  if (webParseJsonBody(&request_json)) {
    if (target.isEmpty()) {
      target = request_json["target"] | request_json["mac"] | "broadcast";
    }
    if (payload.isEmpty()) {
      if (request_json["payload"].is<JsonVariantConst>()) {
        serializeJson(request_json["payload"], payload);
      } else {
        payload = request_json["payload"] | "";
      }
    }
  }
  if (target.isEmpty()) {
    target = "broadcast";
  }
  if (payload.isEmpty()) {
    webSendResult("ESPNOW_SEND", false);
    return;
  }
  const bool ok = g_network.sendEspNowTarget(target.c_str(), payload.c_str());
  webSendResult("ESPNOW_SEND", ok);
});
```

- [ ] Code modifié

### Endpoint 4.10: `/api/network/espnow/send` - POST (alias)

```cpp
g_web_server.on("/api/network/espnow/send", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  String target = g_web_server.arg("target");
  // ... same as above ...
});
```

- [ ] Code modifié

### Endpoint 4.11: `/api/network/espnow/on` - POST

```cpp
g_web_server.on("/api/network/espnow/on", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = g_network.enableEspNow();
  webSendResult("ESPNOW_ON", ok);
});
```

- [ ] Code modifié

### Endpoint 4.12: `/api/network/espnow/off` - POST

```cpp
g_web_server.on("/api/network/espnow/off", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  g_network.disableEspNow();
  webSendResult("ESPNOW_OFF", true);
});
```

- [ ] Code modifié

### Endpoint 4.13: `/api/network/espnow/peer` - POST

```cpp
g_web_server.on("/api/network/espnow/peer", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT String mac)
  String mac = g_web_server.arg("mac");
  StaticJsonDocument<256> request_json;
  if (webParseJsonBody(&request_json) && mac.isEmpty()) {
    mac = request_json["mac"] | "";
  }
  const bool ok = !mac.isEmpty() && g_network.addEspNowPeer(mac.c_str());
  webSendResult("ESPNOW_PEER_ADD", ok);
});
```

- [ ] Code modifié

### Endpoint 4.14: `/api/network/espnow/peer` - DELETE

```cpp
g_web_server.on("/api/network/espnow/peer", HTTP_DELETE, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT String mac)
  String mac = g_web_server.arg("mac");
  StaticJsonDocument<256> request_json;
  if (webParseJsonBody(&request_json) && mac.isEmpty()) {
    mac = request_json["mac"] | "";
  }
  const bool ok = !mac.isEmpty() && g_network.removeEspNowPeer(mac.c_str());
  webSendResult("ESPNOW_PEER_DEL", ok);
});
```

- [ ] Code modifié
- [ ] Compilation OK
- [ ] Test de tous 8 endpoints ESP-NOW

**✅ Phase 4 Complétée**: 14 endpoints réseau sécurisés (6 WiFi + 8 ESP-NOW)

---

## ✅ PHASE 5: ENDPOINTS SCÉNARIO & CONTRÔLE (Section ~2330 dans main.cpp)

### Endpoint 5.1: `/api/story/refresh-sd` - POST

```cpp
g_web_server.on("/api/story/refresh-sd", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = refreshStoryFromSd();
  webSendResult("STORY_REFRESH_SD", ok);
});
```

- [ ] Code modifié

### Endpoint 5.2: `/api/scenario/unlock` - POST

⚠️ **TRÈS CRITIQUE**: Cet endpoint débloque les étapes du jeu!

```cpp
g_web_server.on("/api/scenario/unlock", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = dispatchScenarioEventByName("UNLOCK", millis());
  webSendResult("UNLOCK", ok);
});
```

- [ ] Code modifié
- [ ] Test crucial: sans token → **401** (empêche triche)

### Endpoint 5.3: `/api/scenario/next` - POST

⚠️ **TRÈS CRITIQUE**: Cet endpoint saute les étapes!

```cpp
g_web_server.on("/api/scenario/next", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER
  const bool ok = notifyScenarioButtonGuarded(5U, false, millis(), "api_scenario_next");
  webSendResult("NEXT", ok);
});
```

- [ ] Code modifié
- [ ] Test sans token → **401**

### Endpoint 5.4: `/api/control` - POST

⚠️ **ULTRA CRITIQUE**: Endpoint universel qui peut faire n'importe quoi!

```cpp
g_web_server.on("/api/control", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT String action)
  String action = g_web_server.arg("action");
  StaticJsonDocument<768> request_json;
  if (webParseJsonBody(&request_json) && action.isEmpty()) {
    action = request_json["action"] | "";
  }
  String error;
  const bool ok = dispatchControlAction(action, millis(), &error);
  StaticJsonDocument<256> response;
  response["ok"] = ok;
  response["action"] = action;
  if (!ok && !error.isEmpty()) {
    response["error"] = error;
  }
  webSendJsonDocument(response, ok ? 200 : 400);
});
```

- [ ] Code modifié
- [ ] Test sans token → **401** (TRÈS IMPORTANT!)

**✅ Phase 5 Complétée**: 4 endpoints scénario sécurisés

---

## ✅ PHASE 6: ENDPOINTS HARDWARE (Section ~2024 dans main.cpp)

### Endpoint 6.1: `/api/hardware/led` - POST

```cpp
g_web_server.on("/api/hardware/led", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT int red)
  int red = g_web_server.arg("r").toInt();
  int green = g_web_server.arg("g").toInt();
  int blue = g_web_server.arg("b").toInt();
  int brightness = g_web_server.hasArg("brightness") ? g_web_server.arg("brightness").toInt() : FREENOVE_WS2812_BRIGHTNESS;
  bool pulse = true;
  if (g_web_server.hasArg("pulse")) {
    pulse = (g_web_server.arg("pulse").toInt() != 0);
  }
  StaticJsonDocument<256> request_json;
  if (webParseJsonBody(&request_json)) {
    // ... validation code ...
  }
  // ... rest of validation ...
  const bool ok = g_hardware.setManualLed(...);
  webSendResult("HW_LED_SET", ok);
});
```

- [ ] Code modifié

### Endpoint 6.2: `/api/hardware/led/auto` - POST

```cpp
g_web_server.on("/api/hardware/led/auto", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← AJOUTER (AVANT bool enabled)
  bool enabled = false;
  bool parsed = false;
  if (g_web_server.hasArg("enabled")) {
    parsed = parseBoolToken(g_web_server.arg("enabled").c_str(), &enabled);
  } else if (g_web_server.hasArg("value")) {
    parsed = parseBoolToken(g_web_server.arg("value").c_str(), &enabled);
  }
  StaticJsonDocument<128> request_json;
  if (!parsed && webParseJsonBody(&request_json)) {
    if (request_json["enabled"].is<bool>()) {
      enabled = request_json["enabled"].as<bool>();
      parsed = true;
    } else if (request_json["value"].is<bool>()) {
      enabled = request_json["value"].as<bool>();
      parsed = true;
    }
  }
  if (!parsed) {
    webSendResult("HW_LED_AUTO", false);
    return;
  }
  g_hardware_cfg.led_auto_from_scene = enabled;
  // ... rest of logic ...
  webSendResult("HW_LED_AUTO", true);
});
```

- [ ] Code modifié
- [ ] Compilation OK

**✅ Phase 6 Complétée**: 2 endpoints hardware sécurisés

---

## ✅ PHASE 7a: ENDPOINTS INFORMATIONNELS (Optionnel, Recommandé)

Ces endpoints exposent infos systèmes - Recommandation: ajouter auth

### Endpoint 7a.1: `/api/status` - GET

```cpp
g_web_server.on("/api/status", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← OPTIONNEL (recommandé)
  webSendStatus();
});
```

- [ ] Code modifié (optionnel)

### Endpoint 7a.2: `/api/stream` - GET

```cpp
g_web_server.on("/api/stream", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← OPTIONNEL (recommandé)
  webSendStatusSse();
});
```

- [ ] Code modifié (optionnel)

### Endpoint 7a.3: `/api/hardware` - GET

```cpp
g_web_server.on("/api/hardware", HTTP_GET, []() {
  if (!validateAuthHeader()) return;  // ← OPTIONNEL (recommandé)
  webSendHardwareStatus();
});
```

- [ ] Code modifié (optionnel)

---

## ✅ PHASE 7b: ENDPOINTS À LAISSER PUBLICS

### ❌ NE PAS ajouter validateAuthHeader() à:

- [ ] `/` (GET) - WebUI HTML statique
- [ ] `/webui/assets/*` (GET) - Images, fonts, SFX
- [ ] `onNotFound` (404)

Vérifier ces endpoints N'ONT PAS `if (!validateAuthHeader()) return;`:

```cpp
g_web_server.on("/", HTTP_GET, []() {
  // NO AUTH NEEDED - Assets publics
  g_web_server.send(200, "text/html", kWebUiIndex);
});

g_web_server.on("/webui/assets/header.png", HTTP_GET, []() {
  // NO AUTH NEEDED - Assets publics
  g_web_server.sendHeader("Cache-Control", "public, max-age=3600");
  // ...
});

g_web_server.onNotFound([]() {
  // NO AUTH NEEDED - 404
  g_web_server.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
});
```

- [ ] Vérifier `/` accessible sans token
- [ ] Vérifier `/webui/assets/*` accessible sans token
- [ ] Vérifier 404 handler aussi public

---

## ✅ PHASE 8: SERIAL COMMANDS (Pour tests & admin)

Localiser `handleSerialCommand()` fonction vers ligne ~1650

Ajouter **AVANT** le `Serial.printf("UNKNOWN %s\n"...` final:

```cpp
  if (std::strcmp(command, "AUTH_STATUS") == 0) {
    char token[AuthService::kTokenBufferSize];
    if (AuthService::getCurrentToken(token, sizeof(token))) {
      Serial.printf("AUTH_STATUS enabled=%u token=%s\n",
                    AuthService::isEnabled() ? 1U : 0U,
                    token);
    } else {
      Serial.println("AUTH_STATUS failed");
    }
    return;
  }

  if (std::strcmp(command, "AUTH_ROTATE_TOKEN") == 0) {
    char new_token[AuthService::kTokenBufferSize];
    if (AuthService::rotateToken(new_token, sizeof(new_token))) {
      Serial.printf("AUTH_ROTATE_SUCCESS new_token=%s\n", new_token);
    } else {
      Serial.println("AUTH_ROTATE_FAILED");
    }
    return;
  }

  if (std::strcmp(command, "AUTH_RESET") == 0) {
    if (AuthService::reset()) {
      Serial.println("AUTH_RESET_SUCCESS");
    } else {
      Serial.println("AUTH_RESET_FAILED");
    }
    return;
  }

  if (std::strcmp(command, "AUTH_DISABLE") == 0) {
    AuthService::setEnabled(false);
    Serial.println("ACK AUTH_DISABLE (testing only!)");
    return;
  }

  if (std::strcmp(command, "AUTH_ENABLE") == 0) {
    AuthService::setEnabled(true);
    Serial.println("ACK AUTH_ENABLE");
    return;
  }
```

- [ ] Code serial commands ajouté
- [ ] Test: `AUTH_STATUS` via serial → affiche token actuel
- [ ] Test: `AUTH_DISABLE` puis `AUTH_ENABLE` pour tester bypass

Mettre à jour HELP:

```cpp
  if (std::strcmp(command, "HELP") == 0) {
    Serial.println(
        "CMDS PING STATUS BTN_READ NEXT UNLOCK RESET "
        "SC_LIST SC_LOAD <id> SC_COVERAGE SC_REVALIDATE SC_REVALIDATE_ALL SC_EVENT <type> [name] SC_EVENT_RAW <name> "
        "STORY_REFRESH_SD STORY_SD_STATUS "
        "HW_STATUS HW_STATUS_JSON HW_LED_SET <r> <g> <b> [brightness] [pulse] HW_LED_AUTO <ON|OFF> HW_MIC_STATUS HW_BAT_STATUS "
        "MIC_TUNER_STATUS [ON|OFF|<period_ms>] "
        "CAM_STATUS CAM_ON CAM_OFF CAM_SNAPSHOT [filename] "
        "MEDIA_LIST <picture|music|recorder> MEDIA_PLAY <path> MEDIA_STOP REC_START [seconds] [filename] REC_STOP REC_STATUS "
        "NET_STATUS WIFI_STATUS WIFI_TEST WIFI_CONFIG <ssid> <password> WIFI_STA <ssid> <pass> WIFI_CONNECT <ssid> <pass> WIFI_DISCONNECT "
        "WIFI_AP_ON [ssid] [pass] WIFI_AP_OFF "
        "ESPNOW_ON ESPNOW_OFF ESPNOW_STATUS ESPNOW_STATUS_JSON ESPNOW_PEER_ADD <mac> ESPNOW_PEER_DEL <mac> ESPNOW_PEER_LIST "
        "ESPNOW_SEND <mac|broadcast> <text|json> "
        "AUDIO_TEST AUDIO_TEST_FS AUDIO_PROFILE <idx> AUDIO_STATUS VOL <0..21> AUDIO_STOP STOP "
        "AUTH_STATUS AUTH_ROTATE_TOKEN AUTH_RESET AUTH_ENABLE AUTH_DISABLE");  // ← AJOUTER cette ligne
    return;
  }
```

- [ ] HELP updated avec AUTH commands
- [ ] Compilation OK

---

## ✅ PHASE 9: TESTS COMPLETS

### Test 9.1: Sans Token

```bash
curl -v -X POST http://192.168.1.100:80/api/camera/on 2>&1 | head -30
```

**Expected Response**:
```
< HTTP/1.1 401 Unauthorized
< Content-Type: application/json
< ...
{"ok":false,"error":"unauthorized","reason":"Missing Authorization header"}
```

- [ ] PASS

### Test 9.2: Token Invalide

```bash
curl -v -H "Authorization: Bearer invalidtoken123" \
     -X POST http://192.168.1.100:80/api/camera/on
```

**Expected**:
```
< HTTP/1.1 401 Unauthorized
{"ok":false,"error":"unauthorized","reason":"Invalid token"}
```

- [ ] PASS

### Test 9.3: Token Valide (Caméra)

```bash
TOKEN="<from serial>"
curl -v -H "Authorization: Bearer $TOKEN" \
     -X POST http://192.168.1.100:80/api/camera/on
```

**Expected**:
```
< HTTP/1.1 200 OK
{"ok":true}
```

- [ ] PASS

### Test 9.4: Token Valide (Media)

```bash
TOKEN="<from serial>"
curl -v -H "Authorization: Bearer $TOKEN" \
     -X POST http://192.168.1.100:80/api/media/play \
     -H "Content-Type: application/json" \
     -d '{"path":"/music/test.mp3"}'
```

**Expected**: 200 OK

- [ ] PASS

### Test 9.5: Endpoints Publics (Sans Token)

```bash
curl -v http://192.168.1.100:80/webui/assets/favicon.png 2>&1 | head -20
```

**Expected**:
```
< HTTP/1.1 200 OK
< Content-Type: image/png
```

- [ ] PASS (pas 401)

### Test 9.6: Serial Commands

```bash
# Via serial monitor, envoyez:
AUTH_STATUS
AUTH_ROTATE_TOKEN
AUTH_STATUS
AUTH_ENABLE
AUTH_DISABLE
AUTH_ENABLE
```

**Expected**:
```
AUTH_STATUS enabled=1 token=550e8400e29b41d4a716446655440000
AUTH_ROTATE_SUCCESS new_token=6f7c9e2a1d8b3f5e7c0a1b2d3f4e5a6c
AUTH_STATUS enabled=1 token=6f7c9e2a1d8b3f5e7c0a1b2d3f4e5a6c
ACK AUTH_ENABLE
ACK AUTH_DISABLE (testing only!)
ACK AUTH_ENABLE
```

- [ ] PASS

### Test 9.7: Scénario Critique (Triche Prevention)

```bash
# Essayer de débloquer sans token
curl -X POST http://192.168.1.100:80/api/scenario/unlock

# Expected: 401 (Triche bloquée ✅)
# Avec token: 200 OK (Parent autorisé ✅)
```

- [ ] PASS: Sans token → 401
- [ ] PASS: Avec token → 200

### Test 9.8: Contrôle Universel (Ultra Critique)

```bash
# Essayer d'exécuter commande sans token
curl -X POST http://192.168.1.100:80/api/control \
     -H "Content-Type: application/json" \
     -d '{"action":"UNLOCK"}'

# Expected: 401 (Protection maximale ✅)
```

- [ ] PASS: 401

---

## ✅ FINAL CHECKLIST

### Compilation
- [ ] `pio run -e freenove_esp32s3` sans erreurs
- [ ] Pas de warnings `undefined reference to AuthService`

### Flasher
- [ ] `pio run -e freenove_esp32s3 -t upload --upload-port /dev/cu.usbmodem...`
- [ ] Upload réussi

### Boot Serial
- [ ] Log: `[AUTH] initialized token=...`
- [ ] Log: `[AUTH] use header: Authorization: Bearer ...`
- [ ] Log: `[WEB] started :80`

### Tests
- [ ] 9 endpoints caméra ✅
- [ ] 6 endpoints médias ✅
- [ ] 14 endpoints réseau ✅
- [ ] 4 endpoints scénario ✅
- [ ] 2 endpoints hardware ✅
- [ ] Assets publics accessibles sans token ✅
- [ ] Serial commands fonctionnent ✅

### Security
- [ ] Aucun endpoint critique accessible sans token
- [ ] 401 errors loggés correctement
- [ ] Token affiché au boot serial
- [ ] AUTH_DISABLE/ENABLE fonctionne

---

## 📊 RÉSUMÉ PAR GROUPE

| Groupe | Endpoints | Status | Effort |
|--------|-----------|--------|--------|
| Caméra | 4 | ✅ | 5 min |
| Médias | 6 | ✅ | 10 min |
| WiFi | 6 | ✅ | 10 min |
| ESP-NOW | 8 | ✅ | 10 min |
| Scénario | 4 | ✅ | 5 min |
| Hardware | 2 | ✅ | 5 min |
| Informationnels | 3 | ✅ (opt) | 5 min |
| Serial | 5 cmds | ✅ | 10 min |
| Tests | - | ✅ | 20 min |
| **TOTAL** | **34 endpoints** | ✅ | **~80-90 min** |

---

**Document**: Checklist Intégration Détaillée  
**Version**: 1.0  
**Statut**: ✅ Finalizado  
**Utilisation**: Cochez les cases au fur et à mesure de l'implémentation  
