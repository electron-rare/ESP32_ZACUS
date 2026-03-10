# GUIDE D'INTÉGRATION RAPIDE - Bearer Token dans main.cpp

**Durée estimée**: 1-2 heures pour intégration complète de tous les endpoints critiques

---

## ÉTAPE 1: Include et Initialisation (5 minutes)

### 1.1 Ajouter l'include en haut de `main.cpp`
```cpp
// Avec les autres includes:
#include "auth/auth_service.h"
```

### 1.2 Appeler init() dans setup()
Localiser `void setup()` et ajouter **après** les includes/déclarations initiales:

```cpp
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[MAIN] Freenove all-in-one boot");

  // ← AJOUTER CETTE LIGNE:
  AuthService::init();  // Initialize Bearer token auth
  
  if (!g_storage.begin()) {
    Serial.println("[MAIN] storage init failed");
  }
  // ... reste du setup ...
}
```

### 1.3 Compiler pour vérifier
```bash
cd /Users/cils/Documents/Lelectron_rare/ESP32_ZACUS
pio run -e freenove_esp32s3 2>&1 | grep -i error
```

Si erreur: vérifier que `include/auth/auth_service.h` existe et est lisible.

---

## ÉTAPE 2: Créer Fonction Helper (10 minutes)

Dans `main.cpp`, ajouter **après** les fonctions de parsing existantes (vers ligne 200):

```cpp
// ============================================================================
// Web API Authentication Helper
// ============================================================================
// Valide Bearer token du header Authorization
// Retourne true si token valide, sinon envoie 401 et retourne false
// ============================================================================
inline bool validateAuthHeader() {
  const char* auth_header = g_web_server.header("Authorization").c_str();
  const AuthService::AuthStatus status = AuthService::validateBearerToken(auth_header);
  
  if (status != AuthService::AuthStatus::kOk) {
    Serial.printf("[WEB] AUTH_DENIED endpoint=? status=%s client=%s\n",
                  AuthService::statusMessage(status),
                  g_web_server.client().remoteIP().toString().c_str());
    
    // Retourner 401 Unauthorized
    StaticJsonDocument<256> response;
    response["ok"] = false;
    response["error"] = "unauthorized";
    response["reason"] = AuthService::statusMessage(status);
    webSendJsonDocument(response, 401);
    return false;
  }
  
  return true;
}
```

---

## ÉTAPE 3: Intégrer dans Endpoints (Phase 1 - Caméra)

### Localiser "setupWebUi()" dans main.cpp (vers ligne 2000+)

#### 3.1 Endpoint: /api/camera/on

**AVANT** (vulnérable):
```cpp
  g_web_server.on("/api/camera/on", HTTP_POST, []() {
    const bool ok = g_camera.start();
    webSendResult("CAM_ON", ok);
  });
```

**APRÈS** (sécurisé):
```cpp
  g_web_server.on("/api/camera/on", HTTP_POST, []() {
    if (!validateAuthHeader()) return;  // ← Une seule ligne ajoutée
    
    const bool ok = g_camera.start();
    webSendResult("CAM_ON", ok);
  });
```

#### 3.2 Endpoint: /api/camera/off

```cpp
  g_web_server.on("/api/camera/off", HTTP_POST, []() {
    if (!validateAuthHeader()) return;  // ← Ajouter
    
    g_camera.stop();
    webSendResult("CAM_OFF", true);
  });
```

#### 3.3 Endpoint: /api/camera/snapshot.jpg

```cpp
  g_web_server.on("/api/camera/snapshot.jpg", HTTP_GET, []() {
    if (!validateAuthHeader()) return;  // ← Ajouter
    
    String out_path;
    if (!g_camera.snapshotToFile(nullptr, &out_path)) {
      g_web_server.send(500, "application/json", "{\"ok\":false,\"error\":\"camera_snapshot_failed\"}");
      return;
    }
    // ... reste du code ...
  });
```

#### 3.4 Endpoint: /api/camera/status

