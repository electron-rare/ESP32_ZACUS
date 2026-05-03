# game_endpoint

REST surface for **runtime game configuration**. Slice 12 of the IDF
migration. Currently exposes a single resource — the hints-engine
group profile — but the component is the natural home for additional
runtime tunables (cooldowns, voice persona, etc.) as scenarios grow.

## Why

The hints engine adapts its policy to the audience: `TECH`,
`NON_TECH`, `MIXED`, or `BOTH`. Until this slice the value was baked
into NVS via `idf.py nvs-partition-gen` or the dashboard's flash
helper — both require a power cycle. The game master needs to be able
to change profile mid-session if the actual room composition differs
from the booking, so we expose a REST endpoint that updates both the
in-RAM hints client and the persistent NVS slot.

## Routes

Listener: existing `esp_http_server` instance on port **80** (shared
with `ota_server` and `voice_hook_endpoint` — same TCP socket, no
second httpd).

### `GET /game/group_profile`

Returns the live profile (whatever `hints_client_group_profile()`
reports — defaults to `MIXED` after init).

```json
{ "group_profile": "MIXED" }
```

### `POST /game/group_profile`

Body (JSON, max 256 bytes):

```json
{ "group_profile": "NON_TECH" }
```

| Status | Body | Meaning |
|--------|------|---------|
| 200 | `{"status":"ok","group_profile":"NON_TECH"}` | accepted, NVS persisted |
| 400 | `{"error":"missing 'group_profile'"}` | empty / non-string field |
| 400 | `{"error":"invalid group_profile, must be one of [TECH, NON_TECH, MIXED, BOTH]"}` | rejected by hints client whitelist |
| 400 | `{"error":"malformed json"}` | body did not parse |
| 413 | `{"error":"body must be 1..256 bytes"}` | body too large |
| 500 | `{"status":"runtime_only","group_profile":"NON_TECH","warning":"nvs write failed: …"}` | hints client updated but NVS commit failed (will not survive reboot) |

## curl examples

```bash
# Set the profile (uses mDNS — see main.c slice 12 wire-up)
curl -X POST http://zacus-master.local/game/group_profile \
  -H "Content-Type: application/json" \
  -d '{"group_profile":"NON_TECH"}'

# Probe current state
curl http://zacus-master.local/game/group_profile

# Static-IP fallback
curl -X POST http://192.168.0.<master-ip>/game/group_profile \
  -H "Content-Type: application/json" \
  -d '{"group_profile":"MIXED"}'
```

## Persistence

Successful POSTs write to NVS namespace `zacus`, key `group_profile`.
This is the same slot `main.c` reads at boot to seed the hints client,
so a successful POST survives reboot without a flash step. The
write/commit pair runs on the httpd worker task; expect ~5–15 ms of
flash latency.

If the hints client validation passes but `nvs_set_str` /
`nvs_commit` fails (e.g. NVS partition full), the response is `500`
with `status: runtime_only` so the operator can decide whether to
keep going or force a reboot.

## Component dependencies

```cmake
REQUIRES
    esp_http_server   # the shared httpd_handle_t
    json              # cJSON for body parsing
    nvs_flash         # nvs_open / nvs_set_str / nvs_commit
    hints_client      # validation + push to in-RAM state
    ota_server        # supplies httpd_handle via ota_server_get_handle()
    freertos
    log
```

Init pattern in `main.c` (slice 12):

```c
esp_err_t ota_err = ota_server_init();
if (ota_err == ESP_OK) {
    httpd_handle_t httpd = ota_server_get_handle();
    voice_hook_endpoint_init(httpd);
    game_endpoint_init(httpd);
}
```
