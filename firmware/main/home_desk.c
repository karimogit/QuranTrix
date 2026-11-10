#include "home_desk.h"
#include "home_radio.h"
#include "home_places.h"
#include "home_runtime.h"
#include "home_salah.h"
#include "qurantrix_settings.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "home_desk";
static const gpio_num_t NFC_PWR = GPIO_NUM_21;
static const gpio_num_t NFC_FD = GPIO_NUM_7;

static int prayers;
static int prayer_day;
static int64_t arm_at;
static int64_t last_count_us;
static TaskHandle_t nfc_task;
static volatile uint32_t nfc_edges;
static volatile int64_t nfc_last_us;
static uint32_t nfc_seen;

/* The iPhone's field is often gone again before a task can look at the pin. The interrupt
 * latches that moment. A low level also wakes the chip out of light sleep. */
static void nfc_isr(void *arg)
{
    (void)arg;
    gpio_intr_disable(NFC_FD);
    nfc_last_us = esp_timer_get_time();
    nfc_edges++;
    BaseType_t woken = pdFALSE;
    if (nfc_task)
        vTaskNotifyGiveFromISR(nfc_task, &woken);
    if (woken)
        portYIELD_FROM_ISR();
}

static void prayer_save(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition("home_nvs", "desk", NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, "p", (uint8_t)(prayers & 0x1f));
    nvs_set_i32(h, "d", prayer_day);
    nvs_commit(h);
    nvs_close(h);
}

