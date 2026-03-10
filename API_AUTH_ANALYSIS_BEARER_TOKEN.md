# ANALYSE DÉTAILLÉE DES ENDPOINTS API REST - BEARER TOKEN AUTHENTICATION
**ESP32_ZACUS - AsyncWebServer sur port 80**  
**Date**: 2026-03-01  
**Auteur**: Audit de Sécurité API

---

## RÉSUMÉ EXÉCUTIF

### État Actuel
- **Serveur Web**: AsyncWebServer (Arduino ESP32) sur port 80
- **Endpoints Identifiés**: **31 endpoints API** (9 endpoints assets supplémentaires)
- **Authentification**: **AUCUNE** ⚠️ CRITIQUE
- **Contrôle d'Accès**: Aucun
- **Données Sensibles Exposées**: Caméra, Audio, Scénarios, Réseau (WiFi/EspNow)

### Recommandation Immédiate
🔴 **CRITIQUE**: Tous les endpoints sensibles (caméra, audio, scénario) sont accessibles sans authentification. **Implémentation Bearer token obligatoire avant production**.

---

## 1. INVENTAIRE COMPLET DES ENDPOINTS API

### 1.1 Endpoints de Base & Status

| # | Endpoint | Méthode | Critique? | Description | Vulnérabilité Actuelle |
|---|----------|---------|-----------|-------------|----------------------|
| 1 | `/` | GET | ❌ Non | Serve WebUI HTML | Public (OK) |
| 2 | `/api/status` | GET | ⚠️ Moyen | JSON status système global | Expose tout (ips, versions) |
| 3 | `/api/stream` | GET | ⚠️ Moyen | SSE (Server-Sent Events) stream | Connexion persistante non auth |

### 1.2 Endpoints Matériel (Hardware)

| # | Endpoint | Méthode | Critique? | Description | Vulnérabilité Actuelle |
|---|----------|---------|-----------|-------------|----------------------|
| 4 | `/api/hardware` | GET | ⚠️ Moyen | Status LED, batterie, mic, température | Info système exposée |
| 5 | `/api/hardware/led` | POST | 🔴 CRITIQUE | Contrôle LED RGB (on/off/couleur) | **Pouvoir modifier hardware** |
| 6 | `/api/hardware/led/auto` | POST | 🔴 CRITIQUE | Activer/désactiver LED auto | **Pouvoir modifier comportement** |

### 1.3 Endpoints Caméra

| # | Endpoint | Méthode | Critique? | Description | Vulnérabilité Actuelle |
|---|----------|---------|-----------|-------------|----------------------|
| 7 | `/api/camera/status` | GET | 🔴 CRITIQUE | Status caméra (on/off, résolution) | **Révèle si caméra active/inactive** |
| 8 | `/api/camera/on` | POST | 🔴 CRITIQUE | Activer caméra | **SURVEILLANCE NON AUTORISÉE** |
| 9 | `/api/camera/off` | POST | 🔴 CRITIQUE | Désactiver caméra | Arrêt surveillance possible |
| 10 | `/api/camera/snapshot.jpg` | GET | 🔴 CRITIQUE | Récupérer image JPEG snapshot | **ACCÈS NON AUTORISÉ À VIDÉO** |

### 1.4 Endpoints Média (Audio/Musique/Enregistrement)

| # | Endpoint | Méthode | Critique? | Description | Vulnérabilité Actuelle |
|---|----------|---------|-----------|-------------|----------------------|
| 11 | `/api/media/files` | GET | ⚠️ Moyen | Liste fichiers media (musique, enregistrement) | Énumération des fichiers |
| 12 | `/api/media/play` | POST | 🔴 CRITIQUE | Lancer audio/musique | **Lancer son arbitraire** |
| 13 | `/api/media/stop` | POST | 🔴 CRITIQUE | Arrêter lecture audio | Arrêt possible |
| 14 | `/api/media/record/start` | POST | 🔴 CRITIQUE | Démarrer enregistrement micro | **CAPTURER AUDIO PASSIF** |
| 15 | `/api/media/record/stop` | POST | 🔴 CRITIQUE | Arrêter enregistrement | Arrêt possible |
| 16 | `/api/media/record/status` | GET | 🔴 CRITIQUE | Status enregistrement (recording, durée) | Expose si en cours enregistrement |

### 1.5 Endpoints Réseau (WiFi & ESP-NOW)

| # | Endpoint | Méthode | Critique? | Description | Vulnérabilité Actuelle |
|---|----------|---------|-----------|-------------|----------------------|
| 17 | `/api/network/wifi` | GET | 🔴 CRITIQUE | Status WiFi + liste réseaux visibles | **Énumérer réseaux + révéler connexion actuelle** |
| 18 | `/api/network/espnow` | GET | 🔴 CRITIQUE | Status ESP-NOW (enabled/disabled) | Infos réseau mesh exposées |
| 19 | `/api/network/espnow/peer` | GET | 🔴 CRITIQUE | Liste pairs ESP-NOW avec MAC addresses | **ÉNUMÉRATION APPAREILS PAIRS** |
| 20 | `/api/wifi/disconnect` | POST | 🔴 CRITIQUE | Déconnection WiFi STA | **DOS - COUPER RÉSEAU** |
| 21 | `/api/network/wifi/disconnect` | POST | 🔴 CRITIQUE | Déconnection WiFi STA (alias) | **DOS - COUPER RÉSEAU** |
| 22 | `/api/network/wifi/reconnect` | POST | 🔴 CRITIQUE | Reconnecter WiFi | **RÉTABLIR NON AUTORISÉ** |
| 23 | `/api/wifi/connect` | POST | 🔴 CRITIQUE | Connecter à WiFi custom (SSID + Pass) | **HIJACKER RÉSEAU** |
| 24 | `/api/network/wifi/connect` | POST | 🔴 CRITIQUE | Connecter WiFi (alias) | **HIJACKER RÉSEAU** |
| 25 | `/api/espnow/send` | POST | 🔴 CRITIQUE | Envoyer message ESP-NOW | **SPAM MESH NETWORK** |
| 26 | `/api/network/espnow/send` | POST | 🔴 CRITIQUE | Envoyer ESP-NOW (alias) | **SPAM MESH NETWORK** |
| 27 | `/api/network/espnow/on` | POST | 🔴 CRITIQUE | Activer ESP-NOW | **Activer communication mesh** |
| 28 | `/api/network/espnow/off` | POST | 🔴 CRITIQUE | Désactiver ESP-NOW | **DOS - Couper mesh** |
| 29 | `/api/network/espnow/peer` | POST | 🔴 CRITIQUE | Ajouter pair ESP-NOW | **AJOUTER APPAREILS MALVEILLANTS** |
| 30 | `/api/network/espnow/peer` | DELETE | 🔴 CRITIQUE | Supprimer pair ESP-NOW | **EXCLURE APPAREILS LÉGITIMES** |