```cpp
  g_web_server.on("/api/camera/status", HTTP_GET, []() {
    if (!validateAuthHeader()) return;  // ← Ajouter
    
    webSendCameraStatus();
  });
```

---

## ÉTAPE 4: Intégrer Endpoints Médias (Phase 2)

### Tous les endpoints affectés:

```cpp
  g_web_server.on("/api/media/play", HTTP_POST, []() {
    if (!validateAuthHeader()) return;  // ← Ajouter à tous
    String path = g_web_server.arg("path");
    // ... reste du code ...
  });

  g_web_server.on("/api/media/stop", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = g_media.stop(&g_audio);
    webSendResult("MEDIA_STOP", ok);
  });

  g_web_server.on("/api/media/record/start", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    // ... reste du code ...
  });

  g_web_server.on("/api/media/record/stop", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = g_media.stopRecording();
    webSendResult("REC_STOP", ok);
  });

  g_web_server.on("/api/media/record/status", HTTP_GET, []() {
    if (!validateAuthHeader()) return;
    webSendMediaRecordStatus();
  });

  g_web_server.on("/api/media/files", HTTP_GET, []() {
    if (!validateAuthHeader()) return;
    webSendMediaFiles();
  });
```

---

## ÉTAPE 5: Intégrer Endpoints Réseau (Phase 3)

### GET endpoints (informationnels):

```cpp
  g_web_server.on("/api/network/wifi", HTTP_GET, []() {
    if (!validateAuthHeader()) return;
    webSendWifiStatus();
  });

  g_web_server.on("/api/network/espnow", HTTP_GET, []() {
    if (!validateAuthHeader()) return;
    webSendEspNowStatus();
  });

  g_web_server.on("/api/network/espnow/peer", HTTP_GET, []() {
    if (!validateAuthHeader()) return;
    webSendEspNowPeerList();
  });
```

### POST endpoints (actions):

```cpp
  g_web_server.on("/api/wifi/connect", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    String ssid = g_web_server.arg("ssid");
    // ... reste du code ...
  });

  g_web_server.on("/api/network/wifi/connect", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    String ssid = g_web_server.arg("ssid");
    // ... reste du code ...
  });

  g_web_server.on("/api/wifi/disconnect", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    webScheduleStaDisconnect();
    webSendResult("WIFI_DISCONNECT", true);
  });

  g_web_server.on("/api/network/wifi/disconnect", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    webScheduleStaDisconnect();
    webSendResult("WIFI_DISCONNECT", true);
  });

  g_web_server.on("/api/network/wifi/reconnect", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = webReconnectLocalWifi();
    webSendResult("WIFI_RECONNECT", ok);
  });

  g_web_server.on("/api/espnow/send", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    String target = g_web_server.arg("target");
    // ... reste du code ...
  });

  g_web_server.on("/api/network/espnow/send", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    String target = g_web_server.arg("target");
    // ... reste du code ...
  });

  g_web_server.on("/api/network/espnow/on", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = g_network.enableEspNow();
    webSendResult("ESPNOW_ON", ok);
  });

  g_web_server.on("/api/network/espnow/off", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    g_network.disableEspNow();
    webSendResult("ESPNOW_OFF", true);
  });

  g_web_server.on("/api/network/espnow/peer", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    String mac = g_web_server.arg("mac");
    // ... reste du code ...
  });

  g_web_server.on("/api/network/espnow/peer", HTTP_DELETE, []() {
    if (!validateAuthHeader()) return;
    String mac = g_web_server.arg("mac");
    // ... reste du code ...
  });
```

---

## ÉTAPE 6: Intégrer Endpoints Scénario & Contrôle (Phase 4)

```cpp
  g_web_server.on("/api/scenario/unlock", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = dispatchScenarioEventByName("UNLOCK", millis());
    webSendResult("UNLOCK", ok);
  });

  g_web_server.on("/api/scenario/next", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = notifyScenarioButtonGuarded(5U, false, millis(), "api_scenario_next");
    webSendResult("NEXT", ok);
  });

  g_web_server.on("/api/control", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    String action = g_web_server.arg("action");
    // ... reste du code ...
  });

  g_web_server.on("/api/story/refresh-sd", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    const bool ok = refreshStoryFromSd();
    webSendResult("STORY_REFRESH_SD", ok);
  });
```