static void nfc_rail(int on)
{
    const gpio_num_t pin = GPIO_NUM_42; /* I2C pull-ups sit on the codec supply */
    gpio_hold_dis(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, on ? 1 : 0);
    gpio_hold_en(pin);
}
static void nfc_power_cycle(void)
{
    gpio_hold_dis(NFC_PWR);
    gpio_set_level(NFC_PWR, 0);
    esp_rom_delay_us(5000);
    gpio_set_level(NFC_PWR, 1);
    gpio_hold_en(NFC_PWR);
    esp_rom_delay_us(1000);
}
static void nfc_prepare_tag(void)
{
    nfc_rail(1);
    esp_rom_delay_us(1000);
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_47,
        .scl_io_num = GPIO_NUM_48,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGW(TAG, "NFC bus was not opened");
        nfc_rail(0);
        return;
    }
    nfc_power_cycle();
    esp_err_t probe = i2c_master_probe(bus, 0x55, 100);
    if (probe != ESP_OK) {
        ESP_LOGW(TAG, "NFC chip did not answer: %s", esp_err_to_name(probe));
        i2c_del_master_bus(bus);
        nfc_rail(0);
        return;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x55, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t dev = NULL;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) {
        i2c_del_master_bus(bus);
        nfc_rail(0);
        return;
    }
    uint8_t block = 1;
    uint8_t got[16];
    esp_err_t rd = i2c_master_transmit(dev, &block, 1, 100);
    if (rd == ESP_OK) {
        esp_rom_delay_us(10000);
        rd = i2c_master_receive(dev, got, sizeof got, 100);
    }
    bool rewrite = false;
    if (rd == ESP_OK) {
        static const uint8_t coffee[] = {'C', 'o', 'f', 'f', 'e', 'e'};
        for (size_t i = 0; i + sizeof coffee <= sizeof got; i++)
            if (!memcmp(got + i, coffee, sizeof coffee))
                rewrite = true;
    }
    if (rd != ESP_OK) {
        ESP_LOGW(TAG, "NFC memory was not read: %s", esp_err_to_name(rd));
    } else if (got[0] == 0x03 && !rewrite) {
        ESP_LOGI(TAG, "NFC tag already has a message");
    } else {
        /* Without a real NFC message the iPhone flashes its field and leaves. A short text
         * record is what makes it stay long enough to count. */
        ESP_LOGI(TAG, "NFC block was %02X %02X %02X %02X, writing a message", got[0], got[1], got[2],
                 got[3]);
        nfc_power_cycle();
        static const uint8_t msg[] = {1,    0x03, 0x0C, 0xD1, 0x01, 0x08, 'T', 0x02,
                                      'e', 'n',  'S',  'a',  'l',  'a',  'h', 0xFE};
        esp_err_t wr = i2c_master_transmit(dev, msg, sizeof msg, 100);
        esp_rom_delay_us(8000);
        ESP_LOGI(TAG, "NFC text record %s", wr == ESP_OK ? "written" : "was not written");
    }
    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);
    nfc_rail(0);
}
static int current_prayer(int *today)
{
    time_t now = time(NULL);
    /* These two structs are about 3 KB. They stay off the NFC task stack. */
    static home_config_t cfg;
    static qurantrix_settings_t js;
    bool clock_ok;
    struct tm local;
    int32_t off = 0;
    home_salah_day_t day;
    home_lock();
    cfg = home_runtime.config;
    /* Same gate as the dashboard: a 2024+ clock and a saved place. */
    clock_ok = now >= 1704067200 && cfg.location_ready;
    home_unlock();
    if (!clock_ok) {
        ESP_LOGW(TAG, "NFC prayer skipped, clock %lld place %s", (long long)now,
                 cfg.location_ready ? "saved" : "missing");
        return -1;
    }
    if (!home_tz_localtime(cfg.timezone, (int64_t)now, &local)) {
        ESP_LOGW(TAG, "NFC prayer skipped, time zone %s", cfg.timezone);
        return -1;
    }
    if (!home_tz_offset_at(cfg.timezone, (int64_t)now, &off, NULL)) {
        ESP_LOGW(TAG, "NFC prayer skipped, no offset for %s", cfg.timezone);
        return -1;
    }
    qurantrix_settings_get(&js);
    if (!home_salah_compute(cfg.latitude, cfg.longitude, off, &local,
                            js.wind_unit > 4 ? 0 : js.wind_unit,
                            js.forecast_style > 1 ? 0 : js.forecast_style, cfg.location_ready,
                            &day) ||
        !day.valid || day.current < 0 || day.current >= SALAH_COUNT) {
        ESP_LOGW(TAG, "NFC prayer skipped, times not valid");
        return -1;
    }
    if (today)
        *today = (local.tm_year + 1900) * 1000 + local.tm_yday;
    return day.current;
}
static void nfc_count(void)
{
    int64_t mono = esp_timer_get_time();
    int which;
    int today = 0;
    if (mono < arm_at || mono - last_count_us < INT64_C(1500000))
        return;
    last_count_us = mono;
    ESP_LOGI(TAG, "NFC field");
    home_radio_click();
    which = current_prayer(&today);
    if (which < 0)
        return;
    /* Stamp the day with the mark. Save before unlock so a midnight clear cannot
     * be overwritten by a snapshot taken earlier. */
    home_lock();
    if (prayer_day != today) {
        prayers = 0;
        prayer_day = today;
    }
    prayers |= 1 << which;
    prayer_save();
    if (!home_runtime.setup) {
        home_runtime.dirty = true;
        home_runtime.request_id++;
        home_runtime.force_show = true;
    }
    home_unlock();
    ESP_LOGI(TAG, "Prayer %s marked", home_salah_name(which));
    home_loop_poke();
}
static void nfc_task_fn(void *unused)
{
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        uint32_t edges = nfc_edges;
        bool low = gpio_get_level(NFC_FD) == 0;
        if (edges != nfc_seen || low) {
            nfc_seen = edges;
            nfc_last_us = esp_timer_get_time();
            nfc_count();
            int guard = 0;
            while (gpio_get_level(NFC_FD) == 0 && guard++ < 40)
                vTaskDelay(pdMS_TO_TICKS(25));
        }
        if (gpio_get_level(NFC_FD) != 0)
            gpio_intr_enable(NFC_FD);
    }
}
void home_desk_init(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition("home_nvs", "desk", NVS_READONLY, &h) == ESP_OK) {
        uint8_t n = 0;
        int32_t day = 0;
        nvs_get_u8(h, "p", &n);
        nvs_get_i32(h, "d", &day);
        nvs_close(h);
        prayers = n & 0x1f;
        prayer_day = day;
    }
    gpio_config_t pwr = {.pin_bit_mask = 1ULL << NFC_PWR, .mode = GPIO_MODE_OUTPUT};
    gpio_config_t fd = {.pin_bit_mask = 1ULL << NFC_FD,
                        .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_ENABLE,
                        .pull_down_en = GPIO_PULLDOWN_DISABLE};
    if (gpio_config(&pwr) == ESP_OK) {
        gpio_hold_dis(NFC_PWR);
        gpio_set_level(NFC_PWR, 1);
        gpio_hold_en(NFC_PWR);
    }
    fd.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&fd);
    gpio_sleep_sel_dis(NFC_PWR);
    gpio_sleep_sel_dis(NFC_FD);
    arm_at = esp_timer_get_time() + INT64_C(2000000);
    nfc_prepare_tag();
    if (xTaskCreate(nfc_task_fn, "nfc", 8192, NULL, 10, &nfc_task) != pdPASS)
        ESP_LOGW(TAG, "NFC watcher was not created");
    esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "NFC interrupt service unavailable: %s", esp_err_to_name(isr));
    else if (gpio_isr_handler_add(NFC_FD, nfc_isr, NULL) != ESP_OK)
        ESP_LOGW(TAG, "NFC field interrupt was not attached");
    /* Same wake mode as the buttons. Setting an edge here would cancel the wake. */
    if (gpio_wakeup_enable(NFC_FD, GPIO_INTR_LOW_LEVEL) != ESP_OK)
        ESP_LOGW(TAG, "NFC field will not wake the chip");
    esp_sleep_enable_gpio_wakeup();
    if (gpio_get_level(NFC_FD) != 0)
        gpio_intr_enable(NFC_FD);
    ESP_LOGI(TAG, "Prayers %d, NFC field pin %s", prayers,
             gpio_get_level(NFC_FD) == 0 ? "low" : "resting");
}

