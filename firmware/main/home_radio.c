#include "home_radio.h"
#include "qurantrix_product.h"
#include "qurantrix_settings.h"
#include "home_runtime.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

static const char *TAG = "home_radio";
/* Live Quran streams. The device only plays them; it does not store the audio. */
/* Plain HTTP: a TLS handshake is the first thing a weak link fails, and these relays allow it. */
static int station_idx;
static volatile bool station_bump;
static volatile uint32_t stream_epoch;
static const gpio_num_t AUDIO_PWR = GPIO_NUM_42;
static const gpio_num_t AUDIO_PA = GPIO_NUM_46;

static volatile bool radio_run;
static volatile int volume = 70;
static volatile int click_left;
static int click_phase;
static int click_rate = 44100;
static TaskHandle_t radio_task;
static TaskHandle_t click_task;
static SemaphoreHandle_t audio_mu;
static bool radio_relaxed;
static const audio_codec_data_if_t *codec_data;
static const audio_codec_ctrl_if_t *codec_ctrl;
static const audio_codec_gpio_if_t *codec_gpio;
static const audio_codec_if_t *codec_if;

bool home_radio_on(void) { return radio_run; }

static void audio_lock(void)
{
    if (!audio_mu)
        audio_mu = xSemaphoreCreateMutex();
    if (audio_mu)
        xSemaphoreTake(audio_mu, portMAX_DELAY);
}
static void audio_unlock(void)
{
    if (audio_mu)
        xSemaphoreGive(audio_mu);
}
static void radio_for_stream(bool on)
{
    if (on) {
        /* Modem sleep delivers packets in bursts. Each burst is a little gap in the music. */
        esp_wifi_set_ps(WIFI_PS_NONE);
        if (!radio_relaxed) {
            radio_relaxed = true;
            ESP_LOGI(TAG, "Wi-Fi staying awake for the stream");
        }
    } else if (radio_relaxed) {
        esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        radio_relaxed = false;
    }
}
/* Light sleep stops the speaker clock. The task then waits forever for a buffer that never
 * drains, and the music freezes as if the machine had hung. Hold the chip awake while audio
 * is actually coming out. */
static esp_pm_lock_handle_t radio_nosleep, radio_cpu, radio_apb;
static bool pm_held;
static void pm_for_audio(bool on)
{
    if (on && !pm_held) {
        if (!radio_nosleep) {
            esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "radio", &radio_nosleep);
            esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "radio_cpu", &radio_cpu);
            esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "radio_apb", &radio_apb);
        }
        if (radio_nosleep)
            esp_pm_lock_acquire(radio_nosleep);
        if (radio_cpu)
            esp_pm_lock_acquire(radio_cpu);
        if (radio_apb)
            esp_pm_lock_acquire(radio_apb);
        pm_held = true;
    } else if (!on && pm_held) {
        if (radio_apb)
            esp_pm_lock_release(radio_apb);
        if (radio_cpu)
            esp_pm_lock_release(radio_cpu);
        if (radio_nosleep)
            esp_pm_lock_release(radio_nosleep);
        pm_held = false;
    }
}
const char *home_radio_station_label(void) { return qurantrix_radio_name(station_idx); }

static void stream_cancel(void)
{
    station_bump = true;
    stream_epoch++;
}

static void station_advance_silent(int delta)
{
    int n = qurantrix_radio_count();
    if (n <= 0)
        return;
    int next = station_idx + delta;
    station_idx = ((next % n) + n) % n;
    ESP_LOGI(TAG, "Station %s", home_radio_station_label());
}

void home_radio_station_step(int delta)
{
    int n = qurantrix_radio_count();
    if (n <= 0)
        return;
    int next = station_idx + delta;
    station_idx = ((next % n) + n) % n;
    stream_cancel();
    ESP_LOGI(TAG, "Station %s", home_radio_station_label());
}

static bool stream_alive(uint32_t gen)
{
    return radio_run && !station_bump && stream_epoch == gen;
}

void home_radio_volume_step(int delta)
{
    int next = volume + delta;
    if (next < 0)
        next = 0;
    if (next > 100)
        next = 100;
    volume = next;
    ESP_LOGI(TAG, "Volume %d", next);
}

