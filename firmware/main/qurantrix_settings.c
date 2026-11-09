#include "qurantrix_settings.h"
#include "home_runtime.h"
#include "qurantrix_product.h"
#include "esp_log.h"
#include "nvs.h"
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "qurantrix";
static qurantrix_settings_t cached;

static void defaults(qurantrix_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = QURANTRIX_SETTINGS_MAGIC;
    s->struct_size = (uint16_t)sizeof *s;
    snprintf(s->nfc_label, sizeof s->nfc_label, "SALAH");
    s->brush = 0;
    s->radio_count = 1;
    snprintf(s->radio_name[0], sizeof s->radio_name[0], "Quran");
    snprintf(s->radio_url[0], sizeof s->radio_url[0], "https://qurango.net/radio/tarateel");
    s->dark_mode = false;
    s->unit_suffix = true;
    s->wind_unit = 0;
    s->theme_preset = 0;
    s->color_bar = 0;
    s->color_accent = 2;
    s->color_alert = 3;
    s->color_hi = 3;
    s->color_lo = 0;
    for (int i = 0; i < QURANTRIX_TOOLBAR_SLOTS; i++)
        s->toolbar_order[i] = (uint8_t)i;
    s->toolbar_visible = (uint8_t)((1u << QURANTRIX_BAR_SLOTS) - 1);
    s->bar_order[0] = QURAN_TB_WEEKDAY;
    s->bar_order[1] = QURAN_TB_DATE;
    s->bar_order[2] = QURAN_TB_CLOCK;
    s->bar_order[3] = QURAN_TB_PLACE;
    s->bar_order[4] = QURAN_TB_WIFI;
    s->bar_order[5] = QURAN_TB_BATTERY;
    s->date_format = 0;
    s->weekday_style = 0;
    s->forecast_hours = 6;
    s->forecast_style = 0;
    s->borders = QURAN_FRAME_DEFAULT;
    s->haiku_pick = 0;
    s->foot_order[0] = 0;
    s->foot_order[1] = 1;
    s->foot_order[2] = 2;
    for (int i = 0; i < QURANTRIX_METRIC_COUNT; i++)
        s->metric_order[i] = (uint8_t)i;
    s->metric_visible = 0x1f;
    s->block_visible = QURAN_BLK_TEMP | QURAN_BLK_FORECAST | QURAN_BLK_SUN | QURAN_BLK_COFFEE |
                       QURAN_BLK_ASMA | QURAN_BLK_RADIO | QURAN_BLK_HAIKU;
    s->upper_titles = true;
    s->panel_model = 0;
}

static uint8_t pigment_clamp(uint8_t v) { return v <= 3 ? v : 0; }

/* Black type on paper and yellow. Paper type on black and red. */
static uint8_t bar_text_for(uint8_t bar)
{
    return (bar == 1 || bar == 2) ? 0 : 1;
}

static void apply_palette(qurantrix_ink_t *out, uint8_t bar, uint8_t clock, uint8_t frame,
                          uint8_t hi, uint8_t lo, uint8_t accent, uint8_t alert)
{
    out->page_fill = 0x55;
    out->bar_ink = pigment_clamp(bar);
    out->bar_text = bar_text_for(out->bar_ink);
    out->clock = pigment_clamp(clock);
    out->body = 0;
    out->frame = pigment_clamp(frame);
    out->temp_main = 0;
    out->temp_hi = pigment_clamp(hi);
    out->temp_lo = pigment_clamp(lo);
    out->accent = pigment_clamp(accent);
    out->alert = pigment_clamp(alert);
}

/* Four-colour panel: swap paper and black so a light theme reads as a dark one. */
static void invert_inks(qurantrix_ink_t *out)
{
    out->page_fill = 0;
    if (out->bar_ink == 0)
        out->bar_ink = 1;
    out->bar_text = out->bar_text == 1 ? 0 : 1;
    out->body = out->body == 0 ? 1 : 0;
    out->temp_main = out->temp_main == 0 ? 1 : 0;
    if (out->temp_lo == 0)
        out->temp_lo = 1;
}