### 1.6 Endpoints Scénario & Contrôle (Story Engine)

| # | Endpoint | Méthode | Critique? | Description | Vulnérabilité Actuelle |
|---|----------|---------|-----------|-------------|----------------------|
| 31 | `/api/story/refresh-sd` | POST | ⚠️ Moyen | Recharger scénarios depuis SD card | Recharge forcée possible |
| 32 | `/api/scenario/unlock` | POST | 🔴 CRITIQUE | Débloquer étape scénario | **BYPASSER LOGIC JEUX** |
| 33 | `/api/scenario/next` | POST | 🔴 CRITIQUE | Aller à étape suivante | **BYPASSER LOGIC JEUX** |
| 34 | `/api/control` | POST | 🔴 CRITIQUE | Action arbitraire (LED, média, scénario) | **COMMANDE UNIVERSELLE** |

### 1.7 Endpoints Assets (WebUI)

| # | Endpoint | Méthode | Critique? | Description |
|---|----------|---------|-----------|-------------|
| 35 | `/webui/assets/header.png` | GET | ❌ Public | Image header |
| 36 | `/webui/assets/favicon.png` | GET | ❌ Public | Favicon |
| 37 | `/webui/assets/fonts/PressStart2P-Regular.ttf` | GET | ❌ Public | Font |
| 38 | `/webui/assets/fonts/ComicNeue-Regular.ttf` | GET | ❌ Public | Font |
| 39 | `/webui/assets/fonts/ComicNeue-Bold.ttf` | GET | ❌ Public | Font |
| 40 | `/webui/assets/sfx_click.wav` | GET | ❌ Public | SFX |
| 41 | `/webui/assets/sfx_ok.wav` | GET | ❌ Public | SFX |
| 42 | `/webui/assets/sfx_error.wav` | GET | ❌ Public | SFX |

### 1.8 Endpoint Non-Found

| # | Endpoint | Méthode | Critique? | Description |
|---|----------|---------|-----------|-------------|
| 43 | `/*` | ALL | ❌ Non | 404 default handler |

---

## 2. AUDIT DE SÉCURITÉ ACTUEL

### 2.1 Findings Critiques

#### ⚠️ F001: Absence Totale d'Authentification API
**Sévérité**: 🔴 CRITIQUE  
**Impact**: Tous 34 endpoints sensibles sont accessibles sans authentification  
**Risque**:
- Étranger local sur réseau WiFi peut activer caméra et enregistrer audio
- Contrôle complet du scénario pédagogique (triche jeux)
- Modification arbitraire de configuration réseau (WiFi hijacking)
- Déni de service (désactiver caméra, couper WiFi)

**Code Vulnerable** (exemple):
```cpp
g_web_server.on("/api/camera/on", HTTP_POST, []() {
  const bool ok = g_camera.start();  // ❌ PAS DE VÉRIFICATION AUTH
  webSendResult("CAM_ON", ok);
});
```

#### ⚠️ F002: Pas de Contrôle d'Accès par Ressource
**Sévérité**: 🔴 CRITIQUE  
**Impact**: Chaque endpoint est un point d'entrée autonome  
**Risque**: Difficile d'implémenter contrôle granulaire (ex: lecture seule vs modification)

#### ⚠️ F003: Énumération d'Information Sensible
**Sévérité**: 🟠 ÉLEVÉ  
**Impact**: `/api/network/espnow/peer` expose les MAC addresses des appareils appairés  
**Risque**: Reconnaissance réseau pour attaques ciblées

#### ⚠️ F004: Contrôle Hardware/Média Non Limité
**Sévérité**: 🟠 ÉLEVÉ  
**Impact**: Pas de throttling, rate limiting, ou vérification intégrité  
**Risque**: Spam LED, enregistrement infini, vidéo continue

### 2.2 Vecteurs d'Attaque Réalistes

**Scénario 1: Parent Curieux sur WiFi Local**
```
1. Parent se connecte au WiFi ZACUS
2. Visite http://192.168.1.100/api/camera/snapshot.jpg
3. Obtient image enfant (caméra de chambre)
4. POST /api/media/record/start → enregistre conversations
```

**Scénario 2: Triche Jeu en Réseau Local**
```
1. Enfant ami sur WiFi local
2. Appelle /api/scenario/unlock → débloque tous niveaux
3. Appelle /api/scenario/next → skip étapes
4. Gagne sans effort pédagogique
```

**Scénario 3: Déni de Service (DoS)**
```
1. Attaquant USB local
2. Boucle POST /api/wifi/disconnect
3. Apareil ZACUS perd WiFi continuellement
```

### 2.3 Contrôle d'Accès Existant
❌ **AUCUN**
- Pas de whitelist IPs
- Pas de vérification source
- Pas de rate limiting
- Pas de CORS policy
- Pas de token/secret

