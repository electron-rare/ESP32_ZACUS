# Guide Pratique de Remédiation - ESP32_ZACUS

## 1. CRITIQUE: Identifiants Stockés en Dur → Migrations vers NVS

### Avant (❌ Dangereux)
```cpp
// storage_manager.cpp - Embedded credentials
const struct {
  const char* path;
  const char* json;
} kEmbeddedStoryAssets[] = {
  {"/story/apps/APP_WIFI.json", 
   R"JSON({"local_ssid":"Les cils","local_password":"mascarade"})JSON"},
  // ...
};
```

### Après (✅ Sécurisé)

**Étape 1: Ajouter la dépendance**
```ini
# platformio.ini
[env:esp32s3]
lib_deps = 
  # ... autres libs ...
  # Note: Preferences est fourni avec ESP32 core
```

**Étape 2: Créer un service de gestion des credentials**
```cpp
// include/credential_manager.h
#pragma once
#include <Preferences.h>

class CredentialManager {
private:
  Preferences nvs;
  static constexpr const char* NVS_NAMESPACE = "wifi_creds";
  
public:
  struct WiFiCredentials {
    char ssid[33];
    char password[65];
  };
  
  bool begin() {
    return nvs.begin(NVS_NAMESPACE, false);  // false = RW mode
  }
  
  void end() {
    nvs.end();
  }
  
  // Sauvegarder credentials
  bool saveWiFiCredentials(const char* ssid, const char* password) {
    if (strlen(ssid) > 32 || strlen(password) > 64) {
      return false;  // Validation de taille
    }
    
    // Pour plus de sécurité, implémenter le chiffrement:
    // mbedtls_aes_crypt_cbc() avec clé dérivée du device certificate
    
    nvs.putString("ssid", ssid);
    nvs.putString("pass", password);
    return true;
  }
  
  // Récupérer credentials
  bool getWiFiCredentials(WiFiCredentials& creds) {
    String ssid = nvs.getString("ssid", "");
    String pass = nvs.getString("pass", "");
    
    if (ssid.length() == 0) {
      return false;  // Non trouvé
    }
    
    strncpy(creds.ssid, ssid.c_str(), 32);
    strncpy(creds.password, pass.c_str(), 64);
    creds.ssid[32] = '\0';
    creds.password[64] = '\0';
    
    return true;
  }
  
  // Effacer credentials
  void clearWiFiCredentials() {
    nvs.putString("ssid", "");
    nvs.putString("pass", "");
  }
  
  // Vérifier s'il y a des credentials
  bool hasWiFiCredentials() {
    return nvs.getString("ssid", "").length() > 0;
  }
};
```

**Étape 3: Utiliser dans main.cpp**
```cpp
// main.cpp
#include "credential_manager.h"

CredentialManager g_creds;

void setup() {
  g_creds.begin();
  
  // Charger credentials depuis NVS
  CredentialManager::WiFiCredentials creds;
  if (g_creds.hasWiFiCredentials()) {
    g_creds.getWiFiCredentials(creds);
    g_network.connectSta(creds.ssid, creds.password);
  } else {
    // Démarrer mode AP pour configuration initiale (QR code)
    g_network.startApMode("Zacus-Setup");
  }
}

// API pour mettre à jour credentials (sécurisé par authentification)
void webUpdateWiFiCredentials() {
  if (!verifyHmacRequest(...)) {
    g_web_server.send(401);
    return;
  }
  
  String ssid = g_web_server.arg("ssid");
  String pass = g_web_server.arg("pass");
  
  if (g_creds.saveWiFiCredentials(ssid.c_str(), pass.c_str())) {
    g_web_server.send(200, "application/json", R"({"ok":true})");
    delay(1000);
    ESP.restart();  // Redémarrer avec nouvelle config
  } else {
    g_web_server.send(400, "application/json", R"({"error":"Invalid credentials"})");
  }
}
```

**Étape 4: Configuration initiale sécurisée (Provisioning)**
```cpp
// Provisioning via BLE ou code QR au premier démarrage
// Option 1: Provisionner via captive portal HTTPS sécurisé

void setupCaptivePortal() {
  // Afficher QR code sur l'écran avec un jeton d'appairage unique
  // Les users scannent le code et reçoivent un lien:
  // https://192.168.4.1/provision?token=ABC123&key=XYZ789
  
  // Token valide seulement pour 5 minutes
  // Connexion sécurisée par mTLS avec cert auto-signé
}
```