void home_desk_listen(void)
{
    gpio_wakeup_enable(NFC_FD, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    if (gpio_get_level(NFC_FD) != 0)
        gpio_intr_enable(NFC_FD);
}
int home_desk_prayers(void) { return prayers; }
bool home_desk_button_blocked(void)
{
    if (gpio_get_level(NFC_FD) == 0)
        return true;
    int64_t last = nfc_last_us;
    return last != 0 && esp_timer_get_time() - last < INT64_C(1500000);
}
bool home_desk_radio(void) { return home_radio_on(); }
void home_desk_radio_toggle(void) { home_radio_toggle(); }

#define DESK_REFRESH_US INT64_C(4) * 3600 * 1000000
static int64_t desk_last_paint_us;

bool home_desk_night_quiet(const struct tm *local)
{
    return local && local->tm_hour >= 0 && local->tm_hour < 6;
}

bool home_desk_periodic_due(int64_t mono)
{
    if (desk_last_paint_us == 0)
        return true;
    return mono - desk_last_paint_us >= DESK_REFRESH_US;
}

void home_desk_painted(int64_t mono) { desk_last_paint_us = mono; }

int home_desk_poll(void)
{
    int event = 0;
    time_t now = time(NULL);
    bool clock_ok = false;
    char zone[49];
    home_lock();
    clock_ok = home_runtime.time_valid && now >= 1704067200;
    snprintf(zone, sizeof zone, "%s", home_runtime.config.timezone);
    home_unlock();
    if (clock_ok) {
        struct tm local;
        if (home_tz_localtime(zone, (int64_t)now, &local)) {
            int day = (local.tm_year + 1900) * 1000 + local.tm_yday;
            int previous;
            bool rolled = false;
            bool changed = false;
            home_lock();
            previous = prayers;
            if (prayer_day != day) {
                if (prayer_day != 0)
                    prayers = 0;
                prayer_day = day;
                rolled = true;
            }
            if (rolled)
                prayer_save();
            changed = prayers != previous;
            home_unlock();
            if (changed) {
                event = 2;
                ESP_LOGI(TAG, "Prayers cleared for a new day");
            }
        }
    }
    return event;
}
