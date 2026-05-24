// scenario_server.h — start the BOX-3 minimal HTTP server that accepts
// POST /game/scenario (Runtime 3 IR hot-load via reboot).
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t scenario_server_start(void);

#ifdef __cplusplus
}
#endif