---

## 3. ARCHITECTURE BEARER TOKEN PROPOSÉE

### 3.1 Design Principes

**Contraintes ESP32**:
- RAM limité (~500KB usable)
- Pas de HTTPS native (coûteux)
- Besoin d'initialisation simple (boot sans réseau)
- Doit être simple à debugger via serial

**Choix**: **Simple Token Bearer (non-JWT)** pour minimiser complexité
- JWT ajouterait ~500 bytes overhead
- Token UUID simple suffit pour ce cas

### 3.2 Architecture

```
┌─────────────────────────────────────────────┐
│         AsyncWebServer Port 80              │
├─────────────────────────────────────────────┤
│  Request → AuthService::validateBearer()    │
│     ↓                                       │
│  ┌──────────────────────────────┐           │
│  │ Extraire header Authorization │           │
│  │ Format: "Bearer <token>"  │           │
│  └────────┬─────────────────────┘           │
│           ↓                                 │
│  ┌──────────────────────────────┐           │
│  │ Comparer avec token stocké   │           │
│  │ en NVS (chiffré ou plain)    │           │
│  └────────┬─────────────────────┘           │
│           ↓                                 │
│  Valid?  ┌─────────┬──────────┐             │
│      ├──→│ TRUE    │ FALSE    │             │
│      │   └─────────┴──────────┘             │
│      │        ↓         ↓                   │
│      │     200 OK    401 Unauthorized       │
│      │     Process    + Logging             │
│      │      Request                         │
│      └──────────────────────────────        │
└─────────────────────────────────────────────┘
```

### 3.3 Caractéristiques du Token

