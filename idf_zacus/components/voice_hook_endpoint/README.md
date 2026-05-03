# voice_hook_endpoint

REST bridge between the **PLIP** retro-telephone annex (Si3210 SLIC
hook switch on the JLCPCB-fab'd PCB) and the **Zacus master** voice
pipeline. Slice 10 of the IDF migration.

## Why

The escape-room narrative: *"le téléphone sonne, décroche pour parler
à Zacus"*. Wake-word ("hi esp") still works as a backup, but the
canonical interaction is to physically lift the handset.

When PLIP detects an off-hook transition on the Si3210 INT line it
POSTs the new state to the master ESP32 over Wi-Fi. The master arms
the voice pipeline immediately (LISTENING + capture + WS streaming),
**bypassing the wake-word detector entirely**. On-hook closes the WS
and returns the pipeline to IDLE.

## Hardware-side contract (PLIP firmware)

PLIP firmware lives in a separate tree (`PLIP_FIRMWARE/`) and is
**not** modified by this slice. The only thing it must do:

1. Discover the master ESP32 IP — mDNS lookup `zacus-master.local` is
   the planned mechanism (not yet implemented either side); a static
   IP from DHCP reservation works as a fallback.
2. On the Si3210 INT ISR (or its debounced FreeRTOS task), POST to
   `/voice/hook` whenever the hook state actually changes.
3. Treat any non-2xx response as transient — retry once after 250 ms
   then give up. Do **not** block the audio path on the REST call.

## Wire protocol

Listener: existing `esp_http_server` instance on port **80** (shared
with `ota_server` — same TCP socket, same worker pool, no second
httpd brought up).

### `POST /voice/hook`

Request body (JSON, max 256 bytes):

```json
{ "state": "off", "reason": "pickup" }
```

- `state` (required, string): `"off"` = handset lifted (off-hook,
  user wants to talk) or `"on"` = handset hung up (on-hook).
- `reason` (optional, string): free-form diagnostic tag — `pickup`,
  `hangup`, `ring_started`, `ring_timeout`, etc. Logged but never
  acted on.

Responses:

| Status | Body | Meaning |
|--------|------|---------|
| 200 | `{"status":"listening","mute_gate":false}` | off-hook accepted |
| 200 | `{"status":"idle"}`                        | on-hook accepted |
| 400 | `{"error":"missing 'state'"}`              | malformed/empty JSON or missing key |
| 400 | `{"error":"bad state"}`                    | `state` not in `{"off","on"}` |
| 405 | `{"error":"use POST"}`                     | GET on `/voice/hook` |
| 413 | `{"error":"body must be 1..256 bytes"}`    | body too large |

### `GET /voice/hook/state`

Debug-only introspection. Returns the live voice pipeline state:

```json
{
  "voice_state": "listening",
  "wake_word_active": true,
  "streaming": true
}
```

`voice_state` is one of `idle | listening | speaking | muted`.

## curl examples

```bash
# Off-hook (PLIP picks up the handset)
curl -X POST http://<zacus-master-ip>/voice/hook \
  -H "Content-Type: application/json" \
  -d '{"state":"off","reason":"pickup"}'

# On-hook (PLIP hangs up)
curl -X POST http://<zacus-master-ip>/voice/hook \
  -H "Content-Type: application/json" \
  -d '{"state":"on","reason":"hangup"}'

# Probe current state
curl http://<zacus-master-ip>/voice/hook/state
```

## Behaviour vs. wake-word

The voice pipeline runs in **mixed mode**: both the wake-word
detector (`enable_wake_word = true`) and the REST hook are armed at
the same time. Whichever fires first wins. This is deliberate —
the Si3210 + RJ9 combiné is the primary user-facing surface but if
the phone is unplugged or the hook switch fails the master remains
voice-controllable from any open mic in the room.

The off-hook handler is **idempotent**: bouncing the hook switch
inside the Si3210 debounce window cannot corrupt state. The handler
forces `LISTENING + start_capture + start_streaming` every time, and
all three are no-ops when already in those states.

## Component dependencies

Registered with:

```cmake
REQUIRES
    esp_http_server   # the shared httpd_handle_t
    json              # cJSON for body parsing
    voice_pipeline    # state-machine + capture/stream control
    ota_server        # supplies httpd_handle via ota_server_get_handle()
    freertos
    log
```

Init pattern in `main.c`:

```c
esp_err_t ota_err = ota_server_init();
if (ota_err == ESP_OK) {
    httpd_handle_t httpd = ota_server_get_handle();
    voice_hook_endpoint_init(httpd);
}
```