---

## ÉTAPE 7: Intégrer Endpoints Hardware (Phase 5)

```cpp
  g_web_server.on("/api/hardware/led", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    int red = g_web_server.arg("r").toInt();
    // ... reste du code ...
  });

  g_web_server.on("/api/hardware/led/auto", HTTP_POST, []() {
    if (!validateAuthHeader()) return;
    bool enabled = false;
    // ... reste du code ...
  });
```

---

## ÉTAPE 8: Endpoints à Garder PUBLICS (ne pas ajouter auth)

**NE PAS ajouter `validateAuthHeader()` à ces endpoints:**

```cpp
  // ✅ Reste PUBLIC (WebUI statique)
  g_web_server.on("/", HTTP_GET, []() {
    g_web_server.send(200, "text/html", kWebUiIndex);
  });

  // ✅ Reste PUBLIC (Assets)
  g_web_server.on("/webui/assets/header.png", HTTP_GET, []() {
    // ...
  });
  
  // ✅ Reste PUBLIC (Info-only, non-sensible)
  g_web_server.on("/api/status", HTTP_GET, []() {
    webSendStatus();
  });

  g_web_server.on("/api/stream", HTTP_GET, []() {
    webSendStatusSse();
  });
```

---

## ÉTAPE 9: Ajouter Serial Commands (Phase 6)

Localiser `handleSerialCommand()` dans main.cpp et ajouter **avant** le `Serial.printf("UNKNOWN %s\n", command_line);` final:

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

Puis ajouter aux HELP:

```cpp
  if (std::strcmp(command, "HELP") == 0) {
    Serial.println(
        "CMDS PING STATUS BTN_READ NEXT UNLOCK RESET "
        // ... autres commands ...
        "AUTH_STATUS AUTH_ROTATE_TOKEN AUTH_RESET AUTH_ENABLE AUTH_DISABLE "  // ← Ajouter cette ligne
        );
    return;
  }
```

---

## ÉTAPE 10: Compiler & Flasher

```bash
# Compiler
pio run -e freenove_esp32s3

# Flasher
pio run -e freenove_esp32s3 -t upload --upload-port /dev/cu.usbmodem5AB90753301

# Monitor serial
pio device monitor -p /dev/cu.usbmodem5AB90753301 -b 115200
```

Vérifier au boot:
```
[AUTH] initialized token=550e8400e29b41d4a716446655440000
[AUTH] use header: Authorization: Bearer 550e8400e29b41d4a716446655440000
[WEB] started :80
```

---

## ÉTAPE 11: Tester avec curl

```bash
TOKEN="550e8400e29b41d4a716446655440000"
IP="192.168.1.100"

# Test 1: Sans token → 401
curl -X POST http://$IP:80/api/camera/on
# Expect: {"ok":false,"error":"unauthorized","reason":"Missing Authorization header"}

# Test 2: Token invalide → 401
curl -H "Authorization: Bearer invalid123" \
     -X POST http://$IP:80/api/camera/on
# Expect: {"ok":false,"error":"unauthorized","reason":"Invalid token"}

# Test 3: Token valide → 200
curl -H "Authorization: Bearer $TOKEN" \
     -X POST http://$IP:80/api/camera/on
# Expect: {"ok":true} ou {"ok":false,"error":"camera_already_on"}

# Test 4: Assets publics (pas de token requis)
curl http://$IP:80/webui/assets/favicon.png
# Expect: 200 + PNG data
```

---

## CHECKLIST D'INTÉGRATION

### Phase 1: Fondation
- [ ] Include dans main.cpp (`#include "auth/auth_service.h"`)
- [ ] Appel `AuthService::init()` dans `setup()`
- [ ] Compilation OK
- [ ] Flasher, vérifier log `[AUTH] initialized token=...`

### Phase 2: Caméra (5 min)
- [ ] `/api/camera/on`
- [ ] `/api/camera/off`
- [ ] `/api/camera/snapshot.jpg`
- [ ] `/api/camera/status`

