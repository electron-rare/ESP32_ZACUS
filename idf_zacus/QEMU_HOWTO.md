# QEMU smoke test for `idf_zacus`

## What QEMU can do today

- Boot the firmware end-to-end (NVS init, partition table, app_main).
- Validate that new components do not break boot.
- Surface any link-time / runtime init crashes that escape the build.

Tested 2026-05-24: firmware with the new `POST /game/scenario` handler boots
cleanly to `app_main()` in QEMU 9.0.0 (esp_develop build).

## What QEMU can NOT do (yet)

- **WiFi radio**: stubbed. The board comes up in AP fallback but no station
  ever associates, so the IP netif never gets an address and the HTTP server
  (which waits for `IP_EVENT_STA_GOT_IP`) doesn't bind.
- **PSRAM**: QEMU's esp32s3 machine does not emulate the Octal PSRAM the
  Freenove N16R8 ships with. Use `sdkconfig.qemu` to disable.
- **esp-sr / WakeNet**: depends on PSRAM, also disabled in `sdkconfig.qemu`.
- **WiFi-driven HTTP smoke**: see the "future work" section below.

## Run

```bash
. $HOME/esp/esp-idf/export.sh
export PATH=$HOME/.espressif/tools/qemu-xtensa/esp_develop_9.0.0_20240606/qemu/bin:$PATH

# clean reconfigure with the QEMU overrides
rm -rf build sdkconfig
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" set-target esp32s3
idf.py build

# launch with port forward for the future ethernet integration
idf.py qemu --qemu-extra-args="-nic user,model=open_eth,hostfwd=tcp::8580-:80"

# in another terminal — when HTTP arrives, this is the smoke test
curl -sS http://127.0.0.1:8580/healthz                            # → "ok"
curl -sS -X POST -H "Content-Type: application/json" \
  --data @../../../game/scenarios/zacus_cond_demo.ir.json         \
  http://127.0.0.1:8580/game/scenario
```

Press `Ctrl-A x` to exit the QEMU console.

## Restore the production build

The `sdkconfig.qemu` overrides break the real board (no PSRAM = no esp-sr =
no voice pipeline). To return to the canonical config:

```bash
rm -rf build sdkconfig
idf.py set-target esp32s3                # picks sdkconfig.defaults only
idf.py build flash monitor               # real board path
```

`sdkconfig.qemu` is committed but never used by the default build — only when
explicitly listed in `SDKCONFIG_DEFAULTS`.

## Future work — HTTP smoke in QEMU

The main blocker is `main.c`: it gates `ota_server_init()` / `game_endpoint_init()`
on a WiFi `IP_EVENT_STA_GOT_IP` callback. To unblock HTTP testing under QEMU
without WiFi:

1. Add a `CONFIG_ZACUS_QEMU_ETHERNET=y` Kconfig option in `main/Kconfig.projbuild`
   (default `n`).
2. In `app_main()`, if the option is set, initialise the `esp_eth` driver against
   the `open_eth` NIC and use its `IP_EVENT_ETH_GOT_IP` event to start the HTTP
   stack — same lifecycle as the WiFi path, different transport.
3. Add `CONFIG_ZACUS_QEMU_ETHERNET=y` to `sdkconfig.qemu`.

Once that lands, `curl http://127.0.0.1:8580/game/scenario` from the host hits
the real handler inside QEMU and we get a true integration test of the
hot-load path (scenario validation, LittleFS write, deferred reboot).

Estimated effort: ~80 LOC + Kconfig + one `esp_eth_open_eth_new()` glue —
half a day of work.