static void audio_pins(int on)
{
    gpio_config_t cfg = {.pin_bit_mask = (1ULL << AUDIO_PWR) | (1ULL << AUDIO_PA),
                         .mode = GPIO_MODE_OUTPUT};
    gpio_hold_dis(AUDIO_PWR);
    gpio_hold_dis(AUDIO_PA);
    gpio_config(&cfg);
    gpio_set_level(AUDIO_PWR, on ? 1 : 0);
    gpio_set_level(AUDIO_PA, on ? 1 : 0);
    /* The codec supply has to stay up while the chip light-sleeps, or the speaker goes silent. */
    gpio_hold_en(AUDIO_PWR);
}

static bool i2s_queue(i2s_chan_handle_t *tx, int desc, int frames, const i2s_std_config_t *std)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = desc;
    chan.dma_frame_num = frames;
    chan.auto_clear = true;
    *tx = NULL;
    /* Playback only. A receive queue would take the same internal memory again. */
    if (i2s_new_channel(&chan, tx, NULL) != ESP_OK)
        return false;
    if (i2s_channel_init_std_mode(*tx, std) == ESP_OK) {
        ESP_LOGI(TAG, "Speaker queue %d x %d", desc, frames);
        return true;
    }
    /* The buffers are claimed in init, not when the channel is created. A queue that
     * does not fit has to be deleted or the next, smaller one cannot take the port. */
    if (*tx)
        i2s_del_channel(*tx);
    *tx = NULL;
    return false;
}
static bool hardware_open(int rate, bool stream, esp_codec_dev_handle_t *dev, i2s_chan_handle_t *tx,
                          i2s_chan_handle_t *rx, i2c_master_bus_handle_t *bus)
{
    *dev = NULL;
    *tx = NULL;
    *rx = NULL;
    *bus = NULL;
    if (rate < 8000 || rate > 96000)
        rate = 44100;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_47,
        .scl_io_num = GPIO_NUM_48,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, bus) != ESP_OK)
        return false;
    /* The queue is internal memory, and Wi-Fi needs what is left. Gaps are covered by the
     * playback buffer in external memory, so this stays small. A queue that does not fit
     * is thrown away before the smaller one is tried. */
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        /* ES8311 table expects MCLK = 256 × LRCK; default config already uses 256×. */
        .gpio_cfg = {.mclk = GPIO_NUM_14,
                     .bclk = GPIO_NUM_15,
                     .ws = GPIO_NUM_38,
                     .dout = GPIO_NUM_45,
                     .din = GPIO_NUM_16},
    };
    bool opened = false;
    static const int queue[][2] = {{4, 240}, {3, 120}};
    for (size_t i = 0; i < sizeof queue / sizeof queue[0] && !opened; i++)
        opened = i2s_queue(tx, queue[i][0], queue[i][1], &std);
    (void)stream;
    *rx = NULL;
    if (!opened)
        return false;
    audio_codec_i2s_cfg_t i2s_cfg = {.port = I2S_NUM_0, .rx_handle = *rx, .tx_handle = *tx};
    codec_data = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = *bus};
    codec_ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    codec_gpio = audio_codec_new_gpio();
    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if = codec_ctrl,
        .gpio_if = codec_gpio,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = AUDIO_PA,
        .use_mclk = true,
        .hw_gain.pa_voltage = 5.0f,
        .hw_gain.codec_dac_voltage = 3.3f,
    };
    codec_if = es8311_codec_new(&codec_cfg);
    if (!codec_data || !codec_ctrl || !codec_gpio || !codec_if)
        return false;
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec_if, .data_if = codec_data};
    *dev = esp_codec_dev_new(&dev_cfg);
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16, .channel = 2, .sample_rate = (uint32_t)rate, .mclk_multiple = 256};
    if (!*dev || esp_codec_dev_open(*dev, &fs) != ESP_CODEC_DEV_OK)
        return false;
    click_rate = rate;
    esp_codec_dev_set_out_vol(*dev, volume);
    return true;
}

static void hardware_close(esp_codec_dev_handle_t dev, i2s_chan_handle_t tx, i2s_chan_handle_t rx,
                           i2c_master_bus_handle_t bus)
{
    if (dev) {
        esp_codec_dev_close(dev);
        esp_codec_dev_delete(dev);
    }
    if (codec_if)
        audio_codec_delete_codec_if(codec_if);
    if (codec_ctrl)
        audio_codec_delete_ctrl_if(codec_ctrl);
    if (codec_gpio)
        audio_codec_delete_gpio_if(codec_gpio);
    if (codec_data)
        audio_codec_delete_data_if(codec_data);
    codec_if = NULL;
    codec_ctrl = NULL;
    codec_gpio = NULL;
    codec_data = NULL;
    if (tx)
        i2s_del_channel(tx);
    if (rx)
        i2s_del_channel(rx);
    if (bus)
        i2c_del_master_bus(bus);
}