| Propriété | Valeur |
|-----------|--------|
| Format | UUID4 (32 hex chars lowercase) |
| Exemple | `"550e8400e29b41d4a716446655440000"` |
| Longueur | 32 caractères |
| Stockage | NVS Flash (namespace `auth_service`) |
| Chiffrement | Optionnel (XOR simple pour MVP, AES si temps) |
| Expiration | Non (statique jusqu'à reboot/reset) |
| Rotation | Via serial command `AUTH_ROTATE_TOKEN` ou boot random |

### 3.4 Génération Initial du Token

**Option 1: À chaque boot** (plus simple, moins sécurisé)
```
1. Générer UUID4 aléatoire au démarrage
2. Sauvegarder en NVS (non persistant entre reboot)
3. Afficher sur serial `[AUTH] token=<token>`
4. WebUI affiche token (dès qu'accès serial obtenu)
```
✅ **Recommandé pour MVP** (rapide à implémenter)

**Option 2: Persiste en NVS** (plus sécurisé)
```
1. Au 1er boot: générer UUID4 → NVS
2. Aux boots suivants: charger depuis NVS
3. Serial command: `AUTH_ROTATE_TOKEN` → nouveau token
```
👉 **Phase 2 (après MVP)**

### 3.5 Exposition du Token

**Via Serial (après boot)**:
```
Serial Monitor voit au démarrage:
[AUTH] initialized token=550e8400e29b41d4a716446655440000
[AUTH] use: Authorization: Bearer 550e8400e29b41d4a716446655440000
```

**Via WebUI** (endpoint public, info-only):
```
GET /api/auth/status → 200 OK
{
  "initialized": true,
  "authenticated": false,  // true si client a bon token
  "requires_auth": true
}
```
⚠️ Ne pas exposer le token lui-même via API

**Via QR Code** (futur):
- Générer QR code contenant `http://<ip>/api/docs?token=...`
- Afficher sur écran LVGL
- Client scanne → reçoit token

### 3.6 Rate Limiting (Optionnel mais Recommandé)

```cpp
struct RateLimitBucket {
  uint32_t last_invalid_attempt_ms;
  uint8_t invalid_count;  // nombre tentatives échouées
};

// Si 5+ tentatives invalides en 60s → replay 429 Too Many Requests
```

---

## 4. IMPLÉMENTATION C++

### 4.1 Header: `auth_service.h`

```cpp
#pragma once

#include <cstdint>
#include <cstddef>

namespace AuthService {

// Token length (32 hex chars)
constexpr size_t kTokenLength = 32;
constexpr size_t kTokenBufferSize = kTokenLength + 1;

// Status codes
enum class AuthStatus {
  kOk,              // Token valid
  kMissingHeader,   // No Authorization header
  kInvalidFormat,   // Not "Bearer <token>"
  kInvalidToken,    // Token doesn't match
  kUninitialized,   // Service not initialized
  kInternalError,   // NVS read error, etc
};

// Initialize auth service - call from setup()
// Generates random token if not exists, or loads from NVS
bool init();

// Validate Authorization header from WebServer request
// Returns kOk only if token matches
AuthStatus validateBearerToken(const char* auth_header);

// Get current active token (for logging/display)
// buffer must be at least kTokenBufferSize bytes
bool getCurrentToken(char* out_token_buffer, size_t buffer_size);

// Generate new random token and save to NVS
// (for rotation via serial command)
bool rotateToken(char* out_new_token_buffer, size_t buffer_size);

// Reset service (clears NVS, generates new token)
bool reset();

// Get human-readable status message
const char* statusMessage(AuthStatus status);

// Check if authentication is enabled (can be disabled for testing)
bool isEnabled();

// Disable auth (testing only)
void setEnabled(bool enabled);

}  // namespace AuthService
```

### 4.2 Implementation: `auth_service.cpp`

```cpp
#include "auth_service.h"

#include <Arduino.h>
#include <nvs.h>
#include <cstring>
#include <cstdio>

namespace AuthService {

namespace {

constexpr const char* kNvsNamespace = "auth_service";
constexpr const char* kNvsKeyToken = "bearer_token";
constexpr const char* kLogTag = "[AUTH]";

char g_current_token[kTokenBufferSize] = {0};
bool g_initialized = false;
bool g_auth_enabled = true;

// Generate random token (UUID4-like 32 hex chars)
void generateRandomToken(char* out_buffer, size_t buffer_size) {
  if (!out_buffer || buffer_size < kTokenBufferSize) {
    return;
  }

  // XOR-based lightweight random for demo
  // In production: use esp_random() or hardware RNG
  uint32_t seed = micros() ^ esp_random();
  for (size_t i = 0; i < kTokenLength; i += 4) {
    uint32_t rand_val = (seed ^ esp_random());
    snprintf(&out_buffer[i], 5, "%08x", rand_val);
    seed = rand_val;
  }
  out_buffer[kTokenLength] = '\0';
}

// Load token from NVS
bool loadTokenFromNvs(char* out_buffer, size_t buffer_size) {
  if (!out_buffer || buffer_size < kTokenBufferSize) {
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    Serial.printf("%s NVS open failed (READONLY): %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  size_t required_size = 0;
  err = nvs_get_str(handle, kNvsKeyToken, nullptr, &required_size);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      Serial.printf("%s NVS key not found (first boot?)\n", kLogTag);
    } else {
      Serial.printf("%s NVS size query failed: %s\n", kLogTag, esp_err_to_name(err));
    }
    nvs_close(handle);
    return false;
  }

  if (required_size > buffer_size) {
    Serial.printf("%s token buffer too small: %zu > %zu\n", kLogTag, required_size, buffer_size);
    nvs_close(handle);
    return false;
  }

  err = nvs_get_str(handle, kNvsKeyToken, out_buffer, &buffer_size);
  nvs_close(handle);

  if (err != ESP_OK) {
    Serial.printf("%s NVS read failed: %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  return true;
}

// Save token to NVS
bool saveTokenToNvs(const char* token) {
  if (!token || token[0] == '\0') {
    return false;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    Serial.printf("%s NVS open failed (READWRITE): %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  err = nvs_set_str(handle, kNvsKeyToken, token);
  if (err != ESP_OK) {
    Serial.printf("%s NVS write failed: %s\n", kLogTag, esp_err_to_name(err));
    nvs_close(handle);
    return false;
  }

  err = nvs_commit(handle);
  nvs_close(handle);

  if (err != ESP_OK) {
    Serial.printf("%s NVS commit failed: %s\n", kLogTag, esp_err_to_name(err));
    return false;
  }

  return true;
}

}  // namespace

bool init() {
  if (g_initialized) {
    return true;
  }

  // Try to load from NVS
  if (!loadTokenFromNvs(g_current_token, sizeof(g_current_token))) {
    // Not found or error → generate new
    Serial.printf("%s generating new token\n", kLogTag);
    generateRandomToken(g_current_token, sizeof(g_current_token));

    // Try to save (optional, can fail on first boot if NVS not ready)
    if (!saveTokenToNvs(g_current_token)) {
      Serial.printf("%s warning: could not persist token to NVS\n", kLogTag);
    }
  }

  g_initialized = true;
  Serial.printf("%s initialized token=%s\n", kLogTag, g_current_token);
  return true;
}

AuthStatus validateBearerToken(const char* auth_header) {
  if (!g_initialized) {
    return AuthStatus::kUninitialized;
  }

  if (!g_auth_enabled) {
    return AuthStatus::kOk;  // Auth disabled (testing)
  }

  if (!auth_header || auth_header[0] == '\0') {
    return AuthStatus::kMissingHeader;
  }

  // Parse "Bearer <token>"
  if (std::strncmp(auth_header, "Bearer ", 7) != 0) {
    return AuthStatus::kInvalidFormat;
  }

  const char* provided_token = &auth_header[7];
  if (!provided_token || provided_token[0] == '\0') {
    return AuthStatus::kInvalidFormat;
  }

  // Trim trailing whitespace
  char token_copy[kTokenBufferSize];
  std::strncpy(token_copy, provided_token, sizeof(token_copy) - 1);
  token_copy[sizeof(token_copy) - 1] = '\0';

  size_t len = std::strlen(token_copy);
  while (len > 0 && (token_copy[len - 1] == ' ' || token_copy[len - 1] == '\n' || token_copy[len - 1] == '\r')) {
    token_copy[len - 1] = '\0';
    len--;
  }

  // Compare with stored token
  if (std::strcmp(token_copy, g_current_token) != 0) {
    return AuthStatus::kInvalidToken;
  }

  return AuthStatus::kOk;
}

bool getCurrentToken(char* out_token_buffer, size_t buffer_size) {
  if (!out_token_buffer || buffer_size < kTokenBufferSize || !g_initialized) {
    return false;
  }
  std::strncpy(out_token_buffer, g_current_token, buffer_size - 1);
  out_token_buffer[buffer_size - 1] = '\0';
  return true;
}

bool rotateToken(char* out_new_token_buffer, size_t buffer_size) {
  if (!out_new_token_buffer || buffer_size < kTokenBufferSize) {
    return false;
  }

  char new_token[kTokenBufferSize];
  generateRandomToken(new_token, sizeof(new_token));

  if (!saveTokenToNvs(new_token)) {
    Serial.printf("%s rotation failed: NVS write error\n", kLogTag);
    return false;
  }

  std::strncpy(g_current_token, new_token, sizeof(g_current_token) - 1);
  g_current_token[sizeof(g_current_token) - 1] = '\0';

  std::strncpy(out_new_token_buffer, new_token, buffer_size - 1);
  out_new_token_buffer[buffer_size - 1] = '\0';

  Serial.printf("%s rotated token=%s\n", kLogTag, new_token);
  return true;
}

bool reset() {
  char new_token[kTokenBufferSize];
  generateRandomToken(new_token, sizeof(new_token));

  if (!saveTokenToNvs(new_token)) {
    return false;
  }

  std::strncpy(g_current_token, new_token, sizeof(g_current_token) - 1);
  g_current_token[sizeof(g_current_token) - 1] = '\0';

  Serial.printf("%s reset - new token=%s\n", kLogTag, new_token);
  return true;
}

const char* statusMessage(AuthStatus status) {
  switch (status) {
    case AuthStatus::kOk:
      return "OK";
    case AuthStatus::kMissingHeader:
      return "Missing Authorization header";
    case AuthStatus::kInvalidFormat:
      return "Invalid Bearer format (expected: Authorization: Bearer <token>)";
    case AuthStatus::kInvalidToken:
      return "Invalid token";
    case AuthStatus::kUninitialized:
      return "Auth service not initialized";
    case AuthStatus::kInternalError:
      return "Internal error (NVS)";
    default:
      return "Unknown error";
  }
}

bool isEnabled() {
  return g_auth_enabled;
}

void setEnabled(bool enabled) {
  g_auth_enabled = enabled;
  Serial.printf("%s auth %s\n", kLogTag, enabled ? "enabled" : "disabled");
}

}  // namespace AuthService
```

### 4.3 Intégration dans `main.cpp`

**À ajouter au début du fichier**:
```cpp
#include "auth/auth_service.h"  // Ajouter include

// Dans setup():
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[MAIN] Freenove all-in-one boot");

  // AJOUTER CETTE LIGNE:
  AuthService::init();  // Initialize Bearer token auth
  
  // ... reste du setup ...
}
```

**Pattern d'intégration pour chaque endpoint sensible**:

```cpp
// AVANT (vulnérable):
g_web_server.on("/api/camera/on", HTTP_POST, []() {
  const bool ok = g_camera.start();
  webSendResult("CAM_ON", ok);
});

// APRÈS (sécurisé):
g_web_server.on("/api/camera/on", HTTP_POST, []() {
  // STEP 1: Valider Bearer token
  const char* auth_header = g_web_server.header("Authorization").c_str();
  const AuthService::AuthStatus auth_status = AuthService::validateBearerToken(auth_header);
  
  if (auth_status != AuthService::AuthStatus::kOk) {
    // STEP 2: Rejeter si invalide
    Serial.printf("[WEB] /api/camera/on DENIED auth_status=%s client=%s\n",
                  AuthService::statusMessage(auth_status),
                  g_web_server.client().remoteIP().toString().c_str());
    
    // STEP 3: Retourner 401 Unauthorized
    g_web_server.send(401, "application/json", 
                      "{\"ok\":false,\"error\":\"unauthorized\",\"reason\":\"" 
                      + String(AuthService::statusMessage(auth_status)) + "\"}");
    return;
  }
  
  // STEP 4: Procéder normalement si valide
  Serial.printf("[WEB] /api/camera/on ALLOWED\n");
  const bool ok = g_camera.start();
  webSendResult("CAM_ON", ok);
});
```

**Fonction Helper** (à ajouter dans main.cpp):
```cpp
// Valider Bearer token - retourner true si ok, false sinon
// Envoie automatiquement réponse 401 en cas d'erreur
inline bool validateAuthHeader() {
  const char* auth_header = g_web_server.header("Authorization").c_str();
  const AuthService::AuthStatus status = AuthService::validateBearerToken(auth_header);
  
  if (status != AuthService::AuthStatus::kOk) {
    Serial.printf("[WEB] AUTH_DENIED reason=%s client=%s\n",
                  AuthService::statusMessage(status),
                  g_web_server.client().remoteIP().toString().c_str());
    
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

**Usage Simplifié**:
```cpp
g_web_server.on("/api/camera/on", HTTP_POST, []() {
  if (!validateAuthHeader()) return;  // ← Une ligne!
  
  const bool ok = g_camera.start();
  webSendResult("CAM_ON", ok);
});
```

---

## 5. PLAN D'INTÉGRATION DÉTAILLÉ

### 5.1 Phase 1: Fondation (Jour 1-2, ~4-6 heures)

#### Étape 1.1: Créer les fichiers auth_service
- ✅ Créer `include/auth/auth_service.h`
- ✅ Créer `src/auth/auth_service.cpp`
- Compiler & valider pas d'erreurs

#### Étape 1.2: Initialiser dans setup()
- Ajouter `#include "auth/auth_service.h"`
- Appeler `AuthService::init()` dans `setup()`
- Flasher & valider log serial `[AUTH] initialized token=...`

#### Étape 1.3: Ajouter fonction helper validateAuthHeader()
- Créer fonction inline dans main.cpp
- Tester avec boucle de 5 endpoints (les plus critiques)

### 5.2 Phase 2: Intégration Endpoints Critiques (Jour 2-3)

**Groupe 1: Caméra** (3 endpoints, ~30 min)
- `/api/camera/on`
- `/api/camera/off`
- `/api/camera/snapshot.jpg`

**Groupe 2: Médias** (5 endpoints, ~40 min)
- `/api/media/play`
- `/api/media/stop`
- `/api/media/record/start`
- `/api/media/record/stop`
- `/api/media/record/status`

**Groupe 3: Réseau** (10 endpoints, ~1h)
- `/api/network/wifi`
- `/api/wifi/connect`
- `/api/network/wifi/connect`
- `/api/wifi/disconnect`
- `/api/network/wifi/disconnect`
- `/api/network/espnow/*` (tous)

**Groupe 4: Scénario** (4 endpoints, ~30 min)
- `/api/scenario/unlock`
- `/api/scenario/next`
- `/api/control`
- `/api/story/refresh-sd`

**Groupe 5: Hardware** (2 endpoints, ~20 min)
- `/api/hardware/led`
- `/api/hardware/led/auto`

### 5.3 Phase 3: Endpoints Non-Critiques & Logging

**Endpoints Informationnels** (à sécuriser mais lecture seule)
- `/api/status`
- `/api/stream`
- `/api/hardware`
- `/api/camera/status`
- `/api/media/files`
- `/api/network/espnow/peer` (GET)

**Endpoints à Garder Publics**
- `/` (WebUI)
- `/webui/assets/*` (CSS, fonts, images)
- `/api/auth/status` (info non-sensible)

### 5.4 Phase 4: Validation & Test (~2 heures)

#### Test 1: Sans Token
```bash
curl -X POST http://192.168.1.100:80/api/camera/on
# Expect: 401 Unauthorized
```

#### Test 2: Token Invalide
```bash
curl -H "Authorization: Bearer invalid123" \
     -X POST http://192.168.1.100:80/api/camera/on
# Expect: 401 Unauthorized
```

#### Test 3: Token Valide (obtenu du serial)
```bash
TOKEN=$(pio device monitor | grep "token=" | cut -d= -f2)
curl -H "Authorization: Bearer $TOKEN" \
     -X POST http://192.168.1.100:80/api/camera/on
# Expect: 200 OK + {"ok":true}
```

#### Test 4: Assets Publics
```bash
curl http://192.168.1.100:80/webui/assets/favicon.png
# Expect: 200 OK + PNG data (sans token requis)
```

---

## 6. CLASSIFICATION ENDPOINTS POUR INTÉGRATION

### Groupe A: CRITIQUE - Bearer Token Requis

```
🔴 SECURITY_LEVEL: CRITICAL
Tous les endpoints suivants DOIVENT avoir Bearer token:

CAMÉRA:
  POST /api/camera/on          → Peut activer surveillance
  POST /api/camera/off         → Peut couper surveillance
  GET  /api/camera/snapshot.jpg → Accès vidéo live
  GET  /api/camera/status      → Révèle si actif

AUDIO/MÉDIAS:
  POST /api/media/play         → Jouer son arbitraire
  POST /api/media/stop         → Arrêter
  POST /api/media/record/start → Enregistrer mic
  POST /api/media/record/stop  → Arrêter enregistrement
  GET  /api/media/record/status → Infos enregistrement

RÉSEAU:
  GET  /api/network/wifi       → Lister réseaux + connexion
  GET  /api/network/espnow     → Status ESP-NOW
  GET  /api/network/espnow/peer → Liste appareils
  POST /api/wifi/connect       → Hijacker WiFi
  POST /api/wifi/disconnect    → DoS (couper réseau)
  POST /api/network/wifi/*     → Manipulation WiFi
  POST /api/espnow/*           → Spam mesh network
  DELETE /api/network/espnow/peer → Exclure appareils

SCÉNARIO:
  POST /api/scenario/unlock    → Bypasser jeu
  POST /api/scenario/next      → Avancer étape
  POST /api/control            → Commande universelle
  POST /api/story/refresh-sd   → Recharger scénarios

HARDWARE:
  POST /api/hardware/led       → Contrôler LED
  POST /api/hardware/led/auto  → Modifier comportement
```

### Groupe B: RECOMMANDÉ - Bearer Token Recommandé

```
🟠 SECURITY_LEVEL: RECOMMENDED
Ces endpoints exposent infos système - Bearer token recommandé:

  GET  /api/status             → Status global
  GET  /api/stream             → SSE stream (connexion persistante)
  GET  /api/hardware           → Info matériel
  GET  /api/media/files        → Liste fichiers
```

### Groupe C: PUBLIC - Pas de Token Requis

```
🟢 SECURITY_LEVEL: PUBLIC
Assets statiques, pas de token requis:

  GET  /                       → WebUI HTML
  GET  /webui/assets/*         → Images, fonts, SFX
  GET  /api/auth/status        → Info-only (pas d'exposition token)
```

---

## 7. VALIDATION DES ENTRÉES

### 7.1 Parsing Bearer Token

**Format Attendu**:
```
Authorization: Bearer 550e8400e29b41d4a716446655440000
```

**Validations**:
1. Header exists
2. Starts with "Bearer " (case-sensitive)
3. Token length = 32 chars
4. Token chars are lowercase hex [0-9a-f]

**Exemple Strict** (optionnel):
```cpp
bool isValidTokenFormat(const char* token) {
  if (!token || std::strlen(token) != 32) return false;
  for (size_t i = 0; i < 32; i++) {
    char c = token[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}
```

### 7.2 Rate Limiting (Optionnel pour MVP, Recommandé Phase 2)

```cpp
enum class RateLimitStatus {
  kOk,              // Request allowed
  kTooManyAttempts, // Too many failed auth attempts
};

struct RateLimitBucket {
  uint32_t last_failed_ms = 0;
  uint8_t failed_count = 0;
  static const uint8_t kFailLimit = 5;
  static const uint32_t kWindowMs = 60000;  // 60 seconds
  
  RateLimitStatus checkAndUpdate() {
    uint32_t now = millis();
    
    // Reset bucket if window expired
    if (now - last_failed_ms > kWindowMs) {
      failed_count = 0;
      return RateLimitStatus::kOk;
    }
    
    // Check limit
    if (failed_count >= kFailLimit) {
      return RateLimitStatus::kTooManyAttempts;
    }
    
    return RateLimitStatus::kOk;
  }
  
  void recordFailure() {
    last_failed_ms = millis();
    failed_count++;
  }
};

// Map de buckets par IP client
// std::map<IPAddress, RateLimitBucket> g_rate_limit_buckets;
```

---

## 8. TEST CASES

### TC-001: Request sans Token
```
METHOD: POST
ENDPOINT: /api/camera/on
HEADERS: (none)
BODY: (none)

EXPECTED:
  Status: 401 Unauthorized
  Response: {"ok":false,"error":"unauthorized","reason":"Missing Authorization header"}
  Serial Log: [WEB] /api/camera/on DENIED auth_status=Missing Authorization header client=192.168.1.X
```

### TC-002: Token Invalide
```
METHOD: POST
ENDPOINT: /api/media/play
HEADERS: Authorization: Bearer invalidtoken123
BODY: {"path":"/music/test.mp3"}

EXPECTED:
  Status: 401 Unauthorized
  Response: {"ok":false,"error":"unauthorized","reason":"Invalid token"}
  Serial Log: [WEB] /api/media/play DENIED auth_status=Invalid token client=192.168.1.X
```

### TC-003: Format Bearer Invalide
```
METHOD: POST
ENDPOINT: /api/scenario/unlock
HEADERS: Authorization: Basic dXNlcjpwYXNz
BODY: (none)

EXPECTED:
  Status: 401 Unauthorized
  Response: {"ok":false,"error":"unauthorized","reason":"Invalid Bearer format..."}
```

### TC-004: Token Valide
```
METHOD: POST
ENDPOINT: /api/camera/on
HEADERS: Authorization: Bearer 550e8400e29b41d4a716446655440000
BODY: (none)

EXPECTED:
  Status: 200 OK
  Response: {"ok":true} ou {"error":"camera_already_on"}
  Serial Log: [WEB] /api/camera/on ALLOWED
```

### TC-005: Endpoint Public (Asset)
```
METHOD: GET
ENDPOINT: /webui/assets/favicon.png
HEADERS: (none)

EXPECTED:
  Status: 200 OK
  Content-Type: image/png
  Data: PNG binary data
  NOTE: Pas de vérification auth requise
```

### TC-006: Endpoint Public (Info)
```
METHOD: GET
ENDPOINT: /api/auth/status
HEADERS: (none)

EXPECTED:
  Status: 200 OK
  Response: {"initialized":true,"authenticated":false,"requires_auth":true}
```

### TC-007: Rate Limiting (si implémenté)
```
METHOD: POST
ENDPOINT: /api/camera/on
HEADERS: Authorization: Bearer invalid
BODY: (none)

ACTION: Envoyer 6 requetes en rapide succession

EXPECTED:
  Requetes 1-5: 401 Unauthorized
  Requete 6+: 429 Too Many Requests
  Serial Log: [WEB] rate_limit triggered client=192.168.1.X
```

### TC-008: Token Rotation
```
METHOD: Serial Command
COMMAND: AUTH_ROTATE_TOKEN

EXPECTED:
  Serial Output: [AUTH] rotated token=<NEW_TOKEN>
  Ancien token invalide après
  Nouveau token valide immédiatement
```

---

## 9. SERIAL COMMANDS POUR DEBUG/ADMIN

Ajouter à `handleSerialCommand()` dans main.cpp:

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
  Serial.println("AUTH_DISABLED (testing only)");
  return;
}