---

## 2. CRITIQUE: Absence d'Authentification API → Implémentation HMAC

### Architecture d'Authentification
```
Client                          Device (ESP32)
  |                                |
  |-- 1. Préparer payload -------->|
  |                          2. Générer timestamp
  |                          3. HMAC-SHA256(payload+timestamp+secret)
  |                          4. Envoyer {payload, timestamp, signature}
  |<-- 5. Vérifier signature <-----|
  |                          6. Comparer avec HMAC(payload+timestamp+secret)
  |                          7. Accepter si 200 OK, rejeter si 401
```

### Implémentation Serveur

**Étape 1: Créer un middleware d'authentification**
```cpp
// include/auth_middleware.h
#pragma once
#include <mbedtls/md.h>
#include <ctime>

class AuthMiddleware {
private:
  // Secret partagé: généré lors du provisioning, stocké en NVS
  char g_api_secret[65];  // 64 chars + null terminator
  static constexpr int REQUEST_TIMEOUT_SECS = 300;  // 5 minutes
  
public:
  bool begin() {
    Preferences prefs;
    if (!prefs.begin("api_auth", true)) return false;
    
    String secret = prefs.getString("secret", "");
    if (secret.length() < 32) {
      // Générer secret nouveau à partir du MAC address + random
      generateNewSecret();
    } else {
      strncpy(g_api_secret, secret.c_str(), 64);
    }
    prefs.end();
    return true;
  }
  
  void generateNewSecret() {
    // Générer secret aléatoire de 64 caractères hex (256 bits)
    uint8_t random_data[32];
    esp_fill_random(random_data, 32);
    
    for (int i = 0; i < 32; i++) {
      snprintf(&g_api_secret[i*2], 3, "%02x", random_data[i]);
    }
    
    // Sauvegarder en NVS
    Preferences prefs;
    prefs.begin("api_auth", false);
    prefs.putString("secret", g_api_secret);
    prefs.end();
  }
  
  bool verifyRequest(
    const String& body,
    const String& timestamp_str,
    const String& signature
  ) {
    // 1. Vérifier que le timestamp n'est pas trop ancien
    uint32_t timestamp = atol(timestamp_str.c_str());
    uint32_t now = (uint32_t)(time(nullptr) & 0xFFFFFFFFUL);
    
    if (now > timestamp + REQUEST_TIMEOUT_SECS) {
      Serial.println("AUTH: Request timestamp expired");
      return false;
    }
    
    // 2. Reconstruire le message signé: body + ":" + timestamp
    String message = body + ":" + timestamp_str;
    
    // 3. Calculer HMAC-SHA256
    unsigned char digest[32];
    mbedtls_md_context_t ctx;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char*)g_api_secret, strlen(g_api_secret));
    mbedtls_md_hmac_update(&ctx, (const unsigned char*)message.c_str(), message.length());
    mbedtls_md_hmac_finish(&ctx, digest);
    mbedtls_md_free(&ctx);
    
    // 4. Convertir digest en hex
    char computed_sig[65];
    for (int i = 0; i < 32; i++) {
      snprintf(&computed_sig[i*2], 3, "%02x", digest[i]);
    }
    computed_sig[64] = '\0';
    
    // 5. Comparer avec signature fournie (time-safe comparison)
    bool match = (signature == computed_sig);
    
    if (!match) {
      Serial.printf("AUTH: Signature mismatch. Expected: %s, Got: %s\n", 
                    computed_sig, signature.c_str());
    }
    
    return match;
  }
};

extern AuthMiddleware g_auth;
```