/* Three rising notes, a small cafe bell. Each note decays, then a short rest. */
static int chime_len(int rate)
{
    return 3 * (rate * 150 / 1000);
}
static int chime_at(int t, int rate)
{
    static const float notes[] = {523.25f, 659.25f, 783.99f};
    int span = rate * 150 / 1000;
    int tone = rate * 110 / 1000;
    if (span <= 0 || t < 0 || t >= 3 * span)
        return 0;
    int which = t / span;
    int at = t - which * span;
    if (at >= tone)
        return 0;
    float env = 1.0f - (float)at / (float)tone;
    float w = 2.0f * 3.1415926f * notes[which] * (float)at / (float)rate;
    return (int)(sinf(w) * 26000.0f * env * env);
}
static void mix_click(int16_t *s, int frames)
{
    int rate = click_rate > 0 ? click_rate : 44100;
    while (frames > 0 && click_left > 0) {
        int sample = chime_at(click_phase, rate);
        int l = s[0] + sample, r = s[1] + sample;
        if (l > 32767)
            l = 32767;
        if (l < -32768)
            l = -32768;
        if (r > 32767)
            r = 32767;
        if (r < -32768)
            r = -32768;
        s[0] = (int16_t)l;
        s[1] = (int16_t)r;
        s += 2;
        click_phase++;
        click_left--;
        frames--;
    }
}
/* Compressed audio only. Core 0 fills this from the socket; core 1 decodes straight to I2S. */
#define MP3_RING (128 * 1024)
#define MP3_LOW (16 * 1024)
static uint8_t *mp3_ring;
static int mp3_r, mp3_w;
static int stream_fd = -1;
static volatile bool speaker_stalled;
static volatile bool speaker_open;
static TaskHandle_t net_task;
static volatile bool net_run;
static esp_http_client_handle_t net_client;
static uint32_t net_stream_gen;