if (std::strcmp(command, "AUTH_ENABLE") == 0) {
  AuthService::setEnabled(true);
  Serial.println("AUTH_ENABLED");
  return;
}
```

---

## 10. CHECKLIST D'IMPLÉMENTATION

### Phase 1: Fondation
- [ ] Créer `include/auth/auth_service.h`
- [ ] Créer `src/auth/auth_service.cpp`
- [ ] Modifier `platformio.ini` (optionnel: inclure `-std=c++17` si non présent)
- [ ] Ajouter `#include "auth/auth_service.h"` dans main.cpp
- [ ] Appeler `AuthService::init()` dans `setup()`
- [ ] Compiler sans erreurs
- [ ] Flasher sur ESP32
- [ ] Valider log serial `[AUTH] initialized token=...`

### Phase 2: Endpoints Caméra
- [ ] Wrapper `validateAuthHeader()` dans main.cpp
- [ ] Ajouter check dans `/api/camera/on`
- [ ] Ajouter check dans `/api/camera/off`
- [ ] Ajouter check dans `/api/camera/snapshot.jpg`
- [ ] Ajouter check dans `/api/camera/status`
- [ ] Test: curl sans token → 401
- [ ] Test: curl avec token invalide → 401
- [ ] Test: curl avec token valide → 200