void qurantrix_inks(const qurantrix_settings_t *s, qurantrix_ink_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    switch (s->theme_preset) {
    case 1: /* sunset: red bar, gold clock */
        apply_palette(out, 3, 2, 3, 3, 0, 2, 3);
        break;
    case 2: /* ocean: ink bar, gold high */
        apply_palette(out, 0, 2, 0, 2, 0, 2, 3);
        break;
    case 3: /* custom pigments */
        apply_palette(out, s->color_bar, s->color_accent, s->color_alert,
                      s->color_hi, s->color_lo, s->color_accent, s->color_alert);
        break;
    case 4: /* marquee: gold bar, red clock */
        apply_palette(out, 2, 3, 3, 3, 0, 2, 3);
        break;
    case 5: /* chorus: red clock and red marks */
        apply_palette(out, 0, 3, 3, 3, 0, 3, 3);
        break;
    case 6: /* brass: gold bar, black type */
        apply_palette(out, 2, 0, 2, 3, 0, 2, 3);
        break;
    case 7: /* spotlight: red bar, gold frames */
        apply_palette(out, 3, 2, 2, 2, 0, 2, 3);
        break;
    default: /* classic: ink bar, gold clock, red high */
        apply_palette(out, 0, 2, 0, 3, 0, 2, 3);
        break;
    }
    if (s->dark_mode)
        invert_inks(out);
}

static void sanitize_order(uint8_t *order, int n)
{
    uint8_t seen[8] = {0};
    uint8_t next[8];
    int w = 0;
    for (int i = 0; i < n; i++) {
        uint8_t id = order[i];
        if (id >= n || seen[id])
            continue;
        seen[id] = 1;
        next[w++] = id;
    }
    for (uint8_t id = 0; id < n; id++)
        if (!seen[id])
            next[w++] = id;
    memcpy(order, next, (size_t)n);
}

bool qurantrix_block_on(const qurantrix_settings_t *s, enum qurantrix_block b)
{
    return s && (s->block_visible & (uint8_t)b);
}

bool qurantrix_metric_on(const qurantrix_settings_t *s, enum qurantrix_metric m)
{
    return s && m < QURANTRIX_METRIC_COUNT && (s->metric_visible & (1u << m));
}

double qurantrix_wind_display(double ms, uint8_t unit)
{
    if (!isfinite(ms))
        return ms;
    if (unit == 1)
        return ms * 3.6;
    if (unit == 2)
        return ms * 2.23694;
    return ms;
}

const char *qurantrix_wind_unit_suffix(uint8_t unit)
{
    if (unit == 1)
        return "km/h";
    if (unit == 2)
        return "mph";
    return "m/s";
}

