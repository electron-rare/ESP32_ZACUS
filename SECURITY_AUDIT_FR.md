# Audit de Sécurité - ESP32_ZACUS

**Date:** 1er mars 2026  
**Projet:** ESP32_ZACUS - Firmware embarqué pour ESP32-S3  
**Plateforme:** Arduino/PlatformIO  
**Verdict:** 🔴 **NON PRÊT POUR LA PRODUCTION**

---

## 📋 Résumé Exécutif

L'audit a identifié **12 vulnérabilités de sécurité** :

| Sévérité | Nombre | Exemples |
|----------|--------|----------|
| 🔴 CRITIQUE | 2 | Identifiants stockés en dur, absence d'authentification API |
| 🔴 HAUTE | 3 | Analyse JSON non validée, traversée de répertoires |
| 🟡 MOYENNE | 4 | Analyse de commandes faible, absence de HTTPS |
| 🟢 BASSE | 3 | Hardening du compilateur manquant, logging verbeux |

**Temps de remédiation estimé :** 2-3 semaines (correctifs critiques), 4-6 semaines (sécurisation complète)

---

## 🔴 Vulnérabilités CRITIQUES

### CRIT-001: Identifiants WiFi Codés en Dur

**Localisation:** `ui_freenove_allinone/src/storage_manager.cpp:52`

**Description:** Les identifiants WiFi sont intégrés en dur dans le fichier de configuration JSON embedded:

```json
{
  "local_ssid": "Les cils",
  "local_password": "mascarade",
  "ap_default_ssid": "Freenove-Setup",
  "ap_default_password": "mascarade"
}
```

**Risques:**
- ✅ Visible dans le binaire compilé
- ✅ Extractible via reverse engineering
- ✅ Faible mot de passe (8 caractères, pas de symboles)
- ✅ Compromettre accès réseau local

**Correction recommandée:**

```cpp
// ❌ AVANT: Stockage en dur
{"/story/apps/APP_WIFI.json", R"JSON(...\"local_password\":\"mascarade\"...)JSON"}

// ✅ APRÈS: Stockage chiffré dans NVS
#include <Preferences.h>
Preferences prefs;
prefs.begin("wifi");
char password[65];
prefs.getString("password", password, 65);  // Lecture depuis NVS
// Chiffrement: Utiliser la clé de sécurité de l'ESP32
```

**Priorité:** 🚨 CRITIQUE - Déployer avant la prochaine version

---

### CRIT-002: Endpoints API Web Non Authentifiés

**Localisation:** `ui_freenove_allinone/src/main.cpp:2007+`

**Description:** Tous les endpoints Web (40+) manquent de vérification d'authentification:

```cpp
// ❌ Code actuel - AUCUNE AUTHENTIFICATION
g_web_server.on("/api/scenario/unlock", HTTP_POST, []() {
  const bool ok = dispatchScenarioEventByName("UNLOCK", now_ms);
  webSendResult("unlock", ok);
});

g_web_server.on("/api/wifi/connect", HTTP_POST, []() {
  g_network.connectSta(ssid, password);  // Accepte TOUT
});

g_web_server.on("/api/espnow/send", HTTP_POST, []() {
  g_network.sendEspNowTarget(target, payload);  // Pas de validation
});
```

**Scénario d'attaque:**

```bash
# Attaquant sur le réseau WiFi local:

# 1. Découvrir l'appareil
nmap -p 80 192.168.1.0/24

# 2. Déverrouiller les scénarios
curl -X POST http://192.168.1.50/api/scenario/unlock

# 3. Contourner la logique du jeu
for i in {1..100}; do
  curl -X POST http://192.168.1.50/api/scenario/next
done

# 4. Injecter des commandes ESP-NOW vers d'autres appareils
curl -X POST http://192.168.1.50/api/espnow/send \
  -H "Content-Type: application/json" \
  -d '{"target":"AA:BB:CC:DD:EE:FF","payload":"malicious"}'

# 5. Accéder aux données sensibles
curl http://192.168.1.50/api/camera/snapshot.jpg > stolen.jpg
curl http://192.168.1.50/api/media/record/list
```

**Endpoints exposés:**
- `/api/scenario/unlock` - Déverrouille les niveaux du jeu
- `/api/scenario/next` - Saute les étapes du scénario
- `/api/wifi/disconnect` - Isolate l'appareil (DoS)
- `/api/wifi/connect` - Injection de credentials (MITM)
- `/api/network/espnow/on` - Active le protocole sans protection
- `/api/espnow/send` - Injection de commandes dans le maillage
- `/api/camera/snapshot.jpg` - Violation de vie privée
- `/api/media/record/start` - Enregistrement audio non autorisé
- `/api/hardware/led` - Confirmation de vulnérabilité