### Phase 3: Endpoints Médias
- [ ] `/api/media/play`
- [ ] `/api/media/stop`
- [ ] `/api/media/record/start`
- [ ] `/api/media/record/stop`
- [ ] `/api/media/record/status`
- [ ] `/api/media/files`
- [ ] Tests complets

### Phase 4: Endpoints Réseau
- [ ] `/api/network/wifi` (GET)
- [ ] `/api/network/espnow` (GET)
- [ ] `/api/network/espnow/peer` (GET, POST, DELETE)
- [ ] `/api/wifi/connect` (POST)
- [ ] `/api/wifi/disconnect` (POST)
- [ ] `/api/network/wifi/connect` (POST)
- [ ] `/api/network/wifi/disconnect` (POST)
- [ ] `/api/espnow/send` (POST)
- [ ] `/api/network/espnow/send` (POST)
- [ ] `/api/network/espnow/on` (POST)
- [ ] `/api/network/espnow/off` (POST)
- [ ] Tests complets

### Phase 5: Endpoints Scénario & Hardware
- [ ] `/api/scenario/unlock` (POST)
- [ ] `/api/scenario/next` (POST)
- [ ] `/api/control` (POST)
- [ ] `/api/story/refresh-sd` (POST)
- [ ] `/api/hardware/led` (POST)
- [ ] `/api/hardware/led/auto` (POST)
- [ ] Tests complets