void qurantrix_upper_box_title(char *s, bool upper)
{
    if (!s || !upper)
        return;
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

void qurantrix_metric_title(char *out, size_t len, enum qurantrix_metric m,
                           const qurantrix_settings_t *s)
{
    const char *name = "—";
    const char *unit = "";
    char shown[16];
    switch (m) {
    case QURAN_MET_WIND:
        name = "Wind";
        unit = qurantrix_wind_unit_suffix(s->wind_unit);
        break;
    case QURAN_MET_RAIN:
        name = "Rain";
        unit = "mm";
        break;
    case QURAN_MET_PRESS:
        name = "Press";
        unit = "hPa";
        break;
    case QURAN_MET_AIR:
        name = "Air";
        unit = "AQI";
        break;
    case QURAN_MET_UV:
        name = "UV";
        unit = "index";
        break;
    }
    snprintf(shown, sizeof shown, "%s", name);
    qurantrix_upper_box_title(shown, s->upper_titles);
    if (s->unit_suffix && unit[0])
        snprintf(out, len, "%s (%s)", shown, unit);
    else
        snprintf(out, len, "%s", shown);
}

static void trim(char *s)
{
    if (!s)
        return;
    char *p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
}

bool qurantrix_settings_import_playlist(const char *text, qurantrix_settings_t *out)
{
    if (!text || !out)
        return false;
    qurantrix_settings_t s;
    memset(&s, 0, sizeof s);
    char line[192];
    const char *cur = text;
    char pending_name[24];
    int n = 0;
    pending_name[0] = 0;
    while (n < QURANTRIX_RADIO_MAX && *cur) {
        size_t i = 0;
        while (*cur && *cur != '\n' && *cur != '\r' && i < sizeof line - 1)
            line[i++] = *cur++;
        line[i] = 0;
        while (*cur == '\n' || *cur == '\r')
            cur++;
        trim(line);
        if (!line[0] || line[0] == '#') {
            if (!strncmp(line, "#EXTINF:", 8)) {
                const char *comma = strchr(line, ',');
                if (comma && comma[1])
                    snprintf(pending_name, sizeof pending_name, "%.23s", comma + 1);
            }
            continue;
        }
        if (!strncmp(line, "http://", 7) || !strncmp(line, "https://", 8)) {
            snprintf(s.radio_url[n], sizeof s.radio_url[n], "%.127s", line);
            if (pending_name[0]) {
                snprintf(s.radio_name[n], sizeof s.radio_name[n], "%.23s", pending_name);
                pending_name[0] = 0;
            } else
                snprintf(s.radio_name[n], sizeof s.radio_name[n], "Station %d", n + 1);
            n++;
        }
    }
    if (n == 0)
        return false;
    s.radio_count = (uint8_t)n;
    out->radio_count = s.radio_count;
    for (int i = 0; i < n; i++) {
        strcpy(out->radio_name[i], s.radio_name[i]);
        strcpy(out->radio_url[i], s.radio_url[i]);
    }
    return true;
}

static void migrate_legacy_zectrix(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition("home_nvs", "zectrix", NVS_READONLY, &h) != ESP_OK)
        return;
    typedef struct {
        char nfc_label[40];
        uint8_t theme;
        uint8_t brush;
        uint8_t radio_count;
        char radio_name[8][24];
        char radio_url[8][128];
    } legacy_t;
    legacy_t leg;
    size_t len = sizeof leg;
    if (nvs_get_blob(h, "desk", &leg, &len) == ESP_OK) {
        snprintf(cached.nfc_label, sizeof cached.nfc_label, "%.39s", leg.nfc_label);
        cached.brush = leg.brush;
        cached.radio_count = leg.radio_count > QURANTRIX_RADIO_MAX ? QURANTRIX_RADIO_MAX : leg.radio_count;
        for (int i = 0; i < cached.radio_count; i++) {
            snprintf(cached.radio_name[i], sizeof cached.radio_name[i], "%.23s", leg.radio_name[i]);
            snprintf(cached.radio_url[i], sizeof cached.radio_url[i], "%.127s", leg.radio_url[i]);
        }
        ESP_LOGI(TAG, "Imported legacy desk settings");
    }
    nvs_close(h);
}

/* Old desks stored four top-bar slots. Put the weekday and the date in front of the clock. */
static void migrate_bar(qurantrix_settings_t *s)
{
    uint8_t next[QURANTRIX_BAR_SLOTS];
    int w = 0;
    bool saw_clock = false;
    memset(next, 0, sizeof next);
    for (int i = 0; i < QURANTRIX_TOOLBAR_SLOTS && w < QURANTRIX_BAR_SLOTS; i++) {
        uint8_t id = s->toolbar_order[i];
        if (id > QURAN_TB_BATTERY)
            continue;
        if (id == QURAN_TB_CLOCK && !saw_clock) {
            if (w < QURANTRIX_BAR_SLOTS)
                next[w++] = QURAN_TB_WEEKDAY;
            if (w < QURANTRIX_BAR_SLOTS)
                next[w++] = QURAN_TB_DATE;
            saw_clock = true;
        }
        if (w < QURANTRIX_BAR_SLOTS)
            next[w++] = id;
    }
    if (!saw_clock) {
        if (w < QURANTRIX_BAR_SLOTS)
            next[w++] = QURAN_TB_WEEKDAY;
        if (w < QURANTRIX_BAR_SLOTS)
            next[w++] = QURAN_TB_DATE;
    }
    memcpy(s->bar_order, next, sizeof next);
    sanitize_order(s->bar_order, QURANTRIX_BAR_SLOTS);
    s->toolbar_visible |= (uint8_t)((1u << QURAN_TB_DATE) | (1u << QURAN_TB_WEEKDAY));
    s->date_format = 0;
    s->weekday_style = 0;
}

