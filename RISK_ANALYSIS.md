# Analyse de Risques par Composant - ESP32_ZACUS

## Vue d'Ensemble de l'Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      ESP32-S3-WROOM                          │
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │    WiFi      │  │  BLE/ESP-NOW │  │   Scenario   │      │
│  │  (STA/AP)    │  │    Radio     │  │   Engine     │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│         │                 │                  │              │
│  ┌──────────────────────────────────────────────────┐      │
│  │         Runtime State Machine                     │      │
│  │    (STA: Running Script, Camera, Audio)           │      │
│  └──────────────────────────────────────────────────┘      │
│         │                 │                  │              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  WebServer   │  │  LittleFS    │  │    NVS       │      │
│  │  (Port 80)   │  │  (app code)  │  │  (config)    │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. Analyse de Risques: WebServer (Port 80)

### Dépendances
- `main.cpp` (handlers)
- `network_manager.cpp` (WiFi state)
- `storage_manager.cpp` (file access)
- `scenario_manager.cpp` (state)

### Flux de Données
```
HTTP Request (port 80)
    ↓
[NO AUTHENTICATION] ← ⚠️ CRITICAL
    ↓
Parse JSON body ← ⚠️ NO SCHEMA VALIDATION
    ↓
Execute Command (scenario unlock, WiFi connect, camera access)
    ↓
HTTP Response (plain text/JSON)
```

### Risques Identifiés

| Risque | Vecteur | Impact | Mitigation |
|--------|---------|--------|-----------|
| 🔴 Contournement Scénario | POST /api/scenario/unlock | Jeu joué | Auth HMAC + Rate limit |
| 🔴 Injection Credentials | POST /api/wifi/connect | Compromis réseau | Auth + Validation |
| 🔴 Commandes ESP-NOW | POST /api/espnow/send | Mesh compromise | Auth + Signature |
| 🔴 Accès Caméra | GET /api/camera/snapshot.jpg | Privacy leak | Auth + HTTPS |
| 🔴 Enregistrement Audio | POST /api/media/record/start | Privacy leak | Auth |
| 🟡 DoS (No Rate Limit) | GET /api/status × 1000/sec | Device unavailable | Rate limiting |
| 🟡 Cleartext Transport | HTTP (port 80) | MITM credentials | HTTPS |
| 🟢 Information Disclosure | GET /api/status | Network topology | Minimal risk |

### Flux de Sécurité Actuel
```
Client → HTTP (plaintext) → WebServer.handleClient()
                                ↓
                          [0 authentication checks]
                                ↓
                          dispatch command
```

### Flux de Sécurité Sécurisé (Recommandé)
```
Client → HTTPS (encrypted)                    WebServer.handleSecureClient()
            ↓ (TLS 1.2)                              ↓
         1. Get headers                   1. Extract X-Auth header
            (X-Auth, X-Timestamp)            (signature + timestamp)
            ↓                                 ↓
         2. Sign payload    ←←←← ⚔️ 2. Verify HMAC-SHA256
            HMAC(body:ts)                     compare(expected, actual)
            ↓                                 ↓
         3. Send request  ←←←← ✅ 3. Check timestamp not expired
            ↓                                 ↓
         4. Validate response        4. Rate limit check (10 req/min)
            ↓                                 ↓
         5. Process reply          5. Execute command
                                         ↓
                                      6. Send secure response
```

---

## 2. Analyse de Risques: Gestion des Credentials WiFi

### Chaîne d'Accès Actuelle
```
┌── NVS (Non-Volatile Storage) [Chiffré par H/W]
├── LittleFS CONFIG (embedded JSON) [PLAINTEXT] ← ⚠️ CRITICAL
├── RAM (runtime) [Plaintext]
└── Network Transfer [HTTP cleartext] ← ⚠️ CRITICAL
```

### Scénario d'Attaque
```
Attaquant                           Device
  │                                   │
  ├─ 1. Nmap port 80 ───────────────>│
  │                                   │
  ├─ 2. GET /api/network/wifi ──────>│
  │<─ 3. WiFi config (PLAINTEXT) ────┤
  │     {"ssid":"Les cils",           │
  │      "password":"mascarade"}      │
  │                                   │
  ├─ 4. Injecter MITM ──────────────>│
  │     "connect_sta": "hacker-net"   │
  │                                   │
  ├─ 5. Device se reconn. hidden AP ─┤─ ⚠️ Compromised
  │                                   │
  └─ 6. Accès réseau complet ────────>│
         (credentials + device control)
```

### Risques par Étape