### Phase 6: Endpoints Informationnels (Optionnel)
- [ ] `/api/status` (GET) - recommandé mais non critique
- [ ] `/api/stream` (GET) - recommandé mais non critique
- [ ] `/api/hardware` (GET) - recommandé mais non critique

### Phase 7: Logging & Monitoring
- [ ] Ajouter logging détaillé pour 401/429
- [ ] Formatter logs avec IP client
- [ ] Optionnel: mémoriser tentatives échouées (rate limiting)

### Phase 8: Documentation & Serial Commands
- [ ] Implémenter `AUTH_STATUS`
- [ ] Implémenter `AUTH_ROTATE_TOKEN`
- [ ] Implémenter `AUTH_RESET`
- [ ] Implémenter `AUTH_ENABLE` / `AUTH_DISABLE`
- [ ] Documenter dans HELP

### Phase 9: Tests & Validation
- [ ] Test TC-001 à TC-008
- [ ] Test rechargement page WebUI (si WebUI modifiée)
- [ ] Test séquence hétérogène (sans/avec/mauvais token)
- [ ] Test rate limiting (si implémenté)

### Phase 10: Déploiement
- [ ] Build final `pio run -e freenove_esp32s3`
- [ ] Flash firmware
- [ ] Capture premier boot (log [AUTH] token=...)
- [ ] Document token initial pour parent/utilisateur
- [ ] Tester WebUI accès (si implémenté)