void qurantrix_settings_init(void)
{
    defaults(&cached);
    size_t loaded = 0;
    nvs_handle_t h;
    if (nvs_open_from_partition("home_nvs", "qurantrix", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof cached;
        esp_err_t ge = nvs_get_blob(h, "desk", &cached, &len);
        if (ge != ESP_OK || cached.magic != QURANTRIX_SETTINGS_MAGIC)
            migrate_legacy_zectrix();
        else {
            loaded = len;
            /* Older blobs stored a zero in the byte that is now upper_titles. */
            if (len <= offsetof(qurantrix_settings_t, upper_titles))
                cached.upper_titles = true;
            /* bar_order sits past the previous blob. A shorter save still has the old four slots. */
            if (len < offsetof(qurantrix_settings_t, bar_order) + QURANTRIX_BAR_SLOTS)
                migrate_bar(&cached);
            ESP_LOGI(TAG, "Settings loaded");
        }
        nvs_close(h);
    } else
        migrate_legacy_zectrix();
    if (cached.magic != QURANTRIX_SETTINGS_MAGIC)
        defaults(&cached);
    sanitize_order(cached.bar_order, QURANTRIX_BAR_SLOTS);
    if (cached.date_format > 5)
        cached.date_format = 0;
    if (cached.weekday_style > 1)
        cached.weekday_style = 0;
    if (cached.forecast_hours != 3 && cached.forecast_hours != 6 && cached.forecast_hours != 9 &&
        cached.forecast_hours != 12)
        cached.forecast_hours = 6;
    if (cached.forecast_style > 1)
        cached.forecast_style = 0;
    if (cached.haiku_pick > 30)
        cached.haiku_pick = 0;
    /* The haiku switch sits in the old visible byte. A shorter save never turned it off. */
    if (loaded && loaded < offsetof(qurantrix_settings_t, foot_order) + QURANTRIX_FOOT_SLOTS)
        cached.block_visible |= QURAN_BLK_HAIKU;
    sanitize_order(cached.foot_order, QURANTRIX_FOOT_SLOTS);
    if (loaded && loaded < offsetof(qurantrix_settings_t, panel_model) + 1)
        cached.panel_model = 0;
    if (cached.panel_model > 2)
        cached.panel_model = 0;
    if (strcmp(cached.nfc_label, "COFFEE  ·  TAP") == 0 || strcmp(cached.nfc_label, "COFFEE") == 0)
        snprintf(cached.nfc_label, sizeof cached.nfc_label, "SALAH");
    for (int i = 0; i < cached.radio_count && i < QURANTRIX_RADIO_MAX; i++) {
        if (strcmp(cached.radio_name[i], "LOW FI") == 0)
            snprintf(cached.radio_name[i], sizeof cached.radio_name[i], "LoFi");
        if (strcmp(cached.radio_name[i], "Cairo") == 0)
            snprintf(cached.radio_name[i], sizeof cached.radio_name[i], "Quran");
        if (strcmp(cached.radio_url[i], "http://stream.radiojar.com/8s5u5tpdtwzuv") == 0 ||
            strcmp(cached.radio_url[i], "https://stream.radiojar.com/8s5u5tpdtwzuv") == 0)
            snprintf(cached.radio_url[i], sizeof cached.radio_url[i],
                     "https://qurango.net/radio/tarateel");
        if (strstr(cached.radio_url[i], "qurango.net/radio/mix") != NULL)
            snprintf(cached.radio_url[i], sizeof cached.radio_url[i],
                     "https://qurango.net/radio/tarateel");
    }
    {
        int w = 0;
        for (int i = 0; i < cached.radio_count && i < QURANTRIX_RADIO_MAX; i++) {
            bool dup = false;
            for (int j = 0; j < w; j++)
                if (!strcasecmp(cached.radio_url[i], cached.radio_url[j])) {
                    dup = true;
                    break;
                }
            if (dup)
                continue;
            if (w != i) {
                char name[24], url[128];
                snprintf(name, sizeof name, "%s", cached.radio_name[i]);
                snprintf(url, sizeof url, "%s", cached.radio_url[i]);
                snprintf(cached.radio_name[w], sizeof cached.radio_name[w], "%s", name);
                snprintf(cached.radio_url[w], sizeof cached.radio_url[w], "%s", url);
            }
            w++;
        }
        if (!w) {
            snprintf(cached.radio_name[0], sizeof cached.radio_name[0], "Quran");
            snprintf(cached.radio_url[0], sizeof cached.radio_url[0],
                     "https://qurango.net/radio/tarateel");
            w = 1;
        }
        cached.radio_count = (uint8_t)w;
    }
    qurantrix_settings_apply_config();
}

esp_err_t qurantrix_settings_save(const qurantrix_settings_t *in)
{
    if (!in)
        return ESP_ERR_INVALID_ARG;
    qurantrix_settings_t copy = *in;
    copy.magic = QURANTRIX_SETTINGS_MAGIC;
    copy.struct_size = (uint16_t)sizeof copy;
    if (copy.radio_count > QURANTRIX_RADIO_MAX)
        copy.radio_count = QURANTRIX_RADIO_MAX;
    if (!copy.nfc_label[0])
        snprintf(copy.nfc_label, sizeof copy.nfc_label, "SALAH");
    sanitize_order(copy.toolbar_order, QURANTRIX_TOOLBAR_SLOTS);
    sanitize_order(copy.bar_order, QURANTRIX_BAR_SLOTS);
    sanitize_order(copy.metric_order, QURANTRIX_METRIC_COUNT);
    sanitize_order(copy.foot_order, QURANTRIX_FOOT_SLOTS);
    copy.date_format = copy.date_format > 5 ? 0 : copy.date_format;
    copy.weekday_style = copy.weekday_style > 1 ? 0 : copy.weekday_style;
    if (copy.forecast_hours != 3 && copy.forecast_hours != 6 && copy.forecast_hours != 9 &&
        copy.forecast_hours != 12)
        copy.forecast_hours = 6;
    if (copy.forecast_style > 1)
        copy.forecast_style = 0;
    if (copy.haiku_pick > 30)
        copy.haiku_pick = 0;
    copy.borders = (uint8_t)(copy.borders & 0x7f);
    copy.wind_unit = copy.wind_unit > 4 ? 0 : copy.wind_unit;
    copy.theme_preset = copy.theme_preset > 7 ? 0 : copy.theme_preset;
    copy.brush = copy.brush > 2 ? 0 : copy.brush;
    nvs_handle_t h;
    esp_err_t e = nvs_open_from_partition("home_nvs", "qurantrix", NVS_READWRITE, &h);
    if (e != ESP_OK)
        return e;
    e = nvs_set_blob(h, "desk", &copy, sizeof copy);
    if (e == ESP_OK)
        e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) {
        cached = copy;
        qurantrix_settings_apply_config();
        home_lock();
        home_runtime.dirty = true;
        home_runtime.force_show = true;
        home_runtime.request_id++;
        home_unlock();
    }
    return e;
}