| Étape | Composant | Risque | Sévérité | Cause | Mitigation |
|-------|-----------|--------|----------|-------|-----------|
| 1 | Découverte | Port 80 ouvert | HAUTE | Pas de firewall | Filtrer IPs |
| 2 | API WiFi | Credentials leak | CRITIQUE | Pas d'auth | HMAC |
| 3 | Transport | Plaintext | CRITIQUE | HTTP | HTTPS |
| 4 | Injection | Redirection Wi-Fi| HAUTE | Pas de validation | Whitelist SSID |
| 5 | Reconnexion | MITM | CRITIQUE | Credentials compromised | Rotation |

### Zones de Sensibilité

```
🔴 CRITICAL ZONE (immédiate action requise)
├─ Hardcoded WiFi password in storage_manager.cpp
├─ HTTP endpoints exposing WiFi config
├─ No credential encryption in NVS

🟡 HIGH ZONE (before production)
├─ No HTTPS for credential transport
├─ No whitelist on WiFi SSID targets
├─ Password visible in logs/serial

🟢 MEDIUM ZONE (before next release)
├─ No credential rotation mechanism
├─ No audit log of WiFi changes
├─ No automatic re-lock after credential updates
```

---

## 3. Analyse de Risques: Analyse JSON

### Vulnérabilités Identifiées

#### A. Pas de Validation de Taille
```cpp
// ❌ Avant
StaticJsonDocument<512> document;
deserializeJson(document, payload);  // Peut parser > 512 bytes si docsize > 512

// ✅ Après
if (payload.length() > 1024) return false;  // Validation taille
StaticJsonDocument<512> document;
```

#### B. Pas de Limite de Profondeur
```cpp
// ❌ Payload d'attaque
{
  "nested": {
    "level": {
      "deep": {
        "structure": {
          "causing": {
            "stack": {
              "overflow": { ... 100 levels ... }
            }
          }
        }
      }
    }
  }
}

// Résultat: Stack exhaustion
```

#### C. Pas de Validation de Type
```cpp
// ❌ Code actuel
root["brightness"] | 128  // Pas de vérification si brightness est number

// ✅ Réaction sécurisée
if (!root["brightness"].is<int>()) return false;
if (root["brightness"] < 0 || root["brightness"] > 255) return false;
```

### Matrice d'Impact JSON

| Payload | Size | Depth | Type | Résultat | Sévérité |
|---------|------|-------|------|----------|----------|
| `{}` | 2B | 0 | OK | Parse OK | NONE |
| `{"cmd":"TEST"}` | 14B | 1 | OK | Parse OK | NONE |
| `{"a":{"b":{...` × 100 | 500B | 50 | OK | Stack overflow? | HIGH |
| `{"msg":"huge string..."}` | 50KB | 1 | OK | OOM | HIGH |
| `{"brightness":"text"}` | 25B | 1 | WRONG | Unpredictable | MEDIUM |

---

## 4. Analyse de Risques: Accès Fichiers

### Arborescence Sensible
```
/story/
├── apps/
│   ├── APP_WIFI.json ← ⚠️ Contains credentials
│   ├── APP_ESPNOW.json
│   └── [40+ app configs]
├── content/
│   ├── [game assets]
│   └── [video cache]
└── [other files]

/sdcard/ (Si présent)
├── music/
├── recorder/
└── [user files]
```

### Scénario d'Attaque Path Traversal
```
Client                              Device
  │                                   │
  ├─ GET /api/media/files?kind=../../story/apps/APP_WIFI.json
  │                                   │
  │<────── [Without validation] ──────┤
  │  { "files": ["APP_WIFI.json"]}    │
  │                                   │
  ├─ GET /data/apps/APP_WIFI.json ─→ │
  │<─ { "local_password":"mascarade" }
```

### Validations Manquantes
```cpp
// ❌ Code actuel: normalizeAbsolutePath()
String path = "../../story/apps/APP_WIFI.json";
String normalized = path;
// Résultat: ../../story/apps/APP_WIFI.json (pas d'amélioration)

// ✅ Code sécurisé
if (path.indexOf("..") != -1) reject();
if (path.indexOf("//") != -1) reject();
// Vérifier whitelist de préfixes autorisés
if (!path.startsWith("/story/apps/") && 
    !path.startsWith("/sdcard/music/")) {
  reject();
}
```

---

## 5. Analyse de Risques: Interfaces Externes

### Radio Interfaces
```
                    Device (ESP32-S3)
                           │
               ┌───────────┼───────────┐
               │           │           │
          ┌────▼───┐  ┌───▼────┐  ┌──▼─────┐
          │  WiFi  │  │ESP-NOW │  │  BLE   │
          │ 2.4GHz │  │2.4GHz  │  │2.4GHz  │
          └─┬──────┘  └──┬─────┘  └────────┘
            │            │
         ┌──▼──┐      ┌──▼──┐
         │  AP │      │Mesh │
         │Mode │      │Mode │
         └─────┘      └─────┘

Risques:
- WiFi: WPA2 keys extractible (covered by FCC)
- ESP-NOW: No authentication ← ⚠️ CRITICAL
- BLE: Sniffer accès (if enabled)
```