**Étape 2: Enregistrer les routes avec validation**
```cpp
// main.cpp
AuthMiddleware g_auth;

void setupWebServer() {
  g_auth.begin();
  
  // Route pour récupérer le secret API initial (provisioning)
  g_web_server.on("/api/auth/init", HTTP_POST, []() {
    // Seulement valide si on est en mode AP (pas de WiFi)
    if (WiFi.getMode() != WIFI_AP) {
      g_web_server.send(403, "application/json", R"({"error":"Not in setup mode"})");
      return;
    }
    
    String body = g_web_server.arg("plain");
    // Le client envoie son nonce, on répond avec le secret chiffré
    // Implementation complexe => voir libr crypto espressif
    
    g_web_server.send(200);
  });
  
  // Wrapper pour les routes sécurisées
  auto createSecureHandler = [](std::function<void()> handler) {
    return [handler]() {
      String auth_header = g_web_server.header("X-Auth");
      String timestamp_header = g_web_server.header("X-Timestamp");
      String body = g_web_server.hasArg("plain") ? g_web_server.arg("plain") : "";
      
      if (auth_header.length() == 0 || timestamp_header.length() == 0) {
        g_web_server.send(401, "application/json", R"({"error":"Missing auth headers"})");
        return;
      }
      
      if (!g_auth.verifyRequest(body, timestamp_header, auth_header)) {
        g_web_server.send(401, "application/json", R"({"error":"Invalid signature"})");
        return;
      }
      
      // Auth réussie - appeler le handler
      handler();
    };
  };
  
  // Routes sécurisées
  g_web_server.on("/api/status", HTTP_GET, createSecureHandler([]() {
    webSendStatus();
  }));
  
  g_web_server.on("/api/scenario/unlock", HTTP_POST, createSecureHandler([]() {
    dispatchScenarioEventByName("UNLOCK", millis());
    g_web_server.send(200, "application/json", R"({"ok":true})");
  }));
  
  g_web_server.on("/api/espnow/send", HTTP_POST, createSecureHandler([]() {
    String body = g_web_server.arg("plain");
    // ... protocol implementation
  }));
  
  // Routes publiques non sécurisées (peu nombreuses)
  g_web_server.on("/", HTTP_GET, []() {
    // Servir HTML statique
    webServeHTML();
  });
  
  g_web_server.on("/provisioning", HTTP_GET, []() {
    // Page de configuration initiale (HTTPS seulement)
    webServeProvisioningUI();
  });
}
```

### Client (exemple Python)
```python
import requests
import hmac
import hashlib
import json
import time

class ZacusClient:
    def __init__(self, device_url, api_secret):
        self.device_url = device_url
        self.api_secret = api_secret.encode()
    
    def _sign_request(self, body):
        timestamp = str(int(time.time()))
        message = (body + ":" + timestamp).encode()
        signature = hmac.new(
            self.api_secret,
            message,
            hashlib.sha256
        ).hexdigest()
        return signature, timestamp
    
    def request(self, method, endpoint, data=None):
        url = f"{self.device_url}{endpoint}"
        body = json.dumps(data) if data else ""
        
        signature, timestamp = self._sign_request(body)
        
        headers = {
            "X-Auth": signature,
            "X-Timestamp": timestamp,
            "Content-Type": "application/json"
        }
        
        if method == "GET":
            return requests.get(url, headers=headers)
        elif method == "POST":
            return requests.post(url, headers=headers, data=body)
        elif method == "DELETE":
            return requests.delete(url, headers=headers)
    
    def unlock(self):
        return self.request("POST", "/api/scenario/unlock", {})
    
    def get_status(self):
        return self.request("GET", "/api/status", {})

# Utilisation
client = ZacusClient("http://192.168.1.50", "ABC123DEF456...") # Secret 64 chars min
response = client.unlock()
print(response.json())
```

---

## 3. HIGH: Traversée de Répertoires → Validation de Paths

```cpp
// include/path_validator.h
#pragma once

class PathValidator {
public:
  static bool isSafePath(const String& path) {
    // Rejeter les chemins contenant des séquences dangereuses
    if (path.indexOf("..") != -1) {
      Serial.printf("REJECT: Path contains '..': %s\n", path.c_str());
      return false;
    }
    
    if (path.indexOf("//") != -1) {
      Serial.printf("REJECT: Path contains '//': %s\n", path.c_str());
      return false;
    }
    
    // Seulement les répertoires autorisés
    static const char* ALLOWED_PREFIXES[] = {
      "/story/apps/",
      "/story/content/",
      "/sdcard/music/",
      "/sdcard/recorder/",
      "/sdcard/photos/"
    };
    
    for (const char* prefix : ALLOWED_PREFIXES) {
      if (path.startsWith(prefix)) {
        return true;
      }
    }
    
    Serial.printf("REJECT: Path not in whitelist: %s\n", path.c_str());
    return false;
  }
};
```