**Correction recommandée:**

```cpp
// ✅ APRÈS: Middleware d'authentification HMAC

#include <mbedtls/md.h>

bool verifyHmacRequest(const String& body, const String& signature) {
  const char* secret = "YOUR_SHARED_SECRET_MIN_32_CHARS";
  unsigned char digest[32];
  
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret, 32);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)body.c_str(), body.length());
  mbedtls_md_hmac_finish(&ctx, digest);
  mbedtls_md_free(&ctx);
  
  char hex_digest[65];
  for (int i = 0; i < 32; i++) {
    snprintf(&hex_digest[i*2], 3, "%02x", digest[i]);
  }
  
  return signature == hex_digest;
}

// Envelopper tous les handlers
g_web_server.on("/api/scenario/unlock", HTTP_POST, []() {
  String body;
  if (g_web_server.hasArg("plain")) {
    body = g_web_server.arg("plain");
  }
  String signature = g_web_server.header("X-Signature");
  
  if (!verifyHmacRequest(body, signature)) {
    g_web_server.send(401, "application/json", R"({"error":"Unauthorized"})");
    return;
  }
  
  // Traitement de la requête authentifiée
  dispatchScenarioEventByName("UNLOCK", millis());
});
```

**Alternative avec protocole Bearer:**

```cpp
// ✅ Utiliser des tokens Bearer avec timeout
#include <time.h>

struct AuthToken {
  char token[65];
  uint32_t issued_at;
  uint32_t expires_at;
};

bool verifyBearerToken(const String& header) {
  if (!header.startsWith("Bearer ")) return false;
  
  String token = header.substring(7);
  time_t now = time(nullptr);
  
  // Vérifier token et expiration
  // (Implémenter la génération de token sur initialisation)
  return tokenIsValid(token, now);
}

g_web_server.on("/api/scenario/unlock", HTTP_POST, []() {
  String auth = g_web_server.header("Authorization");
  if (!verifyBearerToken(auth)) {
    g_web_server.send(401);
    return;
  }
  // ...
});
```

**Priorité:** 🚨 CRITIQUE - Bloquer le déploiement jusqu'à correction

---

## 🔴 Vulnérabilités HAUTE SÉVÉRITÉ

### HIGH-001: Analyse d'Entiers Non Fiable (sscanf)

**Localisation:** `ui_freenove_allinone/src/main.cpp:1807`

**Problème:** Les valeurs de couleur sont analysées sans validation de plage:

```cpp
// ❌ Code actuel
char args[] = "999999999 999999999 999999999 255";
int r, g, b, brightness, pulse;
std::sscanf(args, "%d %d %d %d %d", &r, &g, &b, &brightness, &pulse);
// R = 999999999 -> cast to uint8_t = 255 (incorrect)

// Payload d'attaque
POST /api/hardware/led
"HW_LED_SET 999999999 999999999 999999999 999999999 999999999"
```

**Correction:**

```cpp
// ✅ Validation des plages
int parseColorValue(const char* args, uint8_t& r, uint8_t& g, uint8_t& b) {
  int ri, gi, bi;
  const int count = std::sscanf(args, "%d %d %d", &ri, &gi, &bi);
  
  if (count != 3) return -1;
  
  // Valider les plages AVANT conversion de type
  if (ri < 0 || ri > 255) return -2;
  if (gi < 0 || gi > 255) return -2;
  if (bi < 0 || bi > 255) return -2;
  
  r = (uint8_t)ri;
  g = (uint8_t)gi;
  b = (uint8_t)bi;
  return 0;
}
```

---

### HIGH-002: Validation JSON Manquante

**Localisation:** `ui_freenove_allinone/src/main.cpp:654, 1239`

**Problème:** Les documents JSON ne sont validés que pour les erreurs de parsing, pas pour le contenu:

```cpp
// ❌ Code actuel
StaticJsonDocument<512> doc;
auto error = deserializeJson(doc, payload);
if (!error) {
  // Aucune validation de schéma, taille, profondeur
}

// Attaque: JSON profondément imbriqué
{
  "level1": {
    "level2": { "level3": { "level4": { /* ... */ } } }
  }
}
```

**Correction:**