### Sécurité ESP-NOW (Critique)

#### Avant (Actuel)
```cpp
// ❌ executeEspNowCommandPayload() - main.cpp:654
bool executeEspNowCommandPayload(const char* payload_text, ...) {
  // Aucune validation du MAC source
  // Aucune signature du message
  // N'importe quel peer peut envoyer n'importe quelle commande
  
  StaticJsonDocument<512> root;
  deserializeJson(root, payload_text);  // Could have bad JSON
  
  const char* cmd = root["cmd"] | "";
  if (strcmp(cmd, "STATUS") == 0) {
    // Processus réponse
  }
  if (strcmp(cmd, "UNLOCK") == 0) {
    // ⚠️ Débloquer le scénario - AUCUN CONTRÔLE D'ACCÈS
    dispatchScenarioEventByName("UNLOCK", millis());
  }
  // ... 10 autres commandes sans authen
}
```

#### Scénario d'Attaque ESP-NOW
```
Attaquant (Device A)                  Victime (Device B)
    │                                      │
    ├─ Envoyer frame ESP-NOW ────────────→ │
    │  {                                    │
    │    "msg_id": 123,                     │
    │    "seq": 1,                          │
    │    "type": "json",                    │
    │    "payload": "{\"cmd\":\"UNLOCK\"}" │
    │  }                                    │
    │                                    [executeEspNowCommandPayload]
    │<─────── ACK frame ──────────────────┤
    │                                  [Scenario Unlocked]
    │
    │ ✅ Attaquant contrôle maintenant appareil victime
    │    via le maillage ESP-NOW
```

#### Mitigation Recommandée
```cpp
// ✅ Après: Validation du peer + Signature

// 1. Whitelist de MACs de confiance
bool isEspNowPeerTrusted(const uint8_t* src_mac) {
  static const uint8_t TRUSTED_PEERS[][6] = {
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF1},  // Parent device
    {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF2},  // Teacher device
  };
  
  for (const auto& trusted : TRUSTED_PEERS) {
    if (memcmp(src_mac, trusted, 6) == 0) return true;
  }
  return false;
}

// 2. Signer tous les messages ESP-NOW
void sendEspNowSignedMessage(const uint8_t* dest_mac, const char* json_payload) {
  // Construire message signé
  String message = json_payload;
  message += ":";
  message += (uint32_t)time(nullptr);  // Ajouter timestamp
  
  // Signer avec clé partagée
  unsigned char signature[32];
  mbedtls_md_hmac(..., message, signature);
  
  // Ajouter signature à payload
  String signed_payload = message + ":" + hexEncode(signature);
  
  // Envoyer via ESP-NOW
  esp_now_send(dest_mac, (uint8_t*)signed_payload.c_str(), 
               signed_payload.length());
}

// 3. Vérifier signature à la réception
bool executeEspNowCommandPayload(const uint8_t* src_mac, 
                                 const char* payload_text) {
  if (!isEspNowPeerTrusted(src_mac)) {
    Serial.println("ESP-NOW: Untrusted peer");
    return false;
  }
  
  // Extraire signature
  String payload = payload_text;
  int last_colon = payload.lastIndexOf(':');
  String signature_hex = payload.substring(last_colon + 1);
  String message = payload.substring(0, last_colon);
  
  // Vérifier HMAC
  if (!verifyHmacSignature(message, signature_hex)) {
    Serial.println("ESP-NOW: Invalid signature");
    return false;
  }
  
  // ✅ Maintenant safe de traiter le message
  StaticJsonDocument<512> root;
  deserializeJson(root, message);  // JSON parsing...
}
```

---

## 6. Analyse de Risques: Compilation & Linkage

### Flags Manquants
```ini
# ❌ Actuel (platformio.ini:97)
build_flags = -O2 -ffast-math

# ✅ Recommandé
build_flags =
  -O2                           # Optimisation
  -ffast-math                   # Math rapide
  -fstack-protector-strong      # Canaries détection overflow
  -Wformat -Wformat-security    # Format string checks
  -D_FORTIFY_SOURCE=2           # Runtime checks
  -Wall -Wextra -Wpedantic      # All warnings
  -Werror=format-string         # Erreur format strings
```