### Phase 3: Médias (10 min)
- [ ] `/api/media/play`
- [ ] `/api/media/stop`
- [ ] `/api/media/record/start`
- [ ] `/api/media/record/stop`
- [ ] `/api/media/record/status`
- [ ] `/api/media/files`

### Phase 4: Réseau (15 min)
- [ ] `/api/network/wifi` (GET)
- [ ] `/api/wifi/connect` (POST)
- [ ] `/api/network/wifi/connect` (POST)
- [ ] `/api/wifi/disconnect` (POST)
- [ ] `/api/network/wifi/disconnect` (POST)
- [ ] `/api/network/wifi/reconnect` (POST)
- [ ] `/api/network/espnow*` (5 endpoints)

### Phase 5: Scénario + Hardware (10 min)
- [ ] `/api/scenario/unlock`
- [ ] `/api/scenario/next`
- [ ] `/api/control`
- [ ] `/api/story/refresh-sd`
- [ ] `/api/hardware/led`
- [ ] `/api/hardware/led/auto`

### Phase 6: Logging & Serial (5 min)
- [ ] Fonction `validateAuthHeader()`
- [ ] Serial commands: `AUTH_STATUS`, `AUTH_ROTATE_TOKEN`, `AUTH_RESET`
- [ ] HELP updated

### Phase 7: Test (20 min)
- [ ] Test sans token → 401
- [ ] Test token invalide → 401
- [ ] Test token valide → 200
- [ ] Test assets publics → 200 (sans token)
- [ ] Test serial commands

---

## Problèmes Courants & Solutions

### ❌ Erreur: "unknown type name 'AuthService'"
**Cause**: Include manquant  
**Solution**: Ajouter `#include "auth/auth_service.h"` en haut de main.cpp

### ❌ Erreur: "undeclared identifier 'validateAuthHeader'"
**Cause**: Fonction helper pas encore créée ou mal placée  
**Solution**: Copier le code de la section "Créer fonction helper" et le placer avant `setupWebUi()`

### ❌ Endpoint 401 même avec bon token
**Cause**: Whitespace dans token (newline, espace)  
**Solution**: Vérifier que serial affiche bien le token complet, le faire un copy/paste exact

### ❌ Token différent à chaque boot
**Cause**: Normal! Token généré aléatoirement à chaque démarrage (NVS pas prêt)  
**Solution**: Phase 2 implémente persistence en NVS si besoin

---

## Durée Estimée par Phase

| Phase | Description | Durée | Cumulé |
|-------|-------------|-------|--------|
| 1 | Include + init | 5 min | 5 min |
| 2 | Fonction helper | 10 min | 15 min |
| 3 | Caméra (4 endpoints) | 5 min | 20 min |
| 4 | Médias (6 endpoints) | 10 min | 30 min |
| 5 | Réseau (10 endpoints) | 15 min | 45 min |
| 6 | Scénario + hardware (6 endpoints) | 10 min | 55 min |
| 7 | Serial commands + HELP | 10 min | 65 min |
| 8 | Tests curl | 20 min | 85 min |

**Total: ~85-90 minutes (~1.5 heures) pour intégration complète**

---

## Notes Finales

✅ **À faire immédiatement:**
1. Copier `auth_service.h` et `auth_service.cpp`
2. Ajouter `#include` et appeler `init()`
3. Intégrer endpoints phase par phase (15-20 min each)

⚠️ **À éviter:**
- Ne pas désactiver auth en production (`AuthService::setEnabled(false)`)
- Ne pas modifier format Bearer token (doit rester "Bearer <32-hex-chars>")
- Ne pas commiter token dans git 

🔒 **Sécurité:**
- Token généré aléatoirement à chaque boot (sufficient MVP)
- Pour production: ajouter HTTPS (TLS), token persistence (NVS chiffré), rate limiting

---

**Version**: 1.0  
**Date**: 2026-03-01  
**Auteur**: Implementation Guide - Bearer Token Auth  
