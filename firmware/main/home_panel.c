/*
 * NOTE4 / NOTE4C adapter. Pin map and four-colour command sequence adapted from
 * LazyYoun/youn-ink-fourcolor-firmware at
 * 51812e4ab3fa80ba7a5a5a274635ca2cf3901a25, config.h and custom_lcd_display.cc.
 *
 * MIT License (upstream notices retained for the adapted portions)
 * Copyright (c) 2026 macheng2017
 * Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
 * Copyright (c) 2025 Project Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "home_panel.h"
#include "home_board.h"

#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PANEL_RAIL GPIO_NUM_6
#define PANEL_BUSY GPIO_NUM_8
#define PANEL_RST GPIO_NUM_9
#define PANEL_DC GPIO_NUM_10
#define PANEL_CS GPIO_NUM_11
#define PANEL_SCK GPIO_NUM_12
#define PANEL_MOSI GPIO_NUM_13
#define PANEL_SPI SPI3_HOST
#define BUSY_TIMEOUT_US INT64_C(120000000)
#define REFRESH_ASSERT_TIMEOUT_US INT64_C(1000000)
#define SPI_TIMEOUT_MS 1000U
#define NOTE4_BW_ROW_BYTES 50U
#define NOTE4_NATIVE_ROW_BYTES 200U
#define NOTE4_1BPP_FRAME_BYTES 15000U

typedef enum {
    PANEL_NEW,
    PANEL_OFF,
    PANEL_ACTIVE,
    PANEL_REFRESHED,
    PANEL_FAULT
} panel_state_t;

static const char *TAG = "home_panel";
static panel_state_t panel_state = PANEL_NEW;
static spi_device_handle_t panel_spi;
static TaskHandle_t owner_task;
static portMUX_TYPE owner_guard = portMUX_INITIALIZER_UNLOCKED;
/* A timed-out transaction may still belong to IDF. Static storage must remain
 * alive and unchanged after FAULT; no automatic bus teardown or reuse. */
static DMA_ATTR uint8_t transfer_buffer[HOME_PANEL_ROW_BYTES];
static spi_transaction_t transaction;

static TickType_t ticks_at_least_one(uint32_t milliseconds)
{
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    return ticks > 0 ? ticks : 1;
}

static void delay_ms(uint32_t milliseconds)
{
    /* Round upward and allow for the current partial tick, preserving minimum
     * vendor delays even when called just before the next scheduler tick. */
    TickType_t ticks = (milliseconds + portTICK_PERIOD_MS - 1U) / portTICK_PERIOD_MS;
    vTaskDelay(ticks + 1U);
}

static esp_err_t check_owner(bool claim)
{
    if (xPortInIsrContext()) {
        return ESP_ERR_INVALID_STATE;
    }
    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&owner_guard);
    if (claim && owner_task == NULL) {
        owner_task = current;
    }
    bool matches = owner_task == current;
    portEXIT_CRITICAL(&owner_guard);
    return matches ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t fault(esp_err_t error, const char *stage)
{
    panel_state = PANEL_FAULT;
    ESP_LOGE(TAG, "FAULT stage=%s error=%s; recovery decision required, no retry",
             stage, esp_err_to_name(error));
    return error;
}

#define PANEL_TRY(call, stage) do { \
    esp_err_t panel_error_ = (call); \
    if (panel_error_ != ESP_OK) { return fault(panel_error_, (stage)); } \
} while (0)

static esp_err_t set_rail(int level)
{
    PANEL_TRY(gpio_hold_dis(PANEL_RAIL), "rail hold disable");
    PANEL_TRY(gpio_set_level(PANEL_RAIL, level), "rail level");
    PANEL_TRY(gpio_hold_en(PANEL_RAIL), "rail hold enable");
    return ESP_OK;
}

/* How many times we waited for BUSY during the last picture. Milliseconds alone cannot tell why
 * one refresh takes 24.7 s and another 32.6 s: it may be longer because the panel really worked
 * longer (cold paper), or because OUR polling fell behind, since from 0.6.0 the chip may sleep
 * between samples. Time divided by the number of samples separates the two cases: a cold panel
 * gives more samples, a late wake-up gives the same number of samples, each of them just
 * longer. */