void qurantrix_settings_get(qurantrix_settings_t *out)
{
    if (out)
        *out = cached;
}

void qurantrix_settings_apply_config(void)
{
    home_lock();
    home_config_t *c = &home_runtime.config;
    c->texture = 1;
    c->brush = cached.brush <= 2 ? cached.brush : 0;
    c->intensity = 2;
    home_unlock();
}

const char *qurantrix_nfc_label(void) { return cached.nfc_label[0] ? cached.nfc_label : "SALAH"; }
int qurantrix_radio_count(void) { return cached.radio_count > 0 ? cached.radio_count : 2; }
const char *qurantrix_radio_name(int i)
{
    if (i < 0 || i >= qurantrix_radio_count())
        return "RADIO";
    return cached.radio_name[i][0] ? cached.radio_name[i] : "RADIO";
}
const char *qurantrix_radio_url(int i)
{
    if (i < 0 || i >= qurantrix_radio_count())
        return NULL;
    return cached.radio_url[i][0] ? cached.radio_url[i] : NULL;
}

static cJSON *u8_array(const uint8_t *a, int n)
{
    cJSON *j = cJSON_CreateArray();
    if (!j)
        return NULL;
    for (int i = 0; i < n; i++)
        cJSON_AddItemToArray(j, cJSON_CreateNumber(a[i]));
    return j;
}

