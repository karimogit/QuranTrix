#include "home_ota.h"
#include "home_runtime.h"
#include "home_radio.h"
#include "qurantrix_product.h"
#include "qurantrix_settings.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "home_ota";
#define OTA_CHECK_INTERVAL_S (24 * 60 * 60)
#define OTA_HTTP_TIMEOUT_MS 600000

typedef enum { OTA_IDLE = 0, OTA_CHECKING, OTA_UPDATING, OTA_FAILED } ota_phase_t;

static volatile ota_phase_t phase = OTA_IDLE;
static char cached_latest[32];
static char cached_url[512];
static bool cached_available;
static int64_t last_check_epoch;
static char last_error[96];
static volatile bool ota_trigger_active;

static void trim_version(char *s)
{
    if (!s)
        return;
    while (*s && isspace((unsigned char)*s))
        memmove(s, s + 1, strlen(s));
    if (*s == 'v' || *s == 'V')
        memmove(s, s + 1, strlen(s));
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

static bool parse_version(const char *text, int out[3])
{
    char copy[32];
    snprintf(copy, sizeof copy, "%.31s", text ? text : "");
    trim_version(copy);
    return sscanf(copy, "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
}

static int version_cmp(const char *a, const char *b)
{
    int va[3] = {0}, vb[3] = {0};
    if (!parse_version(a, va) || !parse_version(b, vb))
        return strcmp(a ? a : "", b ? b : "");
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i])
            return va[i] < vb[i] ? -1 : 1;
    }
    return 0;
}

static const char *running_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d && d->version[0] ? d->version : QURANTRIX_VERSION;
}

static esp_err_t nvs_load_check_time(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition("home_nvs", "qurantrix", NVS_READONLY, &h) != ESP_OK)
        return ESP_FAIL;
    uint64_t v = 0;
    esp_err_t e = nvs_get_u64(h, "ota_last", &v);
    nvs_close(h);
    if (e == ESP_OK)
        last_check_epoch = (int64_t)v;
    return e;
}