### Implication
```
Protections Actuelles:
├─ Stack canary:     ❌ DISABLED
├─ ASLR:            ❌ ESP32 limitation
├─ DEP/NX:          ✅ Hardware support
├─ PIE:             ❌ NOT ENABLED
├─ Format strings:  ❌ NO WARNING


Risques:
├─ Buffer overflow non détecté
├─ Format string exploitation
├─ Information leak via addresses
└─ Potential ROP chain (though limited by ESP32 IRAM)
```

---

## 7. Matrice de Risques Globale

```
                   Probabilité d'exploitation
                   Basse    Moyenne    Haute
┌─────────────────────────────────────────────┐
  Très Grave  │ ██  ── UncriticalPassively
             │ ██████ HIGH-CRITICAL
             │ ████████████ CRITICAL-WEB-API
             │
  Grave      │ ██ LOW-001
             │ ██████ MED-003
             │ ████████✓ HIGH-PATHN TRAVERSAL
             │
  Modéré     │ ██ MED-004
             │ ██████ MED-002-NORATELI
             │
  Faible     │ ██ MED-001
             └─────────────────────────────────┘
```

### Légende
- 🔴 CRITICAL: Déploiement impossible
- 🔴 HIGH: Bloquer avant production
- 🟡 MEDIUM: Corriger avant lanc produit
- 🟢 LOW: Amélioration continue

---

## 8. Dépendances Entre Vulnérabilités

```
CRIT-001 (Hardcoded Creds)
    │
    ├──→ CRIT-002 (No Auth) ← HIGH-002 (JSON)
    │         │                   │
    │         └──┬────────────────┘
    │            │
    └──→ MED-002 (No HTTPS)
         │
         └──→ MED-004 (MITM)

HIGH-001 (sscanf) ← Used in handlers
         │
         └──→ HIGH-002 (No type validation)

HIGH-003 (Path traversal)
         │
         └──→ Coupled with CRIT-001 (Creds in files)

MED-001 (No rate limit)
         │
         └──→ Could amplify any of above
```

**Implication:** Fixer d'abord CRIT-001, CRIT-002, puis HIGH-* ensemble.

---

## 9. Tableau de Commande de Remédiation

| Phase | Tâche | Fichier | Ligne | Effort | Priorité |
|-------|-------|---------|-------|--------|----------|
| 1 | NVS migration | storage_manager.cpp | 52 | 3h | 1 |
| 1 | HMAC middleware | main.cpp | 46 | 6h | 1 |
| 1 | JSON validation | main.cpp | 654 | 2h | 2 |
| 2 | Path validator | storage_manager.cpp | 308 | 1h | 3 |
| 2 | sscanf → strtol | main.cpp | 1807 | 2h | 3 |
| 3 | HTTPS setup | main.cpp | 46 | 8h | 4 |
| 3 | Rate limiter | main.cpp | 3366 | 3h | 5 |
| 4 | Compiler flags | platformio.ini | 97 | 0.5h | 6 |

**Total estimé:** 25 heures = 3-4 jours (1 développeur)

---

## 10. Compliance & Standards

### Normes Applicables
- ✅ OWASP Top 10 2021
- ✅ CWE Top 25
- ⚠️ GDPR (collected video/audio data)
- ⚠️ FCC (RF emissions - already certified)
- ⚠️ COPPA (Children's Online Privacy - kids app)

### Points de Conformité Clés

**GDPR:**
```
Données collectées:
├─ Photos (camera API)
├─ Audio (recording API)
├─ Location (si WiFi positioning)
└─ Device ID

Conformité:
├─ Parental consent ← À implémenter
├─ Data retention policy ← À implémenter
├─ Encryption in transit ← HTTPS requis
└─ Encryption at rest ← À implémenter
```

**FCC (RF):**
```
✅ ESP32-S3 est FCC approuvé
✅ WiFi 802.11b/g/n certifié
✅ 2.4 GHz bande autorisée

À vérifier:
├─ TX power limits (< 30 dBm)
├─ Channel switching (US: 1-11)
├─ Antenne certification
└─ EMC interference test
```

**COPPA (Kids' Data Protection - Especially Important):**
```
Identification du device comme "kids' app":
app configuration → 8 apps kids_* present

Obligations:
├─ NO tracking / behavioral ads
├─ NO WiFi data collection
├─ Parental supervision logging
├─ Strong security (your audit!)
├─ NO sale of children's information
└─ Verifiable parental consent

Red Flags:
├─ Audio recording without parental notification ← DO THIS
├─ Camera access without UI indicators ← ADD THIS
├─ Unencrypted transmission ← FIX WITH HTTPS
└─ Insufficient authentication ← YOUR #1 PRIORITY
```

---

**Conclusion:** L'analyse de vulnérabilité montre des défaillances de sécurité systématiques plutôt que des bugs isolés. Une remédiation coordonnée et une architecture de sécurité dès le départ sont essentielles.