cJSON *qurantrix_settings_json(void)
{
    qurantrix_settings_t s;
    qurantrix_settings_get(&s);
    cJSON *j = cJSON_CreateObject();
    if (!j)
        return NULL;
    cJSON_AddStringToObject(j, "product", QURANTRIX_PRODUCT_NAME);
    cJSON_AddStringToObject(j, "version", QURANTRIX_VERSION);
    cJSON_AddStringToObject(j, "nfc_label", s.nfc_label);
    cJSON_AddNumberToObject(j, "brush", s.brush);
    cJSON_AddBoolToObject(j, "dark_mode", s.dark_mode);
    cJSON_AddBoolToObject(j, "unit_suffix", s.unit_suffix);
    cJSON_AddBoolToObject(j, "upper_titles", s.upper_titles);
    cJSON_AddNumberToObject(j, "wind_unit", s.wind_unit);
    cJSON_AddNumberToObject(j, "salah_method", s.wind_unit > 4 ? 0 : s.wind_unit);
    cJSON_AddNumberToObject(j, "asr_madhab", s.forecast_style > 1 ? 0 : s.forecast_style);
    cJSON_AddNumberToObject(j, "theme_preset", s.theme_preset);
    cJSON_AddNumberToObject(j, "color_bar", s.color_bar);
    cJSON_AddNumberToObject(j, "color_accent", s.color_accent);
    cJSON_AddNumberToObject(j, "color_alert", s.color_alert);
    cJSON_AddNumberToObject(j, "color_hi", s.color_hi);
    cJSON_AddNumberToObject(j, "color_lo", s.color_lo);
    cJSON_AddNumberToObject(j, "toolbar_visible", s.toolbar_visible);
    cJSON_AddNumberToObject(j, "date_format", s.date_format);
    cJSON_AddNumberToObject(j, "weekday_style", s.weekday_style);
    cJSON_AddNumberToObject(j, "forecast_hours", s.forecast_hours);
    cJSON_AddNumberToObject(j, "forecast_style", s.forecast_style);
    cJSON_AddNumberToObject(j, "borders", s.borders);
    cJSON_AddNumberToObject(j, "haiku_pick", s.haiku_pick);
    cJSON_AddItemToObject(j, "foot_order", u8_array(s.foot_order, QURANTRIX_FOOT_SLOTS));
    cJSON_AddNumberToObject(j, "panel_model", s.panel_model);
    cJSON_AddNumberToObject(j, "metric_visible", s.metric_visible);
    cJSON_AddNumberToObject(j, "block_visible", s.block_visible);
    cJSON_AddItemToObject(j, "toolbar_order", u8_array(s.bar_order, QURANTRIX_BAR_SLOTS));
    cJSON_AddItemToObject(j, "metric_order", u8_array(s.metric_order, QURANTRIX_METRIC_COUNT));
    cJSON *radios = cJSON_AddArrayToObject(j, "radios");
    if (radios) {
        for (int i = 0; i < s.radio_count && i < QURANTRIX_RADIO_MAX; i++) {
            cJSON *r = cJSON_CreateObject();
            if (!r)
                continue;
            cJSON_AddStringToObject(r, "name", s.radio_name[i]);
            cJSON_AddStringToObject(r, "url", s.radio_url[i]);
            cJSON_AddItemToArray(radios, r);
        }
    }
    return j;
}

static void read_u8_array(cJSON *arr, uint8_t *out, int n)
{
    if (!cJSON_IsArray(arr))
        return;
    int i = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr)
    {
        if (i >= n)
            break;
        if (cJSON_IsNumber(item))
            out[i++] = (uint8_t)item->valueint;
    }
}