static void nvs_save_check_time(int64_t when)
{
    last_check_epoch = when;
    nvs_handle_t h;
    if (nvs_open_from_partition("home_nvs", "qurantrix", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u64(h, "ota_last", (uint64_t)when);
    nvs_commit(h);
    nvs_close(h);
}

void home_ota_init(void) { nvs_load_check_time(); }

static esp_err_t http_read_body(const char *url, const char *accept, char **out, size_t *out_len)
{
    *out = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 45000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = QURANTRIX_HTTP_UA,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return ESP_ERR_NO_MEM;
    if (accept)
        esp_http_client_set_header(client, "Accept", accept);
    esp_err_t e = esp_http_client_open(client, 0);
    if (e != ESP_OK)
        goto done;
    int status = esp_http_client_fetch_headers(client);
    if (status < 200 || status >= 300) {
        e = ESP_FAIL;
        goto done;
    }
    int len = esp_http_client_get_content_length(client);
    size_t cap = len > 0 ? (size_t)len + 1 : 65536;
    if (cap > 256 * 1024)
        cap = 256 * 1024;
    char *buf = malloc(cap);
    if (!buf) {
        e = ESP_ERR_NO_MEM;
        goto done;
    }
    size_t total = 0;
    while (total + 1 < cap) {
        int n = esp_http_client_read(client, buf + total, (int)(cap - total - 1));
        if (n < 0) {
            e = ESP_FAIL;
            break;
        }
        if (n == 0)
            break;
        total += (size_t)n;
    }
    if (e != ESP_OK) {
        free(buf);
        goto done;
    }
    buf[total] = 0;
    *out = buf;
    *out_len = total;
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return e;
}

static bool pick_asset(cJSON *release, char *url, size_t url_len, char *ver, size_t ver_len)
{
    if (!cJSON_IsObject(release))
        return false;
    cJSON *tag = cJSON_GetObjectItem(release, "tag_name");
    if (!cJSON_IsString(tag) || !tag->valuestring)
        return false;
    snprintf(ver, ver_len, "%.31s", tag->valuestring);
    cJSON *assets = cJSON_GetObjectItem(release, "assets");
    if (!cJSON_IsArray(assets))
        return false;
    cJSON *item;
    cJSON_ArrayForEach(item, assets)
    {
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *dl = cJSON_GetObjectItem(item, "browser_download_url");
        if (!cJSON_IsString(name) || !cJSON_IsString(dl))
            continue;
        if (strcmp(name->valuestring, QURANTRIX_OTA_ASSET))
            continue;
        snprintf(url, url_len, "%.511s", dl->valuestring);
        return url[0] != 0;
    }
    return false;
}

static esp_err_t ota_check_release(bool *update_available, char *latest, size_t latest_len,
                                   char *url, size_t url_len)
{
    phase = OTA_CHECKING;
    char *body = NULL;
    size_t body_len = 0;
    esp_err_t e = http_read_body(QURANTRIX_OTA_RELEASES_URL,
                                 "application/vnd.github+json", &body, &body_len);
    if (e != ESP_OK || !body) {
        snprintf(last_error, sizeof last_error, "release_check_failed");
        phase = OTA_FAILED;
        return e == ESP_OK ? ESP_FAIL : e;
    }
    cJSON *j = cJSON_ParseWithLength(body, body_len);
    memset(body, 0, body_len);
    free(body);
    if (!j) {
        snprintf(last_error, sizeof last_error, "release_parse_failed");
        phase = OTA_FAILED;
        return ESP_FAIL;
    }
    char found_ver[32] = "";
    char found_url[512] = "";
    bool ok = pick_asset(j, found_url, sizeof found_url, found_ver, sizeof found_ver);
    cJSON_Delete(j);
    nvs_save_check_time(time(NULL) >= 1704067200 ? time(NULL) : last_check_epoch);
    if (!ok) {
        snprintf(last_error, sizeof last_error, "no_release_asset");
        cached_available = false;
        cached_latest[0] = 0;
        cached_url[0] = 0;
        phase = OTA_IDLE;
        if (update_available)
            *update_available = false;
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(cached_latest, sizeof cached_latest, "%.31s", found_ver);
    snprintf(cached_url, sizeof cached_url, "%.511s", found_url);
    const char *run = running_version();
    bool newer = version_cmp(found_ver, run) > 0;
    cached_available = newer;
    if (latest)
        snprintf(latest, latest_len, "%.31s", found_ver);
    if (url)
        snprintf(url, url_len, "%.511s", found_url);
    if (update_available)
        *update_available = newer;
    snprintf(last_error, sizeof last_error, newer ? "update_ready" : "up_to_date");
    phase = OTA_IDLE;
    ESP_LOGI(TAG, "Release %s (running %s) %s", found_ver, run, newer ? "is newer" : "not newer");
    return ESP_OK;
}

esp_err_t home_ota_check(bool *update_available, char *latest, size_t latest_len, char *url,
                         size_t url_len)
{
    if (phase == OTA_CHECKING || phase == OTA_UPDATING || ota_trigger_active)
        return ESP_ERR_INVALID_STATE;
    return ota_check_release(update_available, latest, latest_len, url, url_len);
}

static bool quiesce_for_ota(void)
{
    if (home_radio_on())
        home_radio_toggle();
    home_lock();
    home_runtime.maintenance = true;
    home_runtime.request_id++;
    home_unlock();
    int64_t deadline = esp_timer_get_time() + INT64_C(55000000);
    bool ready = false;
    do {
        home_lock();
        ready = home_runtime.phase == 0 && !home_runtime.source_active && !home_runtime.api_active &&
                !home_radio_on();
        home_unlock();
        if (!ready)
            vTaskDelay(pdMS_TO_TICKS(100));
    } while (!ready && esp_timer_get_time() < deadline);
    return ready;
}

static void resume_after_ota(void)
{
    home_lock();
    home_runtime.maintenance = false;
    home_runtime.request_id++;
    home_unlock();
}

static esp_err_t perform_ota(const char *url)
{
    esp_http_client_config_t http = {
        .url = url,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = QURANTRIX_HTTP_UA,
    };
    esp_https_ota_config_t ota = {.http_config = &http};
    return esp_https_ota(&ota);
}

static void ota_worker(void *arg)
{
    bool auto_started = arg != NULL;
    (void)auto_started;
    phase = OTA_UPDATING;
    ota_trigger_active = false;
    const char *url = cached_url;
    if (!url[0]) {
        snprintf(last_error, sizeof last_error, "missing_url");
        phase = OTA_FAILED;
        resume_after_ota();
        vTaskDelete(NULL);
        return;
    }
    if (!quiesce_for_ota()) {
        snprintf(last_error, sizeof last_error, "device_busy");
        phase = OTA_FAILED;
        resume_after_ota();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "OTA from %s", url);
    esp_err_t e = perform_ota(url);
    if (e == ESP_OK) {
        snprintf(last_error, sizeof last_error, "rebooting");
        ESP_LOGI(TAG, "OTA complete, rebooting");
        esp_restart();
    }
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(e));
    snprintf(last_error, sizeof last_error, "ota_failed");
    phase = OTA_FAILED;
    resume_after_ota();
    vTaskDelete(NULL);
}

esp_err_t home_ota_start(bool auto_started)
{
    if (phase == OTA_CHECKING || phase == OTA_UPDATING || ota_trigger_active)
        return ESP_ERR_INVALID_STATE;
    if (!cached_available || !cached_url[0])
        return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(ota_worker, "home_ota", 12288, (void *)(uintptr_t)auto_started, 5, NULL) !=
        pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

static void ota_button_worker(void *unused)
{
    (void)unused;
    bool available = false;
    esp_err_t e = ota_check_release(&available, NULL, 0, NULL, 0);
    if (e == ESP_OK && available) {
        if (xTaskCreate(ota_worker, "home_ota", 12288, NULL, 5, NULL) != pdPASS) {
            ota_trigger_active = false;
            snprintf(last_error, sizeof last_error, "task_create_failed");
            phase = OTA_FAILED;
        }
        vTaskDelete(NULL);
        return;
    }
    ota_trigger_active = false;
    vTaskDelete(NULL);
}

esp_err_t home_ota_update_now(void)
{
    if ((phase != OTA_IDLE && phase != OTA_FAILED) || ota_trigger_active)
        return ESP_ERR_INVALID_STATE;
    ota_trigger_active = true;
    phase = OTA_CHECKING;
    if (xTaskCreate(ota_button_worker, "ota_button", 8192, NULL, 5, NULL) != pdPASS) {
        ota_trigger_active = false;
        phase = OTA_IDLE;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void home_ota_auto_tick(void)
{
    if (phase != OTA_IDLE || ota_trigger_active)
        return;
    qurantrix_settings_t s;
    qurantrix_settings_get(&s);
    if (!s.auto_update)
        return;
    home_lock();
    bool online = home_runtime.online;
    bool setup = home_runtime.setup;
    bool maint = home_runtime.maintenance;
    home_unlock();
    if (!online || setup || maint)
        return;
    time_t now = time(NULL);
    if (now < 1704067200)
        return;
    if (last_check_epoch > 0 && now - last_check_epoch < OTA_CHECK_INTERVAL_S)
        return;
    bool avail = false;
    if (home_ota_check(&avail, NULL, 0, NULL, 0) != ESP_OK)
        return;
    if (avail)
        home_ota_start(true);
}

cJSON *home_ota_status_json(void)
{
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    const char *run = running_version();
    cJSON_AddStringToObject(j, "running", run);
    cJSON_AddBoolToObject(j, "available", cached_available);
    if (cached_latest[0])
        cJSON_AddStringToObject(j, "latest", cached_latest);
    if (cached_url[0])
        cJSON_AddStringToObject(j, "asset_url", cached_url);
    const char *state = phase == OTA_CHECKING    ? "checking"
                        : phase == OTA_UPDATING ? "updating"
                        : phase == OTA_FAILED    ? "failed"
                                                 : "idle";
    cJSON_AddStringToObject(j, "state", state);
    if (last_error[0])
        cJSON_AddStringToObject(j, "message", last_error);
    qurantrix_settings_t s;
    qurantrix_settings_get(&s);
    cJSON_AddBoolToObject(j, "auto_update", s.auto_update != 0);
    if (last_check_epoch > 0)
        cJSON_AddNumberToObject(j, "last_check", (double)last_check_epoch);
    return j;
}