```cpp
// ✅ Validation de schéma
bool validateJsonPayload(const StaticJsonDocument<512>& doc) {
  // Vérifier champs obligatoires
  if (!doc.containsKey("cmd")) return false;
  if (!doc.containsKey("payload")) return false;
  
  // Vérifier types
  if (!doc["cmd"].is<const char*>()) return false;
  if (strlen(doc["cmd"]) > 32) return false;  // Limite de longueur
  
  // Vérifier profondeur (max 3 niveaux)
  return measureJsonDepth(doc) <= 3;
}

int measureJsonDepth(const JsonVariant& v, int depth = 0) {
  if (depth > 10) return INT_MAX;  // Sécurité
  if (v.is<JsonObject>() || v.is<JsonArray>()) {
    int max = depth;
    for (auto kv : v.as<JsonObject>()) {
      max = std::max(max, measureJsonDepth(kv.value(), depth + 1));
    }
    return max;
  }
  return depth;
}
```

---

### HIGH-003: Traversée de Répertoires (Path Traversal)

**Localisation:** `ui_freenove_allinone/src/storage_manager.cpp:308`

**Problème:** Les chemins ne sont pas validés contre les séquences `../`:

```cpp
// ❌ Code actuel
String normalizeAbsolutePath(const char* path) {
  String normalized = path;
  if (!normalized.startsWith("/")) {
    normalized = "/" + normalized;
  }
  return normalized;  // Peut contenir "/../../../etc/passwd"
}

// Attaque
GET /api/media/files?kind=../../story/apps/APP_WIFI.json
// → Lit le fichier de configuration WiFi contenant les credentials
```

**Correction:**

```cpp
// ✅ Validation stricte
bool isPathTrusted(const String& path) {
  // Refuser les séquences de traversée
  if (path.indexOf("..") != -1) return false;
  if (path.indexOf("//") != -1) return false;
  
  // Seulement les préfixes autorisés
  const char* allowed[] = {"/story/", "/sdcard/music/", "/sdcard/recorder/"};
  for (const char* prefix : allowed) {
    if (path.startsWith(prefix)) return true;
  }
  return false;
}

String normalizeAbsolutePath(const char* path) {
  String normalized = path;
  if (!normalized.startsWith("/")) {
    normalized = "/" + normalized;
  }
  
  if (!isPathTrusted(normalized)) {
    Serial.printf("REJECT: Path traversal attempt: %s\n", path);
    return "";  // Rebut la requête
  }
  return normalized;
}
```

---

## 🟡 Vulnérabilités MOYENNE SÉVÉRITÉ

### MED-001: Absence de Rate Limiting

**Localisation:** `ui_freenove_allinone/src/main.cpp:3366` (boucle handleClient)

**Problème:** Aucune limite de débit sur les requêtes. Un attaquant peut saturer l'appareil:

```cpp
// ❌ Code actuel
void loop() {
  g_web_server.handleClient();  // Aucune limite
  // ...
}

// Attaque: Saturation
for i in range(10000):
  curl http://192.168.1.50/api/status
```

**Correction:**

```cpp
// ✅ Écrêtage par IP
#include <unordered_map>
#include <ctime>

class RateLimiter {
  static const int MAX_REQUESTS_PER_MINUTE = 60;
  std::unordered_map<String, std::pair<int, time_t>> ip_counts;
  
public:
  bool isAllowed(const String& ip) {
    time_t now = time(nullptr);
    auto& record = ip_counts[ip];
    
    // Réinitialiser si la minute est écoulée
    if (now - record.second > 60) {
      record.first = 0;
      record.second = now;
    }
    
    record.first++;
    return record.first <= MAX_REQUESTS_PER_MINUTE;
  }
} g_rate_limiter;

// Dana le web server handler
g_web_server.on("/api/status", HTTP_GET, []() {
  if (!g_rate_limiter.isAllowed(g_web_server.client().remoteIP().toString())) {
    g_web_server.send(429, "text/plain", "Too Many Requests");
    return;
  }
  // Traitement de la requête
});
```

---

### MED-002: Absence de HTTPS

**Localisation:** `ui_freenove_allinone/src/main.cpp:46`

```cpp
// ❌ Code actuel
WebServer g_web_server(80);  // HTTP en clair

// Attaque: Sniffing de réseau
tcpdump -i eth0 -A 'tcp port 80'
# Capture directe des credentials WiFi, commandes, vidéos
```

**Correction:**