bool qurantrix_settings_decode(const char *json, size_t len, qurantrix_settings_t *out, char *why,
                               size_t why_len)
{
    if (!json || !out)
        return false;
    cJSON *j = cJSON_ParseWithLength(json, len);
    if (!j) {
        if (why && why_len)
            snprintf(why, why_len, "Invalid JSON");
        return false;
    }
    qurantrix_settings_t s;
    qurantrix_settings_get(&s);
    cJSON *nfc = cJSON_GetObjectItem(j, "nfc_label");
    if (cJSON_IsString(nfc) && nfc->valuestring)
        snprintf(s.nfc_label, sizeof s.nfc_label, "%.39s", nfc->valuestring);
    cJSON *v;
    if ((v = cJSON_GetObjectItem(j, "brush")) && cJSON_IsNumber(v))
        s.brush = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "dark_mode")))
        s.dark_mode = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "unit_suffix")))
        s.unit_suffix = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "upper_titles")))
        s.upper_titles = cJSON_IsTrue(v);
    if ((v = cJSON_GetObjectItem(j, "wind_unit")) && cJSON_IsNumber(v))
        s.wind_unit = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "salah_method")) && cJSON_IsNumber(v))
        s.wind_unit = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "forecast_style")) && cJSON_IsNumber(v))
        s.forecast_style = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "asr_madhab")) && cJSON_IsNumber(v))
        s.forecast_style = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "theme_preset")) && cJSON_IsNumber(v))
        s.theme_preset = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "color_bar")) && cJSON_IsNumber(v))
        s.color_bar = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "color_accent")) && cJSON_IsNumber(v))
        s.color_accent = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "color_alert")) && cJSON_IsNumber(v))
        s.color_alert = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "color_hi")) && cJSON_IsNumber(v))
        s.color_hi = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "color_lo")) && cJSON_IsNumber(v))
        s.color_lo = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "toolbar_visible")) && cJSON_IsNumber(v))
        s.toolbar_visible = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "date_format")) && cJSON_IsNumber(v))
        s.date_format = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "weekday_style")) && cJSON_IsNumber(v))
        s.weekday_style = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "forecast_hours")) && cJSON_IsNumber(v))
        s.forecast_hours = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "borders")) && cJSON_IsNumber(v))
        s.borders = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "haiku_pick")) && cJSON_IsNumber(v))
        s.haiku_pick = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "metric_visible")) && cJSON_IsNumber(v))
        s.metric_visible = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(j, "block_visible")) && cJSON_IsNumber(v))
        s.block_visible = (uint8_t)v->valueint;
    read_u8_array(cJSON_GetObjectItem(j, "toolbar_order"), s.bar_order, QURANTRIX_BAR_SLOTS);
    read_u8_array(cJSON_GetObjectItem(j, "metric_order"), s.metric_order, QURANTRIX_METRIC_COUNT);
    read_u8_array(cJSON_GetObjectItem(j, "foot_order"), s.foot_order, QURANTRIX_FOOT_SLOTS);
    if ((v = cJSON_GetObjectItem(j, "panel_model")) && cJSON_IsNumber(v))
        s.panel_model = (uint8_t)v->valueint;
    else if ((v = cJSON_GetObjectItem(j, "panel")) && cJSON_IsString(v) && v->valuestring) {
        if (!strcasecmp(v->valuestring, "note4"))
            s.panel_model = 2;
        else if (!strcasecmp(v->valuestring, "note4c"))
            s.panel_model = 1;
        else
            s.panel_model = 0;
    }
    if (s.panel_model > 2)
        s.panel_model = 0;
    cJSON *pl = cJSON_GetObjectItem(j, "playlist");
    if (cJSON_IsString(pl) && pl->valuestring)
        qurantrix_settings_import_playlist(pl->valuestring, &s);
    else {
        cJSON *radios = cJSON_GetObjectItem(j, "radios");
        if (cJSON_IsArray(radios)) {
            int n = 0;
            cJSON *item;
            cJSON_ArrayForEach(item, radios)
            {
                if (n >= QURANTRIX_RADIO_MAX)
                    break;
                cJSON *name = cJSON_GetObjectItem(item, "name");
                cJSON *url = cJSON_GetObjectItem(item, "url");
                if (!cJSON_IsString(url) || !url->valuestring)
                    continue;
                snprintf(s.radio_url[n], sizeof s.radio_url[n], "%.127s", url->valuestring);
                if (cJSON_IsString(name) && name->valuestring)
                    snprintf(s.radio_name[n], sizeof s.radio_name[n], "%.23s", name->valuestring);
                else
                    snprintf(s.radio_name[n], sizeof s.radio_name[n], "Station %d", n + 1);
                n++;
            }
            s.radio_count = (uint8_t)n;
        }
    }
    cJSON_Delete(j);
    *out = s;
    return true;
}