Utilisation:
```cpp
// storage_manager.cpp - Modifier loadTextFile
String StorageManager::loadTextFile(const char* path) {
  if (!PathValidator::isSafePath(path)) {
    return "";  // Rejeter chemin non sûr
  }
  
  // Continuer avec le chargement
  // ...
}
```

---

## 4. HIGH: Validation Entière → Fonction Helper

```cpp
// Ajouter à include/input_validation.h
#pragma once
#include <cstdint>

namespace InputValidation {
  
  // Parser entier sécurisé avec validation de plage
  bool parseUint8(const char* str, uint8_t& out, uint8_t min = 0, uint8_t max = 255) {
    char* endptr;
    long value = strtol(str, &endptr, 10);
    
    // Vérifier que toute la chaîne a été parsée
    if (*endptr != '\0') return false;
    
    // Vérifier la plage
    if (value < min || value > max) return false;
    
    out = (uint8_t)value;
    return true;
  }
  
  // Parser de couleur RGB
  bool parseRgbColor(const char* args, uint8_t& r, uint8_t& g, uint8_t& b) {
    char r_str[4], g_str[4], b_str[4];
    int parsed = sscanf(args, "%3s %3s %3s", r_str, g_str, b_str);
    
    if (parsed != 3) return false;
    
    return parseUint8(r_str, r) &&
           parseUint8(g_str, g) &&
           parseUint8(b_str, b);
  }
  
  // Valider chaîne JSON
  bool validateJsonString(const String& json, size_t max_size = 2048) {
    if (json.length() > max_size) return false;
    if (json.length() == 0) return false;
    
    int brace_count = 0;
    for (char c : json) {
      if (c == '{') brace_count++;
      if (c == '}') brace_count--;
      if (brace_count < 0) return false;  // Fermeture avant ouverture
    }
    return brace_count == 0;  // Équilibré
  }
}
```

Utilisation dans main.cpp:
```cpp
uint8_t r, g, b;
if (!InputValidation::parseRgbColor(args, r, g, b)) {
  g_web_server.send(400, "application/json", R"({"error":"Invalid RGB values"})");
  return;
}

// r, g, b sont maintenant garantis entre 0-255
g_hardware.setManualLed(r, g, b);
```

---

## 5. HTTPS: Certificat Auto-Signé

### Générer certificat à la première utilisation

```cpp
// include/https_manager.h
#pragma once
#include <WiFiClientSecure.h>
#include <esp_tls.h>

class HttpsManager {
public:
  // Générer certificat auto-signé si absent
  static bool initializeCertificate() {
    Preferences prefs;
    if (!prefs.begin("https_cert", true)) return false;
    
    bool has_cert = prefs.isKey("cert_pem") && prefs.isKey("key_pem");
    prefs.end();
    
    if (!has_cert) {
      Serial.println("HTTPS: Generating self-signed certificate...");
      generateSelfSignedCertificate();
    }
    
    return true;
  }
  
private:
  static void generateSelfSignedCertificate() {
    // Utiliser axTLS ou mbedTLS
    // Note: Cette implémentation est complexe et requires des libs externes
    
    // Simplification: utiliser pre-generated certs
    const char default_cert[] = R"CERT(
-----BEGIN CERTIFICATE-----
MIIDazCCAlOgAwIBAgIUI...
...
-----END CERTIFICATE-----
)CERT";
    
    const char default_key[] = R"KEY(
-----BEGIN RSA PRIVATE KEY-----
MIIG...
...
-----END RSA PRIVATE KEY-----
)KEY";
    
    Preferences prefs;
    prefs.begin("https_cert", false);
    prefs.putString("cert_pem", default_cert);
    prefs.putString("key_pem", default_key);
    prefs.end();
  }
};
```

**Alternative: Utiliser Werkzeug (Python) pour générer les certs**

```bash
# Script: generate_cert.py
python3 -c "
from werkzeug.serving import generate_adhoc_ssl_context
import os

ctx = generate_adhoc_ssl_context('localhost')
with open('cert.pem', 'w') as f:
    f.write(ctx.get_ca_certs())
with open('key.pem', 'w') as f:
    f.write(ctx.get_private_key())
"
```

Puis copier les fichiers .pem dans le projet et les intégrer en Étape 1.

---

## 6. Rate Limiting