static int mp3_count(void)
{
    int w = __atomic_load_n(&mp3_w, __ATOMIC_ACQUIRE);
    int r = __atomic_load_n(&mp3_r, __ATOMIC_ACQUIRE);
    return (w - r + MP3_RING) % MP3_RING;
}
static bool mp3_push(const uint8_t *src, int n)
{
    if (!mp3_ring || n <= 0 || !radio_run || stream_epoch != net_stream_gen)
        return false;
    int waits = 0;
    while (stream_alive(net_stream_gen) && MP3_RING - 1 - mp3_count() < n) {
        if (++waits > 24)
            return false;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (MP3_RING - 1 - mp3_count() < n)
        return false;
    int w = __atomic_load_n(&mp3_w, __ATOMIC_ACQUIRE);
    int first = MP3_RING - w;
    if (first > n)
        first = n;
    memcpy(mp3_ring + w, src, (size_t)first);
    if (n > first)
        memcpy(mp3_ring, src + first, (size_t)(n - first));
    __atomic_store_n(&mp3_w, (w + n) % MP3_RING, __ATOMIC_RELEASE);
    return true;
}
static int mp3_pop(uint8_t *dst, int n)
{
    int have = mp3_count();
    if (have < n)
        n = have;
    if (n <= 0)
        return 0;
    int r = __atomic_load_n(&mp3_r, __ATOMIC_ACQUIRE);
    int first = MP3_RING - r;
    if (first > n)
        first = n;
    memcpy(dst, mp3_ring + r, (size_t)first);
    if (n > first)
        memcpy(dst + first, mp3_ring, (size_t)(n - first));
    __atomic_store_n(&mp3_r, (r + n) % MP3_RING, __ATOMIC_RELEASE);
    return n;
}
static void mp3_reset(void)
{
    __atomic_store_n(&mp3_r, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&mp3_w, 0, __ATOMIC_RELEASE);
}
static void net_stop(void)
{
    net_run = false;
    if (stream_fd >= 0) {
        shutdown(stream_fd, SHUT_RD);
        close(stream_fd);
        stream_fd = -1;
    }
    for (int i = 0; net_task && i < 80; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    net_client = NULL;
}
static void write_chime(esp_codec_dev_handle_t dev, int rate)
{
    int16_t buf[128 * 2];
    int lead = rate * 80 / 1000;
    int total = lead + chime_len(rate);
    int done = 0;
    while (done < total) {
        int n = total - done;
        if (n > 128)
            n = 128;
        for (int i = 0; i < n; i++) {
            int t = done + i - lead;
            int s = t < 0 ? 0 : chime_at(t, rate);
            buf[i * 2] = buf[i * 2 + 1] = (int16_t)s;
        }
        esp_codec_dev_write(dev, buf, n * 4);
        done += n;
    }
}
/* Icecast titles ride in the byte stream. Icy-MetaData: 1, then every icy-metaint bytes a block
 * is stripped. Bytes already past the HTTP header must count toward icy_left or titles land in
 * the decoder and the speaker hiccups about once a second. */
static int icy_metaint, icy_left;
static esp_err_t on_http(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED)
        icy_metaint = 0;
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key && evt->header_value &&
        strcasecmp(evt->header_key, "icy-metaint") == 0) {
        int v = atoi(evt->header_value);
        if (v >= 256 && v <= 65536)
            icy_metaint = v;
    }
    return ESP_OK;
}
static void icy_reset(void)
{
    icy_left = icy_metaint > 0 ? icy_metaint : 0;
}
static int net_read(esp_http_client_handle_t client, void *dst, int max);
static int icy_sock_read(esp_http_client_handle_t client, uint8_t *dst, int need)
{
    char *p = (char *)dst;
    int left = need, spins = 0;
    while (left > 0 && radio_run) {
        int n = net_read(client, p, left);
        if (n < 0)
            return -1;
        if (n == 0) {
            if (++spins > 40)
                return -1;
            vTaskDelay(pdMS_TO_TICKS(15));
            continue;
        }
        spins = 0;
        p += n;
        left -= n;
    }
    return need - left;
}
static bool icy_skip_meta(esp_http_client_handle_t client, const uint8_t **mem, int *mem_left)
{
    unsigned char blen = 0;
    if (mem && mem_left && *mem_left > 0) {
        blen = **mem;
        (*mem)++;
        (*mem_left)--;
    } else if (icy_sock_read(client, &blen, 1) != 1)
        return false;
    int meta = (int)blen * 16;
    while (meta > 0 && radio_run) {
        if (mem && mem_left && *mem_left > 0) {
            int take = *mem_left;
            if (take > meta)
                take = meta;
            *mem += take;
            *mem_left -= take;
            meta -= take;
            continue;
        }
        char dump[64];
        int take = meta > (int)sizeof dump ? (int)sizeof dump : meta;
        if (icy_sock_read(client, (uint8_t *)dump, take) != take)
            return false;
        meta -= take;
    }
    icy_left = icy_metaint;
    return true;
}
/* Turn raw Icecast bytes into clean MP3; bytes past the HTTP header count against icy_left. */
static bool icy_feed(esp_http_client_handle_t client, const uint8_t *data, int len)
{
    int off = 0;
    uint8_t chunk[1024];
    const uint8_t *mem = data;
    int mem_left = len;
    while (off < len && radio_run && (client || stream_fd >= 0)) {
        if (icy_metaint <= 0) {
            if (!mp3_push(data + off, len - off))
                return false;
            return true;
        }
        while (off < len && icy_left > 0) {
            int take = icy_left;
            if (take > len - off)
                take = len - off;
            if (take > (int)sizeof chunk)
                take = (int)sizeof chunk;
            memcpy(chunk, data + off, (size_t)take);
            if (!mp3_push(chunk, take))
                return false;
            icy_left -= take;
            off += take;
            mem = data + off;
            mem_left = len - off;
        }
        if (off >= len)
            break;
        if (!icy_skip_meta(client, &mem, &mem_left))
            return false;
        off = len - mem_left;
    }
    return true;
}
static int net_read(esp_http_client_handle_t client, void *dst, int max)
{
    if (max <= 0)
        return 0;
    if (stream_fd >= 0) {
        int n = recv(stream_fd, dst, max, 0);
        if (n > 0)
            return n;
        if (n == 0)
            return -1;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    int n = esp_http_client_read(client, dst, max);
    if (n == -ESP_ERR_HTTP_EAGAIN)
        return 0;
    return n;
}
static void net_task_fn(void *unused)
{
    (void)unused;
    esp_http_client_handle_t client = net_client;
    uint8_t raw[2048];
    while (stream_alive(net_stream_gen) && net_run && !speaker_stalled) {
        int n = net_read(client, raw, (int)sizeof raw);
        if (n < 0)
            break;
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        if (!icy_feed(client, raw, n))
            break;
    }
    net_run = false;
    net_task = NULL;
    vTaskDelete(NULL);
}
static bool net_start(esp_http_client_handle_t client, uint32_t gen)
{
    net_client = client;
    net_stream_gen = gen;
    net_run = true;
    if (net_task)
        return true;
    if (xTaskCreatePinnedToCore(net_task_fn, "radio_net", 4096, NULL, 4, &net_task, 0) != pdPASS) {
        net_run = false;
        net_task = NULL;
        return false;
    }
    return true;
}

static int header_find(const char *hdr, int len, const char *key, char *out, int cap)
{
    int klen = (int)strlen(key);
    const char *p = hdr, *end = hdr + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        int line = nl ? (int)(nl - p) : (int)(end - p);
        if (line > 0 && p[line - 1] == '\r')
            line--;
        if (line > klen + 1 && strncasecmp(p, key, (size_t)klen) == 0 && p[klen] == ':') {
            const char *v = p + klen + 1;
            int vlen = line - (klen + 1);
            while (vlen > 0 && (*v == ' ' || *v == '\t')) {
                v++;
                vlen--;
            }
            if (vlen >= cap)
                vlen = cap - 1;
            if (vlen < 0)
                vlen = 0;
            memcpy(out, v, (size_t)vlen);
            out[vlen] = 0;
            return 1;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    return 0;
}
static bool split_http(const char *url, char *host, size_t hostcap, int *port, char *path,
                       size_t pathcap)
{
    if (strncmp(url, "http://", 7) != 0)
        return false;
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *colon = NULL;
    for (const char *s = p; slash ? s < slash : *s; ++s)
        if (*s == ':')
            colon = s;
    *port = 80;
    size_t hlen = colon ? (size_t)(colon - p) : slash ? (size_t)(slash - p) : strlen(p);
    if (colon)
        *port = atoi(colon + 1);
    if (hlen == 0 || hlen >= hostcap || *port <= 0 || *port > 65535)
        return false;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if (!slash || !slash[0])
        snprintf(path, pathcap, "/");
    else
        snprintf(path, pathcap, "%s", slash);
    return true;
}
static int tcp_open(const char *host, int port)
{
    struct addrinfo hints = {.ai_socktype = SOCK_STREAM, .ai_family = AF_INET};
    char portstr[8];
    struct addrinfo *res = NULL;
    snprintf(portstr, sizeof portstr, "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
        return -1;
    int fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd >= 0) {
        int rcv = 8 * 1024, one = 1;
        struct timeval tv = {.tv_sec = 0, .tv_usec = 400000};
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    return fd;
}
/* Icecast sends the music with no content length. The HTTP client treats that as
 * chunked encoding and pulls the song apart. A plain socket keeps the bytes in order
 * and still strips the station-name labels. */
static int open_plain(const char *url, uint8_t *prefetch, int cap, int *got)
{
    char current[160];
    snprintf(current, sizeof current, "%s", url);
    *got = 0;
    for (int hop = 0; hop < 3 && radio_run; ++hop) {
        char host[96], path[128], req[384], hdr[1536], value[160];
        int port = 80;
        if (!split_http(current, host, sizeof host, &port, path, sizeof path))
            return -1;
        int fd = tcp_open(host, port);
        if (fd < 0)
            return -1;
        int reqn = snprintf(req, sizeof req,
                            "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n"
                            "Icy-MetaData: 1\r\nConnection: close\r\n\r\n",
                            path, host, QURANTRIX_HTTP_UA);
        if (reqn <= 0 || reqn >= (int)sizeof req || send(fd, req, (size_t)reqn, 0) != reqn) {
            close(fd);
            return -1;
        }
        int hlen = 0, mark = -1, spins = 0;
        while (hlen < (int)sizeof hdr - 1 && radio_run) {
            int n = recv(fd, hdr + hlen, sizeof hdr - 1 - (size_t)hlen, 0);
            if (n == 0) {
                close(fd);
                return -1;
            }
            if (n < 0) {
                if ((errno == EAGAIN || errno == EWOULDBLOCK) && ++spins <= 8)
                    continue;
                close(fd);
                return -1;
            }
            spins = 0;
            hlen += n;
            hdr[hlen] = 0;
            char *sep = strstr(hdr, "\r\n\r\n");
            if (sep) {
                mark = (int)(sep - hdr) + 4;
                break;
            }
        }
        if (mark < 0) {
            close(fd);
            return -1;
        }
        int status = 0;
        if (!strncmp(hdr, "ICY ", 4) || !strncmp(hdr, "HTTP/", 5)) {
            const char *sp = strchr(hdr, ' ');
            if (sp)
                status = atoi(sp + 1);
        }
        if ((status == 301 || status == 302 || status == 307) &&
            header_find(hdr, mark, "Location", value, sizeof value) &&
            !strncmp(value, "http://", 7)) {
            close(fd);
            snprintf(current, sizeof current, "%s", value);
            continue;
        }
        if (status != 200) {
            ESP_LOGW(TAG, "Stream %s status %d", current, status);
            close(fd);
            return -1;
        }
        icy_metaint = 0;
        if (header_find(hdr, mark, "icy-metaint", value, sizeof value)) {
            int v = atoi(value);
            if (v >= 256 && v <= 65536)
                icy_metaint = v;
        }
        icy_reset();
        if (icy_metaint > 0)
            ESP_LOGI(TAG, "Removing station labels every %d bytes", icy_metaint);
        int left = hlen - mark;
        if (left > cap)
            left = cap;
        if (left > 0) {
            memcpy(prefetch, hdr + mark, (size_t)left);
            *got = left;
        } else
            *got = 0;
        return fd;
    }
    return -1;
}

static bool play_url(const char *url, mp3dec_t *dec)
{
    icy_metaint = 0;
    stream_fd = -1;
    speaker_stalled = false;
    esp_http_client_handle_t client = NULL;
    int prefetch = 0;
    if (!mp3_ring)
        mp3_ring = heap_caps_malloc(MP3_RING, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    mp3_reset();
    if (!mp3_ring)
        return false;
    uint8_t *in = heap_caps_malloc(16 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *pcm = heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    int16_t *pcm_stereo = heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!in || !pcm || !pcm_stereo) {
        free(in);
        free(pcm);
        free(pcm_stereo);
        return false;
    }
    if (!strncmp(url, "http://", 7))
        stream_fd = open_plain(url, in, 16 * 1024, &prefetch);
    if (stream_fd < 0) {
        stream_fd = -1;
        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 1200,
            .buffer_size = 4096,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .event_handler = on_http,
            .user_agent = QURANTRIX_HTTP_UA,
        };
        client = esp_http_client_init(&cfg);
        if (!client) {
            free(in);
            free(pcm);
            free(pcm_stereo);
            return false;
        }
        esp_http_client_set_header(client, "Icy-MetaData", "1");
        esp_err_t opened_http = esp_http_client_open(client, 0);
        if (opened_http != ESP_OK) {
            ESP_LOGW(TAG, "Stream open failed: %s", esp_err_to_name(opened_http));
            esp_http_client_cleanup(client);
            free(in);
            free(pcm);
            free(pcm_stereo);
            return false;
        }
        esp_http_client_fetch_headers(client);
        icy_reset();
        if (icy_metaint > 0)
            ESP_LOGI(TAG, "Removing station labels every %d bytes", icy_metaint);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "Stream %s status %d", url, status);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            free(in);
            free(pcm);
            free(pcm_stereo);
            return false;
        }
    }
    /* Prefetch runs before the network task exists. Align the generation first,
     * or a station change rejects those bytes and the next stream never starts. */
    uint32_t gen = stream_epoch;
    net_stream_gen = gen;
    if (prefetch > 0 && !icy_feed(client, in, prefetch)) {
        free(in);
        free(pcm);
        free(pcm_stereo);
        if (stream_fd >= 0) {
            close(stream_fd);
            stream_fd = -1;
        }
        if (client) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
        return false;
    }
    if (!net_start(client, gen)) {
        free(in);
        free(pcm);
        free(pcm_stereo);
        if (stream_fd >= 0) {
            close(stream_fd);
            stream_fd = -1;
        }
        if (client) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
        return false;
    }
    int filled = 0, zeros = 0, vol_set = -1;
    esp_codec_dev_handle_t dev = NULL;
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2c_master_bus_handle_t bus = NULL;
    bool opened = false, powered = false;
    bool ok = false;
    int stream_rate = 0;
    while (stream_alive(gen) && !speaker_stalled) {
        if (!stream_alive(gen))
            break;
        if (!opened) {
            if (mp3_count() < MP3_LOW) {
                if (!net_run) {
                    if (mp3_count() == 0)
                        break;
                }
                if (++zeros > 160)
                    break;
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            zeros = 0;
        }
        while (filled < 12 * 1024 && mp3_count() > 0) {
            int n = mp3_pop(in + filled, 16 * 1024 - filled);
            if (n <= 0)
                break;
            filled += n;
        }
        int off = 0;
        bool wrote = false;
        while (stream_alive(gen) && !speaker_stalled && filled - off >= 4) {
            mp3dec_frame_info_t info;
            int samples = mp3dec_decode_frame(dec, in + off, filled - off, pcm, &info);
            if (samples <= 0 || info.frame_bytes <= 0) {
                if (filled - off > 1024) {
                    off = filled - 1024;
                    continue;
                }
                break;
            }
            off += info.frame_bytes;
            if (opened && info.hz > 0 && stream_rate > 0 && info.hz != stream_rate) {
                ESP_LOGW(TAG, "Stream rate changed %d -> %d Hz", stream_rate, info.hz);
                speaker_stalled = true;
                break;
            }
            if (!opened) {
                int rate = info.hz ? info.hz : 44100;
                audio_lock();
                audio_pins(1);
                powered = true;
                vTaskDelay(pdMS_TO_TICKS(80));
                radio_for_stream(true);
                pm_for_audio(true);
                if (!hardware_open(rate, true, &dev, &tx, &rx, &bus)) {
                    ESP_LOGE(TAG, "Speaker codec did not start");
                    hardware_close(dev, tx, rx, bus);
                    dev = NULL;
                    tx = NULL;
                    rx = NULL;
                    bus = NULL;
                    audio_pins(0);
                    powered = false;
                    audio_unlock();
                    goto done;
                }
                opened = true;
                stream_rate = rate;
                speaker_open = true;
                audio_unlock();
                ESP_LOGI(TAG, "Playing %d Hz, %d ch", rate, info.channels);
            }
            if (dev && vol_set != volume) {
                esp_codec_dev_set_out_vol(dev, volume);
                vol_set = volume;
            }
            int ch = info.channels == 1 ? 1 : 2;
            int pcm_bytes;
            if (ch == 1) {
                int n = samples > 1152 ? 1152 : samples;
                for (int i = 0; i < n; ++i)
                    pcm_stereo[i * 2] = pcm_stereo[i * 2 + 1] = pcm[i];
                pcm_bytes = n * 4;
                mix_click(pcm_stereo, n);
                if (dev && esp_codec_dev_write(dev, pcm_stereo, pcm_bytes) != 0) {
                    ESP_LOGW(TAG, "Speaker stopped taking audio");
                    speaker_stalled = true;
                    break;
                }
            } else {
                pcm_bytes = samples * 4;
                mix_click(pcm, samples);
                if (dev && esp_codec_dev_write(dev, pcm, pcm_bytes) != 0) {
                    ESP_LOGW(TAG, "Speaker stopped taking audio");
                    speaker_stalled = true;
                    break;
                }
            }
            wrote = true;
            ok = true;
        }
        if (off > 0) {
            memmove(in, in + off, (size_t)(filled - off));
            filled -= off;
        } else if (!wrote && filled >= 16 * 1024)
            filled = 0;
        if (!stream_alive(gen) || speaker_stalled)
            break;
        if (!wrote && mp3_count() == 0 && !net_run)
            break;
        if (wrote)
            zeros = 0;
        else if (opened && mp3_count() < MP3_LOW / 2)
            zeros++;
        if (zeros > 40 && opened)
            break;
        if (!wrote)
            vTaskDelay(pdMS_TO_TICKS(3));
    }
done:
    if (speaker_stalled)
        ESP_LOGW(TAG, "Speaker stopped taking audio; closing the stream");
    if (stream_epoch != gen)
        ESP_LOGI(TAG, "Stream ended for station change");
    net_stop();
    speaker_open = false;
    audio_lock();
    hardware_close(dev, tx, rx, bus);
    if (powered)
        audio_pins(0);
    audio_unlock();
    free(in);
    free(pcm);
    free(pcm_stereo);
    if (stream_fd >= 0) {
        close(stream_fd);
        stream_fd = -1;
    }
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    return ok && stream_epoch == gen;
}

static void radio_task_fn(void *unused)
{
    (void)unused;
    mp3dec_t dec;
    mp3dec_init(&dec);
    pm_for_audio(true);
    radio_for_stream(true);
    for (int wait = 0; radio_run && wait < 120 && !home_network_radio_on(); wait++)
        vTaskDelay(pdMS_TO_TICKS(500));
    if (radio_run && !home_network_radio_on())
        ESP_LOGW(TAG, "Radio still down; the stream will keep trying");
    while (radio_run) {
        bool played = false;
        int tries = qurantrix_radio_count();
        station_bump = false;
        while (radio_run && !station_bump && !played && tries-- > 0) {
            const char *url = qurantrix_radio_url(station_idx);
            const char *label = qurantrix_radio_name(station_idx);
            if (!url)
                break;
            ESP_LOGI(TAG, "Opening %s (%s)", url, label);
            if (play_url(url, &dec))
                played = true;
            else if (radio_run && !station_bump)
                station_advance_silent(1);
        }
        station_bump = false;
        if (radio_run)
            vTaskDelay(pdMS_TO_TICKS(played ? 1000 : 5000));
    }
    radio_for_stream(false);
    pm_for_audio(false);
    radio_task = NULL;
    radio_run = false;
    vTaskDelete(NULL);
}
static void click_task_fn(void *unused)
{
    (void)unused;
    /* Mixing only works once the speaker is actually playing. A radio that is still
     * connecting, or that failed to start, has to let the tap ring on its own. */
    if (speaker_open) {
        click_left = chime_len(click_rate);
        click_phase = 0;
        click_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    audio_lock();
    if (speaker_open) {
        click_left = chime_len(click_rate);
        click_phase = 0;
        audio_unlock();
        click_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    /* Light sleep stops the speaker clock, and the tick then never comes out. Hold the
     * chip awake for the whole click. */
    pm_for_audio(true);
    audio_pins(1);
    vTaskDelay(pdMS_TO_TICKS(80));
    esp_codec_dev_handle_t dev = NULL;
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2c_master_bus_handle_t bus = NULL;
    if (hardware_open(16000, false, &dev, &tx, &rx, &bus)) {
        int loud = volume < 90 ? 90 : volume;
        esp_codec_dev_set_out_vol(dev, loud);
        write_chime(dev, 16000);
        ESP_LOGI(TAG, "Prayer chime");
    } else
        ESP_LOGW(TAG, "Chime could not start the speaker");
    hardware_close(dev, tx, rx, bus);
    audio_pins(0);
    if (!radio_run)
        pm_for_audio(false);
    audio_unlock();
    click_task = NULL;
    vTaskDelete(NULL);
}
void home_radio_click(void)
{
    if (speaker_open) {
        click_left = chime_len(click_rate);
        click_phase = 0;
        return;
    }
    if (click_task)
        return;
    if (xTaskCreate(click_task_fn, "click", 12288, NULL, 6, &click_task) != pdPASS) {
        click_task = NULL;
        ESP_LOGW(TAG, "Click task was not created");
    }
}

void home_radio_toggle(void)
{
    if (radio_run) {
        radio_run = false;
        stream_cancel();
        for (int i = 0; i < 150 && radio_task; ++i)
            vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "Radio stopping");
        return;
    }
    radio_run = true;
    if (radio_task) {
        eTaskState st = eTaskGetState(radio_task);
        if (st != eDeleted && st != eInvalid) {
            ESP_LOGW(TAG, "Radio task still running");
            return;
        }
        radio_task = NULL;
    }
    /* The decoder's scratch is about 16 KB and has to live on this stack. Fast internal
     * RAM keeps up with the stream. External RAM is only the fallback. */
    static StackType_t *radio_stack;
    static StaticTask_t radio_tcb;
    static uint32_t radio_words;
    if (!radio_stack) {
        radio_stack = heap_caps_malloc(40 * 1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        radio_words = (40 * 1024) / sizeof(StackType_t);
        if (!radio_stack) {
            radio_stack = heap_caps_malloc(48 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            radio_words = (48 * 1024) / sizeof(StackType_t);
        }
    }
    if (radio_stack)
        radio_task = xTaskCreateStaticPinnedToCore(radio_task_fn, "radio", radio_words, NULL, 8,
                                                  radio_stack, &radio_tcb, 1);
    if (!radio_task) {
        radio_run = false;
        ESP_LOGE(TAG, "Radio task was not created");
    }
}