static uint32_t busy_polls, busy_polls_last;
uint32_t home_panel_busy_polls(void)
{
    return busy_polls_last;
}
static void (*idle_hook)(void);
void home_panel_set_idle_hook(void (*hook)(void))
{
    idle_hook = hook;
}
static esp_err_t wait_idle(const char *stage)
{
    int64_t start = esp_timer_get_time();
    busy_polls = 0;
    int64_t last_log = start;
    while (gpio_get_level(PANEL_BUSY) == 0) {
        int64_t now = esp_timer_get_time();
        if (now - start >= BUSY_TIMEOUT_US) {
            return fault(ESP_ERR_TIMEOUT, stage);
        }
        if (now - last_log >= INT64_C(5000000)) {
            ESP_LOGI(TAG, "BUSY stage=%s elapsed_ms=%ld", stage,
                     (long)((now - start) / 1000));
            last_log = now;
        }
        delay_ms(50);
        busy_polls++;
        if (idle_hook)
            idle_hook();
    }
    return ESP_OK;
}

static esp_err_t wait_refresh_asserted(void)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(PANEL_BUSY) != 0) {
        if (esp_timer_get_time() - start >= REFRESH_ASSERT_TIMEOUT_US) {
            return fault(ESP_ERR_TIMEOUT, "refresh did not assert BUSY");
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

static esp_err_t transfer(int data_mode, const uint8_t *bytes, size_t len)
{
    if (bytes == NULL || len == 0 || len > sizeof(transfer_buffer)) {
        return fault(ESP_ERR_INVALID_ARG, "internal transfer bounds");
    }
    memcpy(transfer_buffer, bytes, len);
    memset(&transaction, 0, sizeof(transaction));
    transaction.length = len * 8U;
    transaction.tx_buffer = transfer_buffer;
    PANEL_TRY(gpio_set_level(PANEL_DC, data_mode), "SPI DC");
    PANEL_TRY(gpio_set_level(PANEL_CS, 0), "SPI CS assert");
    PANEL_TRY(spi_device_queue_trans(panel_spi, &transaction,
                                    ticks_at_least_one(SPI_TIMEOUT_MS)), "SPI queue");
    spi_transaction_t *completed = NULL;
    PANEL_TRY(spi_device_get_trans_result(panel_spi, &completed,
                                         ticks_at_least_one(SPI_TIMEOUT_MS)), "SPI completion");
    if (completed != &transaction) {
        return fault(ESP_ERR_INVALID_STATE, "unexpected SPI transaction");
    }
    PANEL_TRY(gpio_set_level(PANEL_CS, 1), "SPI CS release");
    return ESP_OK;
}

static esp_err_t command(uint8_t byte)
{
    return transfer(0, &byte, 1);
}

static esp_err_t data(uint8_t byte)
{
    return transfer(1, &byte, 1);
}

esp_err_t home_panel_init(void)
{
    if (check_owner(true) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (panel_state == PANEL_OFF) {
        return ESP_OK;
    }
    if (panel_state != PANEL_NEW) {
        return ESP_ERR_INVALID_STATE;
    }
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << PANEL_RST) | (1ULL << PANEL_DC) |
                        (1ULL << PANEL_CS) | (1ULL << PANEL_RAIL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    PANEL_TRY(gpio_config(&output_config), "GPIO outputs");
    PANEL_TRY(set_rail(0), "initial rail off");
    PANEL_TRY(gpio_set_level(PANEL_RST, 1), "initial reset release");
    PANEL_TRY(gpio_set_level(PANEL_CS, 1), "initial chip deselect");
    PANEL_TRY(gpio_set_level(PANEL_DC, 1), "initial data mode");
    gpio_config_t busy_config = {
        .pin_bit_mask = 1ULL << PANEL_BUSY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    PANEL_TRY(gpio_config(&busy_config), "GPIO BUSY");
    spi_bus_config_t bus_config = {
        .mosi_io_num = PANEL_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PANEL_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = HOME_PANEL_ROW_BYTES,
    };
    spi_device_interface_config_t device_config = {
        .clock_speed_hz = home_board_mono() ? 20 * 1000 * 1000 : 40 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    PANEL_TRY(spi_bus_initialize(PANEL_SPI, &bus_config, SPI_DMA_CH_AUTO), "SPI bus init");
    PANEL_TRY(spi_bus_add_device(PANEL_SPI, &device_config, &panel_spi), "SPI device init");
    panel_state = PANEL_OFF;
    ESP_LOGI(TAG, "%s transport initialized: 400x300 SPI3, rail off",
             home_board_mono() ? "NOTE4" : "NOTE4C");
    return ESP_OK;
}

static void frame_to_1bpp(const uint8_t *color, uint8_t *bw)
{
    memset(bw, 0xFF, NOTE4_1BPP_FRAME_BYTES);
    for (unsigned y = 0; y < HOME_PANEL_HEIGHT; ++y) {
        for (unsigned x = 0; x < HOME_PANEL_WIDTH; ++x) {
            unsigned i = y * HOME_PANEL_ROW_BYTES + x / 4u;
            unsigned shift = 6u - (x % 4u) * 2u;
            unsigned pigment = (color[i] >> shift) & 3u;
            unsigned bit = pigment == 1u ? 1u : 0u;
            unsigned bi = y * NOTE4_BW_ROW_BYTES + x / 8u;
            unsigned bshift = 7u - (x % 8u);
            if (bit)
                bw[bi] |= (uint8_t)(1u << bshift);
            else
                bw[bi] &= (uint8_t)~(1u << bshift);
        }
    }
}

static esp_err_t note4_temperature(void)
{
    PANEL_TRY(command(0x40), "NOTE4 temp read");
    PANEL_TRY(wait_idle("NOTE4 temp"), "NOTE4 temp BUSY");
    uint8_t encoded = 244;
    PANEL_TRY(command(0xE0), "NOTE4 temp mode");
    PANEL_TRY(data(0x02), "NOTE4 temp mode data");
    PANEL_TRY(command(0xE6), "NOTE4 temp set");
    PANEL_TRY(data(encoded), "NOTE4 temp value");
    PANEL_TRY(command(0xA5), "NOTE4 temp activate");
    PANEL_TRY(wait_idle("NOTE4 temp latch"), "NOTE4 temp latch BUSY");
    delay_ms(10);
    return ESP_OK;
}

static esp_err_t note4_select_otp(void)
{
    PANEL_TRY(command(0x00), "NOTE4 PSR");
    PANEL_TRY(data(0x2F), "NOTE4 PSR data");
    PANEL_TRY(data(0x0E), "NOTE4 PSR data2");
    PANEL_TRY(command(0xE9), "NOTE4 OTP");
    PANEL_TRY(data(0x01), "NOTE4 OTP data");
    return wait_idle("NOTE4 OTP");
}

static esp_err_t note4_trigger_refresh(void)
{
    PANEL_TRY(command(0x04), "NOTE4 PON");
    PANEL_TRY(wait_idle("NOTE4 PON"), "NOTE4 PON BUSY");
    PANEL_TRY(command(0x12), "NOTE4 refresh");
    PANEL_TRY(data(0x00), "NOTE4 refresh data");
    PANEL_TRY(wait_idle("NOTE4 refresh"), "NOTE4 refresh BUSY");
    PANEL_TRY(command(0x02), "NOTE4 POF");
    PANEL_TRY(data(0x00), "NOTE4 POF data");
    return wait_idle("NOTE4 POF");
}

static esp_err_t note4_write_1bpp(const uint8_t *bw)
{
    static DMA_ATTR uint8_t native[NOTE4_NATIVE_ROW_BYTES];
    PANEL_TRY(note4_temperature(), "NOTE4 temperature");
    PANEL_TRY(command(0x10), "NOTE4 DTM");
    for (unsigned y = 0; y < HOME_PANEL_HEIGHT; ++y) {
        memset(native, 0, sizeof native);
        for (unsigned b = 0; b < NOTE4_BW_ROW_BYTES; ++b) {
            uint8_t input = bw[y * NOTE4_BW_ROW_BYTES + b];
            for (unsigned bit = 0; bit < 8u; ++bit) {
                unsigned pixel = (input >> (7u - bit)) & 1u;
                if (bit < 4u)
                    native[b * 2] |= (uint8_t)(pixel << (6u - bit * 2u));
                else
                    native[b * 2 + 1] |= (uint8_t)(pixel << (14u - bit * 2u));
            }
        }
        PANEL_TRY(transfer(1, native, NOTE4_NATIVE_ROW_BYTES), "NOTE4 row");
        if ((y & 15u) == 15u)
            vTaskDelay(1);
    }
    return note4_trigger_refresh();
}

static esp_err_t panel_show_note4(const uint8_t *color_frame)
{
    static DMA_ATTR uint8_t bw[NOTE4_1BPP_FRAME_BYTES];
    frame_to_1bpp(color_frame, bw);
    int64_t start = esp_timer_get_time();
    panel_state = PANEL_ACTIVE;
    PANEL_TRY(set_rail(1), "rail on");
    delay_ms(10);
    PANEL_TRY(gpio_set_level(PANEL_RST, 1), "reset high");
    delay_ms(10);
    PANEL_TRY(gpio_set_level(PANEL_RST, 0), "reset low");
    delay_ms(20);
    PANEL_TRY(gpio_set_level(PANEL_RST, 1), "reset release");
    delay_ms(10);
    PANEL_TRY(wait_idle("reset"), "reset BUSY");
    PANEL_TRY(note4_select_otp(), "NOTE4 OTP select");
    PANEL_TRY(note4_write_1bpp(bw), "NOTE4 frame");
    /* The OTP refresh already issued the internal power-off. A second 0x02
     * plus the four-colour deep-sleep command leaves BUSY stuck. */
    busy_polls_last = busy_polls;
    PANEL_TRY(set_rail(0), "NOTE4 rail off");
    panel_state = PANEL_OFF;
    ESP_LOGI(TAG, "NOTE4 refresh done; elapsed_ms=%ld busy_polls=%lu",
             (long)((esp_timer_get_time() - start) / 1000), (unsigned long)busy_polls_last);
    return ESP_OK;
}

esp_err_t home_panel_power_off(void)
{
    if (check_owner(false) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    if (panel_state == PANEL_OFF) {
        return ESP_OK;
    }
    if (panel_state != PANEL_REFRESHED) {
        return ESP_ERR_INVALID_STATE;
    }
    PANEL_TRY(command(0x02), "power-off command");
    PANEL_TRY(data(0x00), "power-off data");
    PANEL_TRY(wait_idle("power off"), "power-off BUSY");
    delay_ms(20);
    PANEL_TRY(command(0x07), "deep-sleep command");
    PANEL_TRY(data(0xA5), "deep-sleep data");
    PANEL_TRY(set_rail(0), "normal rail off");
    panel_state = PANEL_OFF;
    return ESP_OK;
}

esp_err_t home_panel_show(const uint8_t *frame, size_t len)
{
    if (frame == NULL || len != HOME_PANEL_FRAME_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (check_owner(false) != ESP_OK || panel_state != PANEL_OFF) {
        return ESP_ERR_INVALID_STATE;
    }
    if (home_board_mono())
        return panel_show_note4(frame);
    int64_t start = esp_timer_get_time();
    panel_state = PANEL_ACTIVE;
    PANEL_TRY(set_rail(1), "rail on");
    delay_ms(10);
    PANEL_TRY(gpio_set_level(PANEL_RST, 1), "reset high");
    delay_ms(10);
    PANEL_TRY(gpio_set_level(PANEL_RST, 0), "reset low");
    delay_ms(20);
    PANEL_TRY(gpio_set_level(PANEL_RST, 1), "reset release");
    delay_ms(10);
    PANEL_TRY(wait_idle("reset"), "reset BUSY");
    /* Four-colour source path uses OTP and returns here; deliberately omit
     * the monochrome 00 2F 2E and temperature/LUT adaptation commands. */
    PANEL_TRY(command(0xE9), "OTP command");
    PANEL_TRY(data(0x01), "OTP data");
    PANEL_TRY(command(0x10), "frame command");
    PANEL_TRY(wait_idle("frame write"), "frame BUSY");
    for (size_t row = 0; row < HOME_PANEL_HEIGHT; ++row) {
        PANEL_TRY(transfer(1, frame + row * HOME_PANEL_ROW_BYTES,
                           HOME_PANEL_ROW_BYTES), "frame row");
        if ((row % 16U) == 15U) {
            vTaskDelay(1);
        }
    }
    PANEL_TRY(command(0x04), "power-on command");
    PANEL_TRY(wait_idle("power on"), "power-on BUSY");
    delay_ms(10);
    PANEL_TRY(command(0x12), "refresh command");
    PANEL_TRY(data(0x00), "refresh data");
    /* A pulled-up or unconnected BUSY line is not success. */
    PANEL_TRY(wait_refresh_asserted(), "refresh BUSY assertion");
    delay_ms(10);
    PANEL_TRY(wait_idle("refresh"), "refresh BUSY completion");
    panel_state = PANEL_REFRESHED;
    busy_polls_last = busy_polls;
    PANEL_TRY(home_panel_power_off(), "normal power-down");
    ESP_LOGI(TAG, "Refresh cycle completed; rail off; elapsed_ms=%ld busy_polls=%lu",
             (long)((esp_timer_get_time() - start) / 1000), (unsigned long)busy_polls_last);
    return ESP_OK;
}
