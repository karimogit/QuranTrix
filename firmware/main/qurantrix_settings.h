#ifndef QURANTRIX_SETTINGS_H
#define QURANTRIX_SETTINGS_H
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QURANTRIX_RADIO_MAX 8
#define QURANTRIX_NFC_LABEL_MAX 40
#define QURANTRIX_METRIC_COUNT 5
#define QURANTRIX_TOOLBAR_SLOTS 4 /* legacy order kept in place so old desk blobs still line up */
#define QURANTRIX_BAR_SLOTS 6
#define QURANTRIX_SETTINGS_MAGIC 0x5155524e /* QURN */

enum qurantrix_metric {
    QURAN_MET_WIND = 0,
    QURAN_MET_RAIN = 1,
    QURAN_MET_PRESS = 2,
    QURAN_MET_AIR = 3,
    QURAN_MET_UV = 4,
};

enum qurantrix_toolbar {
    QURAN_TB_PLACE = 0,
    QURAN_TB_CLOCK = 1,
    QURAN_TB_WIFI = 2,
    QURAN_TB_BATTERY = 3,
    QURAN_TB_DATE = 4,
    QURAN_TB_WEEKDAY = 5,
};

enum qurantrix_block {
    QURAN_BLK_TEMP = 1,     /* prayer times */
    QURAN_BLK_FORECAST = 2, /* Ramadan countdown */
    QURAN_BLK_COFFEE = 4,   /* dua of the day */
    QURAN_BLK_RADIO = 8,
    QURAN_BLK_HAIKU = 16, /* next holiday */
    QURAN_BLK_SUN = 32, /* sunrise and sunset, or suhoor and iftar in Ramadan */
    QURAN_BLK_ASMA = 64, /* one of the ninety-nine names */
};

/* Bottom row. 0 brew, 1 radio, 2 haiku. */
#define QURANTRIX_FOOT_SLOTS 3

/* Outlines on the dashboard. Degrees and the sky line start off. The rest match the paper today. */
enum qurantrix_frame {
    QURAN_FRAME_FORECAST = 1,
    QURAN_FRAME_DEGREES = 2, /* sunrise and sunset, or suhoor and iftar */
    QURAN_FRAME_SKY = 4, /* ninety-nine names */
    QURAN_FRAME_TILES = 8,
    QURAN_FRAME_BREW = 16, /* dua of the day */
    QURAN_FRAME_RADIO = 32,
    QURAN_FRAME_HAIKU = 64,
};
#define QURAN_FRAME_DEFAULT                                                                                \
    (QURAN_FRAME_FORECAST | QURAN_FRAME_DEGREES | QURAN_FRAME_TILES | QURAN_FRAME_BREW | QURAN_FRAME_SKY |   \
     QURAN_FRAME_RADIO | QURAN_FRAME_HAIKU)

/* NOTE4C pigments: 0 black, 1 paper, 2 yellow, 3 red */
typedef struct {
    uint32_t magic;
    uint16_t struct_size;
    char nfc_label[QURANTRIX_NFC_LABEL_MAX];
    uint8_t brush;
    uint8_t radio_count;
    char radio_name[QURANTRIX_RADIO_MAX][24];
    char radio_url[QURANTRIX_RADIO_MAX][128];
    bool dark_mode;
    bool unit_suffix;
    uint8_t wind_unit; /* prayer method: 0 MWL, 1 ISNA, 2 Egypt, 3 Makkah, 4 Karachi */
    uint8_t theme_preset; /* 0 classic, 1 sunset, 2 ocean, 3 custom, 4 marquee, 5 chorus, 6 brass, 7 spotlight */
    uint8_t color_bar;
    uint8_t color_accent;
    uint8_t color_alert;
    uint8_t color_hi;
    uint8_t color_lo;
    uint8_t toolbar_order[QURANTRIX_TOOLBAR_SLOTS];
    uint8_t toolbar_visible; /* bit i = slot id i */
    uint8_t metric_order[QURANTRIX_METRIC_COUNT];
    uint8_t metric_visible; /* bit = metric id */
    uint8_t block_visible; /* qurantrix_block bits */
    bool upper_titles; /* box titles on the paper */
    uint8_t reserved_pad[4];
    uint8_t bar_order[QURANTRIX_BAR_SLOTS]; /* weekday, date, time, place, wifi, battery */
    uint8_t date_format; /* 0: 9 Nov, 1: Nov 9, 2: 09/11, 3: 11/09, 4: 2026-11-09, 5: Hijri */
    uint8_t weekday_style; /* 0 short, 1 long */
    uint8_t reserved_grow[4];
    uint8_t forecast_hours; /* 3, 6, 9, or 12 hours ahead on the paper */
    uint8_t forecast_style; /* Asr school: 0 Shafi, 1 Hanafi */
    uint8_t reserved_forecast[2];
    uint8_t borders; /* qurantrix_frame bits */
    uint8_t reserved_borders[3];
    uint8_t haiku_pick; /* 0 a new one each day, 1–30 a chosen poem */
    uint8_t reserved_haiku[3];
    uint8_t foot_order[QURANTRIX_FOOT_SLOTS]; /* brew, radio, haiku */
    uint8_t adhan; /* 1: play the call and refresh the paper at each prayer. Off until the phone turns it on. */
    uint8_t panel_model; /* HOME_PANEL_* : auto, NOTE4C, or NOTE4 monochrome */
} qurantrix_settings_t;

typedef struct {
    uint8_t page_fill;
    uint8_t bar_ink;
    uint8_t bar_text;
    uint8_t clock;
    uint8_t body;
    uint8_t frame;
    uint8_t temp_main;
    uint8_t temp_hi;
    uint8_t temp_lo;
    uint8_t accent;
    uint8_t alert;
} qurantrix_ink_t;

void qurantrix_settings_init(void);
void qurantrix_settings_get(qurantrix_settings_t *out);
esp_err_t qurantrix_settings_save(const qurantrix_settings_t *in);
void qurantrix_settings_apply_config(void);
void qurantrix_inks(const qurantrix_settings_t *s, qurantrix_ink_t *out);
bool qurantrix_block_on(const qurantrix_settings_t *s, enum qurantrix_block b);
bool qurantrix_metric_on(const qurantrix_settings_t *s, enum qurantrix_metric m);
const char *qurantrix_nfc_label(void);
int qurantrix_radio_count(void);
const char *qurantrix_radio_name(int i);
const char *qurantrix_radio_url(int i);
double qurantrix_wind_display(double ms, uint8_t unit);
const char *qurantrix_wind_unit_suffix(uint8_t unit);
void qurantrix_metric_title(char *out, size_t len, enum qurantrix_metric m,
                           const qurantrix_settings_t *s);
void qurantrix_upper_box_title(char *s, bool upper);
bool qurantrix_settings_import_playlist(const char *text, qurantrix_settings_t *out);
cJSON *qurantrix_settings_json(void);
bool qurantrix_settings_decode(const char *json, size_t len, qurantrix_settings_t *out, char *why,
                             size_t why_len);

#endif