```cpp
// ✅ HTTPS avec certificat auto-signé
#include <WebServerSecure.h>
#include <WiFiClientSecure.h>
#include <Certificate.h>  // Certificat généré à la première utilisation

// Générer certificat auto-signé au premier démarrage
void generateSelfSignedCertificate() {
  // Utiliser axTLS ou mbedTLS pour générer les clés
  // Stocker dans NVS (Preferences)
}

WebServerSecure g_web_server(443);

void setupWebServer() {
  if (!certExistsInNVS()) {
    generateSelfSignedCertificate();
  }
  
  const char* cert = readCertFromNVS();
  const char* key = readKeyFromNVS();
  g_web_server.setServerKeyAndCert_PEM(key, cert);
  
  // Enregistrer les mêmes routes que précédemment
  g_web_server.on("/api/", ...);
}
```

---

### MED-003 & MED-004: Analyse de Commandes Faible, Injection de Médias

Voir détails complets dans `SECURITY_AUDIT_REPORT.json`

---

## 🟢 Vulnérabilités BASSE SÉVÉRITÉ

### LOW-001: Hardening du Compilateur Manquant

**Localisation:** `platformio.ini:97`

```ini
# ❌ Défaut
build_flags = -O2 -ffast-math

# ✅ Recommandé
build_flags = 
  -O2 
  -ffast-math
  -fstack-protector-strong
  -Wformat -Wformat-security
  -D_FORTIFY_SOURCE=2
```

---

## 📊 Matrice de Remédiation

| ID | Titre | Sévérité | Effort | Impact |
|----|----|----------|--------|--------|
| CRIT-001 | Credentials | 🔴 CRIT | Moyen | ✅ ÉLEVÉ |
| CRIT-002 | API Auth | 🔴 CRIT | Haut | ✅ ÉLEVÉ |
| HIGH-001 | sscanf | 🔴 HAUTE | Faible | ✅ MOYEN |
| HIGH-002 | JSON | 🔴 HAUTE | Faible | ✅ MOYEN |
| HIGH-003 | Path | 🔴 HAUTE | Faible | ✅ MOYEN |
| MED-001 | Rate Limit | 🟡 MOY | Moyen | ✅ MOYEN |
| MED-002 | HTTPS | 🟡 MOY | Haut | ✅ ÉLEVÉ |
| MED-003 | Serial | 🟡 MOY | Faible | ✅ FAIBLE |
| MED-004 | Media | 🟡 MOY | Faible | ✅ FAIBLE |
| LOW-001 | Hardening | 🟢 BASSE | Faible | ✅ FAIBLE |
| LOW-002 | Debug | 🟢 BASSE | Nul | ✅ TRÈS FAIBLE |
| LOW-003 | Sanitize | 🟢 BASSE | Faible | ✅ TRÈS FAIBLE |

---

## 🚀 Plan de Remédiation

### Phase 1: Critique (2 semaines)
1. ✅ Déplacer credentials vers NVS chiffré
2. ✅ Implémenter authentification HMAC-SHA256
3. ✅ Ajouter validation JSON avec limites de taille

### Phase 2: Haute (1 semaine)
1. ✅ Implémenter vérification de path traversal
2. ✅ Ajouter validation de plage entière
3. ✅ Implémenter rate limiting

### Phase 3: Moyenne (2 semaines)
1. ✅ Déployer HTTPS avec certificat auto-signé
2. ✅ Sanitiser entrées de commandes série
3. ✅ Ajouter hardening du compilateur

### Phase 4: Production (1 semaine)
1. ✅ Tests de pénétration
2. ✅ Code review de sécurité
3. ✅ Audit de remédiation

---

## 📋 Checklist de Vérification Post-Remédiation

- [ ] Aucun secret en dur détecté (grep -r "password" src/)
- [ ] Tous les endpoints API retournent 401 sans auth
- [ ] Tests HTTPS avec vérification de certificat
- [ ] Tests fuzzing JSON avec AFL
- [ ] Tests de path traversal automatisés
- [ ] Audit de code de sécurité complété
- [ ] Scan de vulnérabilités dépendances (`pio update`)
- [ ] Tests de charge (100 requêtes/sec sans crash)

---

## 📚 Références

- **OWASP Top 10 2021:** Broken Access Control (A01)
- **CWE-798:** Hardcoded Credentials
- **CWE-306:** Missing Authentication
- **CWE-22:** Path Traversal
- **ESP32 Security Best Practices:** https://docs.espressif.com/
- **Arduino SafeString Library:** Input sanitization

---

**Rapport généré:** 1er mars 2026  
**Auditeur:** Expert en Sécurité Systèmes Embarqués  
**Confidentiel - Utilisation Interne Uniquement**