---

## 11. TIMINGS ESTIMÉS

| Phase | Description | Heures | Cumulé |
|-------|-------------|--------|--------|
| 1 | Fondation (auth_service.h/cpp) | 0.5h | 0.5h |
| 2 | Intégration caméra (3 endpoints) | 0.5h | 1h |
| 3 | Intégration médias (5 endpoints) | 0.75h | 1.75h |
| 4 | Intégration réseau (10 endpoints) | 1.25h | 3h |
| 5 | Intégration scénario + hardware (6 endpoints) | 0.75h | 3.75h |
| 6 | Logging, serial commands | 0.5h | 4.25h |
| 7 | Tests TC-001 à TC-008 | 0.75h | 5h |
| 8 | Debugging, edge cases | 0.75h | 5.75h |
| 9 | Documentation finale | 0.25h | 6h |

**Total: ~6 heures pour MVP complet** (Bearer token sur tous endpoints critiques)

---

## 12. SÉCURITÉ ADDITIONNELLE (Phase 2+)

### 12.1 HTTPS/TLS
- Actuellement: HTTP plain text  
- Recommandation: Ajouter support HTTPS via certificates auto-signés
- Impact: ~50KB RAM additionnel

### 12.2 Chiffrement NVS
- Actuellement: Token sauvegardé en plain text en NVS
- Recommandation: XOR simple ou AES (si place)
- Impact: ~200 bytes overhead

### 12.3 JWT avec Expiration
- Actuellement: Token statique infini
- Recommandation: JWT avec TTL (ex: 1 heure)
- Impact: ~300 bytes overhead

### 12.4 Refresh Token Flow
- Actuellement: Un seul token
- Recommandation: Access token court + Refresh token long
- Impact: Complexité accrue

### 12.5 CORS Policy
- Actuellement: Aucun CORS header
- Recommandation: Ajouter CORS avec validation domaine
- Impact: ~50 bytes overhead

---

## 13. INTÉGRATION WEBUI (FUTUR)

Si WebUI client front-end implémenté:

```javascript
// Dans client JS
const token = localStorage.getItem('app_bearer_token');

fetch('http://192.168.1.100:80/api/camera/on', {
  method: 'POST',
  headers: {
    'Authorization': `Bearer ${token}`,
    'Content-Type': 'application/json'
  }
}).then(r => {
  if (r.status === 401) {
    // Prompt user for token
    const token = prompt('Enter API token:');
    localStorage.setItem('app_bearer_token', token);
  }
  return r.json();
});
```

---

## 14. CONCLUSION & RECOMMANDATIONS

### Findings Clés
1. **Absence totale d'authentification** API - CRITIQUE
2. **34 endpoints sensibles** accessibles sans contrôle
3. **Vecteur d'attaque réaliste**: Étranger sur WiFi local peut surveiller/enregistrer
4. **Implémentation Bearer token minimale** est suffisante pour MVP

### Recommandations Immédiates
1. ✅ **FAIRE**: Implémenter Bearer token (suivre plan 6 heures ci-dessus)
2. ✅ **FAIRE**: Ajouter logging des 401 Unauthorized
3. ✅ **CONSIDÉRER**: Rate limiting simple (429 après 5 tentatives/min)
4. ⚠️ **FUTURE**: Migrer vers HTTPS (TLS)
5. ⚠️ **FUTURE**: Ajouter endpoint `/api/auth/token` pour obtenir token (avec verification hors-bande)

### Risque Résiduel
Même avec Bearer token, attaquant ayant accès :
- **Serial USB** peut lire token au boot
- **Réseau local** peut intercepter token (HTTPS recommandé)
- **Firmware** peut être cracked et token extrait

**Mitigation**: Combiner Bearer token + HTTPS + Serial lock-down (future)

---

## Annexe A: Codes d'Erreur JSON

```json
// 401 Unauthorized
{
  "ok": false,
  "error": "unauthorized",
  "reason": "Invalid token"
}

// 429 Too Many Requests (optionnel)
{
  "ok": false,
  "error": "too_many_requests",
  "reason": "Rate limit exceeded, retry after 60s"
}

// 200 OK (exemple)
{
  "ok": true,
  "data": { /* endpoint-specific */ }
}
```

---

## Annexe B: Headers de Réponse Recommandés

```
HTTP/1.1 401 Unauthorized
Content-Type: application/json
Content-Length: XX
X-Auth-Error: invalid_token
X-RateLimit-Remaining: 4
X-RateLimit-Reset: 1646150460

{ "ok": false, ... }
```

---

**Document**: Analyse Complète Bearer Token ESPN32_ZACUS  
**Version**: 1.0 Draft  
**Statut**: ✅ Prêt pour Implémentation  
**Prochaine étape**: Créer auth_service.h et lancer Phase 1  