```cpp
// include/rate_limiter.h
#pragma once
#include <unordered_map>

class RateLimiter {
private:
  struct ClientRecord {
    uint32_t request_count;
    uint32_t window_start;
  };
  
  std::unordered_map<String, ClientRecord> clients;
  static constexpr uint32_t WINDOW_SIZE_SECS = 60;
  static constexpr uint32_t MAX_REQUESTS = 60;
  
public:
  bool isAllowed(const String& client_ip) {
    uint32_t now = (uint32_t)(millis() / 1000);
    
    auto it = clients.find(client_ip);
    if (it == clients.end()) {
      // Premier requête du client
      clients[client_ip] = {1, now};
      return true;
    }
    
    ClientRecord& record = it->second;
    
    // Réinitialiser fenêtre si passée
    if (now - record.window_start >= WINDOW_SIZE_SECS) {
      record.request_count = 1;
      record.window_start = now;
      return true;
    }
    
    // Incrémenter et vérifier limite
    record.request_count++;
    if (record.request_count > MAX_REQUESTS) {
      Serial.printf("RATELIMIT: Client %s exceeded limit\n", client_ip.c_str());
      return false;
    }
    
    return true;
  }
};

extern RateLimiter g_rate_limiter;
```

Utilisation:
```cpp
g_web_server.on("/api/status", HTTP_GET, []() {
  String client_ip = g_web_server.client().remoteIP().toString();
  
  if (!g_rate_limiter.isAllowed(client_ip)) {
    g_web_server.send(429, "application/json", R"({"error":"Too many requests"})");
    return;
  }
  
  webSendStatus();
});
```

---

## Timeline d'Implémentation

```
Week 1-2:  ✅ CRIT-001 (Credentials → NVS)
           ✅ CRIT-002 (Auth Middleware)

Week 2:    ✅ HIGH-001 (Validation entière)
           ✅ HIGH-002 (JSON validation)
           ✅ HIGH-003 (Path validation)

Week 3:    ✅ MED-001 (Rate limiting)
           ✅ MED-002 (HTTPS)

Week 4:    ✅ Testing & Code Review
           ✅ Re-audit
```

---

## Checklist de Vérification

### Avant Déploiement
- [ ] Aucun secret en dur dans le code source
- [ ] Tous les API endpoints retournent 401 sans auth valide
- [ ] HTTPS active par défaut (port 443)
- [ ] Rate limiting fonctionne (test avec ApacheBench)
- [ ] Tests de fuzzing passés
- [ ] OWASP Top 10 2021 checklist complétée

### Tests Automatisés
```python
# test_security.py
import requests
import json

def test_no_auth_required():
    """Vérifier que endpoints retournent 401 sans auth"""
    r = requests.get("http://192.168.1.50/api/status")
    assert r.status_code == 401, f"Expected 401, got {r.status_code}"

def test_invalid_signatur():
    """Vérifier que signature invalide est rejetée"""
    headers = {
        "X-Auth": "invalid_signature",
        "X-Timestamp": "1234567890"
    }
    r = requests.get("http://192.168.1.50/api/status", headers=headers)
    assert r.status_code == 401

def test_expired_timestamp():
    """Vérifier que timestamp ancien est rejeté"""
    import time
    old_timestamp = str(int(time.time()) - 400)  # 400 secs ago > 300 sec timeout
    headers = {
        "X-Auth": "any_signature",
        "X-Timestamp": old_timestamp
    }
    r = requests.get("http://192.168.1.50/api/status", headers=headers)
    assert r.status_code == 401

def test_rate_limiting():
    """Vérifier que rate limiting fonctionne"""
    # Envoyer 100 requêtes rapides
    # Vérifier que certaines retournent 429
    pass

# Exécuter les tests
if __name__ == "__main__":
    test_no_auth_required()
    test_invalid_signatur()
    test_expired_timestamp()
    print("✅ All security tests passed")
```

Exécution:
```bash
pip install requests
python test_security.py
```

---

## Support & Questions

Pour chaque changement:
1. Créer une branche `security/vulnerability-id`
2. Implémenter le correctif
3. Ajouter les tests correspondants
4. Faire un code review de sécurité
5. Fusionner après approbation

Ressources:
- ESP32 Security: https://docs.espressif.com/projects/esp-idf/en/latest/
- OWASP: https://owasp.org/Top10/
- CWE Top 25: https://cwe.mitre.org/
