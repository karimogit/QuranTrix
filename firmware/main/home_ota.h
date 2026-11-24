#ifndef HOME_OTA_H
#define HOME_OTA_H
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>

void home_ota_init(void);
/* Check GitHub for a newer QuranTrix.bin (network required). */
esp_err_t home_ota_check(bool *update_available, char *latest, size_t latest_len, char *url,
                         size_t url_len);
/* Begin download into the inactive OTA slot; returns immediately while a worker runs. */
esp_err_t home_ota_start(bool auto_started);
/* Check for a newer release and install it asynchronously when one is available. */
esp_err_t home_ota_update_now(void);
/* Once per day when auto-update is on and the desk is idle online. */
void home_ota_auto_tick(void);
cJSON *home_ota_status_json(void);

#endif