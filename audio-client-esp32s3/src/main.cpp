#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "cJSON.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vadn_models.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "esp_wn_models.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "model_path.h"
#include "nvs_flash.h"
}

#ifndef DEVICE_ID
#define DEVICE_ID "esp32s3-room01"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID "Thanh Hai"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "thanhhai30032000"
#endif
#ifndef SERVER_URL
#define SERVER_URL "ws://103.75.183.34:8000/ws/audio"
#endif
#ifndef SAMPLE_RATE
#define SAMPLE_RATE 16000
#endif
#ifndef CHANNELS
#define CHANNELS 1
#endif
#ifndef BITS_PER_SAMPLE
#define BITS_PER_SAMPLE 16
#endif
#ifndef AUDIO_FORMAT
#define AUDIO_FORMAT "pcm_s16le"
#endif
#ifndef MIC_BCLK_PIN
#define MIC_BCLK_PIN 14
#endif
#ifndef MIC_WS_PIN
#define MIC_WS_PIN 15
#endif
#ifndef MIC_SD_PIN
#define MIC_SD_PIN 16
#endif
#ifndef OLED_ENABLE
#define OLED_ENABLE 1
#endif
#ifndef OLED_I2C_PORT
#define OLED_I2C_PORT I2C_NUM_0
#endif
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 8
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 9
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif
#ifndef OLED_DRIVER_SH1106
#define OLED_DRIVER_SH1106 1
#endif
#ifndef OLED_COLUMN_OFFSET
#define OLED_COLUMN_OFFSET 2
#endif
#ifndef ENABLE_WIFI
#define ENABLE_WIFI 1
#endif

static const char *TAG = "audio_client_vadnet";
static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr int WIFI_CONNECTED_BIT = BIT0;
static constexpr int64_t OLED_HOLD_US = 3 * 1000 * 1000;

// ── Wakeword (loopy TFLite) ───────────────────────────────────────────────
#include "wakeword.h"
#include "animation/wakeword_loopy.h"
// StreamBuffer: feed_task writes, wakeword_task reads (16 kHz int16 mono)
static constexpr int WW_RING_SIZE = 16000; // 1 s buffer
static constexpr int WW_INPUT_GAIN = 4;    // software gain boost
static StreamBufferHandle_t s_ww_stream = nullptr;

static EventGroupHandle_t g_wifi_event_group = nullptr;
static SemaphoreHandle_t g_ws_mutex = nullptr;
static SemaphoreHandle_t g_oled_mutex = nullptr;
static esp_websocket_client_handle_t g_ws = nullptr;

static volatile bool g_ws_connected = false;
static volatile bool g_recording = false;
static char g_active_session_id[96] = {0};
static uint64_t g_seq_no = 0;

static srmodel_list_t *g_models = nullptr;
static const esp_afe_sr_iface_t *g_afe_iface = nullptr;
static esp_afe_sr_data_t *g_afe_data = nullptr;
static int g_feed_chunksize = 0;
static bool g_oled_ready = false;
static int64_t g_oled_last_show_us = 0;
static bool g_is_animating = false; // Cờ báo đang chạy hoạt ảnh

static esp_err_t oled_send_cmd(uint8_t cmd) {
  uint8_t pkt[2] = {0x00, cmd};
  return i2c_master_write_to_device(OLED_I2C_PORT, OLED_I2C_ADDR, pkt,
                                    sizeof(pkt), pdMS_TO_TICKS(100));
}

static esp_err_t oled_send_data(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return ESP_OK;
  uint8_t pkt[17];
  pkt[0] = 0x40;

  size_t sent = 0;
  while (sent < len) {
    size_t chunk = std::min(static_cast<size_t>(16), len - sent);
    memcpy(&pkt[1], data + sent, chunk);
    esp_err_t err = i2c_master_write_to_device(
        OLED_I2C_PORT, OLED_I2C_ADDR, pkt, chunk + 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
      return err;
    sent += chunk;
  }
  return ESP_OK;
}

static void oled_set_pos(uint8_t page, uint8_t col) {
  uint8_t hw_col = static_cast<uint8_t>(col + OLED_COLUMN_OFFSET);
  oled_send_cmd(0xB0 + page);
  oled_send_cmd(0x00 + (hw_col & 0x0F));
  oled_send_cmd(0x10 + ((hw_col >> 4) & 0x0F));
}

static void oled_clear_locked() {
  uint8_t zero[128] = {0};
  for (uint8_t page = 0; page < 8; ++page) {
    oled_set_pos(page, 0);
    oled_send_data(zero, sizeof(zero));
  }
}



static void oled_draw_rle_frame(const uint16_t *data) {
  if (!data) return;
  
  int w = (int)data[0];
  int h = (int)data[1];
  int num_runs = (int)data[2];
  
  // Sanity check: Màn hình 128x64 chỉ có 8192 pixel. 
  // Nếu num_runs quá lớn (>4000) hoặc kích thước sai, có thể dữ liệu bị lỗi.
  if (w > 128 || h > 64 || num_runs > 4000) {
    ESP_LOGE("OLED", "Invalid RLE header: w=%d, h=%d, runs=%d", w, h, num_runs);
    return;
  }

  const uint16_t *ptr = data + 3;
  static uint8_t full_buf[1024];
  memset(full_buf, 0, 1024);

  int current_x = 0;
  int current_y = 0;
  int x_offset = (128 - w) / 2;
  int y_offset = (64 - h) / 2;
  int total_pixels_processed = 0;

  for (int i = 0; i < num_runs; i++) {
    uint16_t count = ptr[i * 2];
    uint16_t color = ptr[i * 2 + 1];

    // Yield mỗi 100 runs để reset watchdog nếu frame quá nặng
    if (i % 100 == 0) {
      vTaskDelay(0); 
    }

    for (int p = 0; p < count; p++) {
      if (total_pixels_processed >= 8192) break;
      
      if (color) {
        int abs_x = x_offset + current_x;
        int abs_y = y_offset + current_y;
        if (abs_x >= 0 && abs_x < 128 && abs_y >= 0 && abs_y < 64) {
          int page = abs_y / 8;
          int bit_in_page = abs_y % 8;
          full_buf[page * 128 + abs_x] |= (1 << bit_in_page);
        }
      }
      current_x++;
      if (current_x >= w) {
        current_x = 0;
        current_y++;
      }
      total_pixels_processed++;
    }
  }

  for (uint8_t p = 0; p < 8; p++) {
    oled_set_pos(p, 0);
    oled_send_data(&full_buf[p * 128], 128);
  }
}

static void oled_show_hi() {
  if (!g_oled_ready)
    return;
  if (xSemaphoreTake(g_oled_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    return;

  g_is_animating = true;
  ESP_LOGI(TAG, "Playing animation...");
  
  const uint16_t *frame_ptr = oled_animation_sequence;
  // Giả sử tối đa 500 frame để tránh vòng lặp vô tận nếu dữ liệu sai
  for (int f = 0; f < 500; f++) {
    oled_draw_rle_frame(frame_ptr);
    
    int num_runs = (int)frame_ptr[2];
    // Kiểm tra num_runs hợp lệ trước khi nhảy con trỏ
    if (num_runs <= 0 || num_runs > 4000) break; 
    
    frame_ptr += (3 + num_runs * 2);
    
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Nếu frame_ptr trỏ tới vùng dữ liệu không hợp lệ (ví dụ kết thúc mảng), 
    // bạn cần một cách để biết đã hết animation. 
    // Ở đây tôi giả định bạn biết trước là 28 frame, 
    // hoặc thêm một giá trị đánh dấu kết thúc (như 0xFFFF) trong mảng.
    if (f >= 27) break; // Quay lại 28 frames mặc định
  }

  g_is_animating = false;
  g_oled_last_show_us = esp_timer_get_time();
  xSemaphoreGive(g_oled_mutex);
}

static void oled_show_ready() {
  if (!g_oled_ready)
    return;
  if (xSemaphoreTake(g_oled_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
    return;
  oled_clear_locked();
  xSemaphoreGive(g_oled_mutex);
}

static void oled_check_timeout() {
  if (!g_oled_ready)
    return;
  int64_t ts = g_oled_last_show_us;
  if (ts == 0)
    return;
  if (esp_timer_get_time() - ts < OLED_HOLD_US)
    return;
  if (xSemaphoreTake(g_oled_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    return;
  if (g_oled_last_show_us != 0 &&
      (esp_timer_get_time() - g_oled_last_show_us) >= OLED_HOLD_US) {
    oled_clear_locked();
    g_oled_last_show_us = 0;
  }
  xSemaphoreGive(g_oled_mutex);
}

static void init_oled() {
#if OLED_ENABLE
  i2c_config_t cfg = {};
  cfg.mode = I2C_MODE_MASTER;
  cfg.sda_io_num = OLED_SDA_PIN;
  cfg.scl_io_num = OLED_SCL_PIN;
  cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
  cfg.master.clk_speed = 400000;

  if (i2c_param_config(OLED_I2C_PORT, &cfg) != ESP_OK) {
    ESP_LOGW(TAG, "OLED i2c_param_config failed");
    return;
  }
  if (i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
    ESP_LOGW(TAG, "OLED i2c_driver_install failed");
    return;
  }

  const uint8_t init_cmds[] = {
      0xAE,       // display off
      0xD5, 0x80, // clock divide
      0xA8, 0x3F, // multiplex 1/64
      0xD3, 0x00, // display offset
      0x40,       // start line
      0xAD, 0x8B, // DC-DC on (SH1106)
      0xA1,       // segment remap
      0xC8,       // COM scan dec
      0xDA, 0x12, // COM pins
      0x81, 0xCF, // contrast
      0xD9, 0x22, // precharge
      0xDB, 0x35, // vcomh
      0xA4,       // display all on resume
      0xA6,       // normal display
      0xAF,       // display on
  };
  for (size_t i = 0; i < sizeof(init_cmds); ++i) {
    if (oled_send_cmd(init_cmds[i]) != ESP_OK) {
      ESP_LOGW(TAG, "OLED init cmd failed at %d", static_cast<int>(i));
      return;
    }
  }

  g_oled_ready = true;
  oled_show_ready();
  ESP_LOGI(TAG, "OLED ready at addr 0x%02X", OLED_I2C_ADDR);
#endif
}

static const char *status_text() {
  if (!g_ws_connected)
    return "ws_connecting";
  if (g_recording)
    return "recording";
  return "idle";
}

static void ws_send_json(cJSON *root) {
  if (!root || !g_ws || !g_ws_connected)
    return;
  char *txt = cJSON_PrintUnformatted(root);
  if (!txt)
    return;
  if (xSemaphoreTake(g_ws_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    esp_websocket_client_send_text(g_ws, txt, static_cast<int>(strlen(txt)),
                                   pdMS_TO_TICKS(500));
    xSemaphoreGive(g_ws_mutex);
  }
  free(txt);
}

static void send_hello() {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "hello");
  cJSON_AddStringToObject(doc, "device_id", DEVICE_ID);
  cJSON_AddNumberToObject(doc, "sample_rate", SAMPLE_RATE);
  cJSON_AddNumberToObject(doc, "channels", CHANNELS);
  cJSON_AddNumberToObject(doc, "bits_per_sample", BITS_PER_SAMPLE);
  cJSON_AddStringToObject(doc, "format", AUDIO_FORMAT);
  cJSON_AddStringToObject(doc, "status", status_text());
  ws_send_json(doc);
  cJSON_Delete(doc);
}

static void send_status(const char *st) {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "status");
  cJSON_AddStringToObject(doc, "device_id", DEVICE_ID);
  cJSON_AddStringToObject(doc, "status", st);
  ws_send_json(doc);
  cJSON_Delete(doc);
}

static void send_ping() {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "ping");
  cJSON_AddStringToObject(doc, "device_id", DEVICE_ID);
  cJSON_AddStringToObject(doc, "status", status_text());
  ws_send_json(doc);
  cJSON_Delete(doc);
}

static void send_chunk_meta(const char *session_id, uint64_t seq) {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "chunk_meta");
  cJSON_AddStringToObject(doc, "device_id", DEVICE_ID);
  cJSON_AddStringToObject(doc, "session_id", session_id);
  cJSON_AddNumberToObject(doc, "seq", static_cast<double>(seq));
  ws_send_json(doc);
  cJSON_Delete(doc);
}

static void send_recording_started(const char *session_id) {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "recording_started");
  cJSON_AddStringToObject(doc, "session_id", session_id);
  cJSON_AddStringToObject(doc, "device_id", DEVICE_ID);
  cJSON_AddStringToObject(doc, "status", "recording");
  ws_send_json(doc);
  cJSON_Delete(doc);
}

static void send_recording_stopped(const char *session_id) {
  cJSON *doc = cJSON_CreateObject();
  cJSON_AddStringToObject(doc, "type", "recording_stopped");
  cJSON_AddStringToObject(doc, "session_id", session_id);
  cJSON_AddStringToObject(doc, "device_id", DEVICE_ID);
  cJSON_AddStringToObject(doc, "status", "stopping");
  ws_send_json(doc);
  cJSON_Delete(doc);
}

static void ws_send_bin(const int16_t *data, int bytes) {
  if (!data || bytes <= 0 || !g_ws || !g_ws_connected)
    return;
  if (xSemaphoreTake(g_ws_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    esp_websocket_client_send_bin(g_ws, reinterpret_cast<const char *>(data),
                                  bytes, pdMS_TO_TICKS(500));
    xSemaphoreGive(g_ws_mutex);
  }
}

static void on_ws_text(const char *payload, int len) {
  if (!payload || len <= 0)
    return;
  char *json = static_cast<char *>(malloc(len + 1));
  if (!json)
    return;
  memcpy(json, payload, len);
  json[len] = '\0';

  cJSON *doc = cJSON_Parse(json);
  free(json);
  if (!doc)
    return;

  const cJSON *type = cJSON_GetObjectItem(doc, "type");
  const cJSON *device_id = cJSON_GetObjectItem(doc, "device_id");
  const char *type_str = cJSON_IsString(type) ? type->valuestring : "";
  const char *device_str =
      cJSON_IsString(device_id) ? device_id->valuestring : "";

  if (strcmp(type_str, "ack") == 0) {
    send_status("idle");
    cJSON_Delete(doc);
    return;
  }

  if ((strcmp(type_str, "start_record") == 0 ||
       strcmp(type_str, "stop_record") == 0) &&
      strcmp(device_str, DEVICE_ID) != 0) {
    cJSON_Delete(doc);
    return;
  }

  if (strcmp(type_str, "start_record") == 0) {
    const cJSON *sid = cJSON_GetObjectItem(doc, "session_id");
    const char *sid_str = cJSON_IsString(sid) ? sid->valuestring : "";
    strncpy(g_active_session_id, sid_str, sizeof(g_active_session_id) - 1);
    g_active_session_id[sizeof(g_active_session_id) - 1] = '\0';
    g_seq_no = 0;
    g_recording = true;
    send_recording_started(g_active_session_id);
  } else if (strcmp(type_str, "stop_record") == 0) {
    const cJSON *sid = cJSON_GetObjectItem(doc, "session_id");
    if (cJSON_IsString(sid) && sid->valuestring &&
        sid->valuestring[0] != '\0') {
      strncpy(g_active_session_id, sid->valuestring,
              sizeof(g_active_session_id) - 1);
      g_active_session_id[sizeof(g_active_session_id) - 1] = '\0';
    }
    g_recording = false;
    send_recording_stopped(g_active_session_id);
    send_status("idle");
    g_active_session_id[0] = '\0';
  } else if (strcmp(type_str, "completed") == 0) {
    g_recording = false;
    g_active_session_id[0] = '\0';
    send_status("idle");
  } else if (strcmp(type_str, "error") == 0) {
    g_recording = false;
    send_status("error");
  }

  cJSON_Delete(doc);
}

#if ENABLE_WIFI
static void websocket_event_handler(void *, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
  if (base != WEBSOCKET_EVENTS)
    return;

  auto *data = static_cast<esp_websocket_event_data_t *>(event_data);
  switch (event_id) {
  case WEBSOCKET_EVENT_CONNECTED:
    g_ws_connected = true;
    ESP_LOGI(TAG, "WS connected");
    send_hello();
    break;
  case WEBSOCKET_EVENT_DISCONNECTED:
    g_ws_connected = false;
    g_recording = false;
    g_active_session_id[0] = '\0';
    ESP_LOGW(TAG, "WS disconnected");
    break;
  case WEBSOCKET_EVENT_DATA:
    if (data && data->op_code == 0x1 && data->data_ptr && data->data_len > 0) {
      on_ws_text(data->data_ptr, data->data_len);
    }
    break;
  default:
    break;
  }
}

static void wifi_event_handler(void *, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  (void)event_data;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    g_recording = false;
    esp_wifi_connect();
    ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "WiFi connected");
  }
}
#endif

#if ENABLE_WIFI
static void init_wifi() {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

  wifi_config_t wifi_config = {};
  strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), WIFI_SSID,
          sizeof(wifi_config.sta.ssid) - 1);
  strncpy(reinterpret_cast<char *>(wifi_config.sta.password), WIFI_PASS,
          sizeof(wifi_config.sta.password) - 1);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                      portMAX_DELAY);
}
#endif

static void init_i2s() {
  i2s_config_t cfg = {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = MIC_BCLK_PIN;
  pins.ws_io_num = MIC_WS_PIN;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_SD_PIN;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));
}

static bool init_afe() {
  g_models = esp_srmodel_init("model");
  if (!g_models) {
    ESP_LOGE(
        TAG,
        "No speech model in partition 'model'. Flash model partition first.");
    return false;
  }

  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG, "Heap before AFE init: internal=%u, psram=%u",
           static_cast<unsigned>(free_internal),
           static_cast<unsigned>(free_spiram));

  const bool has_psram = (free_spiram > 0);

  // Chọn mode một lần: S3 có PSRAM dùng HIGH_PERF, không có PSRAM dùng
  // LOW_COST. Tránh retry loop để không log "AFE init" nhiều lần.
  afe_mode_t mode = has_psram ? AFE_MODE_HIGH_PERF : AFE_MODE_LOW_COST;

  afe_config_t *afe_cfg = afe_config_init("M", g_models, AFE_TYPE_SR, mode);
  if (!afe_cfg) {
    // HIGH_PERF thất bại (OOM), thử lại với LOW_COST
    if (mode == AFE_MODE_HIGH_PERF) {
      ESP_LOGW(TAG,
               "afe_config_init HIGH_PERF failed, falling back to LOW_COST");
      mode = AFE_MODE_LOW_COST;
      afe_cfg = afe_config_init("M", g_models, AFE_TYPE_SR, mode);
    }
    if (!afe_cfg) {
      ESP_LOGE(TAG, "afe_config_init failed");
      return false;
    }
  }

  // Ép tối đa vào PSRAM (8MB embedded) để tránh OOM internal RAM khi chạy
  // đồng thời WakeNet (wn9s_hijason) + VADNet (vadnet1_medium).
  afe_cfg->memory_alloc_mode =
      has_psram ? AFE_MEMORY_ALLOC_MORE_PSRAM : AFE_MEMORY_ALLOC_MORE_INTERNAL;
  afe_cfg->afe_ringbuf_size =
      has_psram ? 12 : 4; // PSRAM đủ chỗ, tăng để tránh FEED full
  afe_cfg->afe_perferred_core = 1;
  afe_cfg->afe_perferred_priority = 5;
  afe_cfg->debug_init = false;

  // VADNet
  afe_cfg->vad_init = true;
  afe_cfg->vad_min_noise_ms = 1000;
  afe_cfg->vad_min_speech_ms = 128;
  afe_cfg->vad_delay_ms = 128;
  afe_cfg->vad_mode = VAD_MODE_1;

  char *vad_name = esp_srmodel_filter(g_models, ESP_VADN_PREFIX, nullptr);
  if (!vad_name) {
    ESP_LOGE(TAG, "No VADNet model found in partition 'model'.");
    afe_config_free(afe_cfg);
    return false;
  }
  afe_cfg->vad_model_name = vad_name;
  ESP_LOGI(TAG, "VADNet model: %s", vad_name);

  // WakeNet: disabled – using loopy TFLite wakeword detector instead
  afe_cfg->wakenet_init = false;
  afe_cfg->wakenet_model_name = nullptr;
  afe_cfg->wakenet_model_name_2 = nullptr;
  const char *wn_name = nullptr;
  ESP_LOGI(TAG, "WakeNet disabled (replaced by loopy TFLite wakeword)");

  const esp_afe_sr_iface_t *iface = esp_afe_handle_from_config(afe_cfg);
  if (!iface) {
    ESP_LOGE(TAG, "esp_afe_handle_from_config failed (mode=%d)",
             static_cast<int>(mode));
    afe_config_free(afe_cfg);
    return false;
  }

  esp_afe_sr_data_t *data = iface->create_from_config(afe_cfg);
  if (!data) {
    ESP_LOGE(TAG, "AFE create_from_config failed (mode=%d)",
             static_cast<int>(mode));
    afe_config_free(afe_cfg);
    return false;
  }

  int feed_chunksize = iface->get_feed_chunksize(data);
  if (feed_chunksize <= 0) {
    ESP_LOGE(TAG, "Invalid AFE feed chunk size=%d", feed_chunksize);
    iface->destroy(data);
    afe_config_free(afe_cfg);
    return false;
  }

  g_afe_iface = iface;
  g_afe_data = data;
  g_feed_chunksize = feed_chunksize;
  g_afe_iface->print_pipeline(g_afe_data);
  afe_config_free(afe_cfg);

  free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ESP_LOGI(TAG,
           "AFE init OK (mode=%d, wakeword=%s), feed_chunksize=%d, heap: "
           "internal=%u, psram=%u",
           static_cast<int>(mode), wn_name ? wn_name : "none", g_feed_chunksize,
           static_cast<unsigned>(free_internal),
           static_cast<unsigned>(free_spiram));
  return true;
}

#if ENABLE_WIFI
static void init_ws() {
  esp_websocket_client_config_t ws_cfg = {};
  ws_cfg.uri = SERVER_URL;
  ws_cfg.reconnect_timeout_ms = 2000;
  ws_cfg.network_timeout_ms = 5000;
  ws_cfg.disable_auto_reconnect = false;

  g_ws = esp_websocket_client_init(&ws_cfg);
  esp_websocket_register_events(g_ws, WEBSOCKET_EVENT_ANY,
                                websocket_event_handler, nullptr);
  esp_websocket_client_start(g_ws);
}
#endif

static void feed_task(void *) {
  if (!g_afe_iface || !g_afe_data || g_feed_chunksize <= 0) {
    ESP_LOGE(TAG, "AFE not ready in feed_task, exiting task");
    vTaskDelete(nullptr);
    return;
  }

  int feed_nch = g_afe_iface->get_feed_channel_num(g_afe_data);
  auto *i2s_raw =
      static_cast<int32_t *>(malloc(sizeof(int32_t) * g_feed_chunksize));
  auto *feed_buf = static_cast<int16_t *>(
      malloc(sizeof(int16_t) * g_feed_chunksize * feed_nch));
  if (!i2s_raw || !feed_buf) {
    ESP_LOGE(TAG, "No memory for feed task buffers");
    vTaskDelete(nullptr);
    return;
  }

  while (true) {
    size_t read_bytes = 0;
    esp_err_t err =
        i2s_read(I2S_PORT, i2s_raw, sizeof(int32_t) * g_feed_chunksize,
                 &read_bytes, pdMS_TO_TICKS(500));
    if (err != ESP_OK)
      continue;

    int frames = static_cast<int>(read_bytes / sizeof(int32_t));
    if (frames < 0)
      frames = 0;
    frames = std::min(frames, g_feed_chunksize);

    for (int i = 0; i < g_feed_chunksize; ++i) {
      int16_t s16 = 0;
      if (i < frames) {
        // I2S 24-bit in 32-bit slot: data is in bits [31:8].
        // Shift by 16 to get [31:16] into [15:0] for standard 16-bit PCM.
        int32_t s = i2s_raw[i] >> 16;
        if (s > 32767)
          s = 32767;
        if (s < -32768)
          s = -32768;
        s16 = static_cast<int16_t>(s);
      }
      feed_buf[i] = s16;
    }

    // Tap mono s16 audio into wakeword stream buffer
    if (s_ww_stream) {
      int16_t *boosted_buf = (int16_t *)malloc(frames * sizeof(int16_t));
      if (boosted_buf) {
        for (int i = 0; i < frames; ++i) {
          int32_t boosted = static_cast<int32_t>(feed_buf[i]) * WW_INPUT_GAIN;
          if (boosted > 32767)
            boosted = 32767;
          if (boosted < -32768)
            boosted = -32768;
          boosted_buf[i] = static_cast<int16_t>(boosted);
        }
        xStreamBufferSend(s_ww_stream, boosted_buf, frames * sizeof(int16_t),
                          0);
        free(boosted_buf);
      }
    }

    g_afe_iface->feed(g_afe_data, feed_buf);
  }
}

static void fetch_task(void *) {
  if (!g_afe_iface || !g_afe_data) {
    ESP_LOGE(TAG, "AFE not ready in fetch_task, exiting task");
    vTaskDelete(nullptr);
    return;
  }

  while (true) {
    afe_fetch_result_t *res = g_afe_iface->fetch(g_afe_data);
    if (!res || res->ret_value == ESP_FAIL) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // wakeword detection is handled by wakeword_task (loopy TFLite)

    bool has_speech =
        (res->vad_state != VAD_SILENCE) || (res->vad_cache_size > 0);
    if (!has_speech || !g_ws_connected || !g_recording ||
        g_active_session_id[0] == '\0')
      continue;

    if (res->vad_cache_size > 0 && res->vad_cache) {
      ws_send_bin(res->vad_cache, res->vad_cache_size);
    }

    if (res->data_size > 0 && res->data) {
      g_seq_no++;
      if (g_seq_no == 1 || (g_seq_no % 10) == 0) {
        send_chunk_meta(g_active_session_id, g_seq_no);
      }
      ws_send_bin(res->data, res->data_size);
    }
  }
}

static void ping_task(void *) {
  int64_t last_ping_us = 0;
  while (true) {
    int64_t now = esp_timer_get_time();
    if (g_ws_connected && (now - last_ping_us) >= 10 * 1000 * 1000) {
      send_ping();
      last_ping_us = now;
    }
    oled_check_timeout();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

static void wakeword_task(void *) {
  ESP_LOGI(TAG, "wakeword_task started");
  static int16_t scratch[512];
  while (true) {
    if (g_is_animating) {
      // Trong khi đang chạy animation, ta "xả" sạch buffer âm thanh
      xStreamBufferReceive(s_ww_stream, scratch, sizeof(scratch), pdMS_TO_TICKS(10));
      continue;
    }

    // Read from stream buffer (blocks until data available)
    size_t received = xStreamBufferReceive(s_ww_stream, scratch,
                                           sizeof(scratch), pdMS_TO_TICKS(500));
    if (received > 0) {
      if (wakeword_feed(scratch, received / sizeof(int16_t))) {
        ESP_LOGI(TAG, "'loopy' detected!");
        oled_show_hi(); // Hàm này sẽ set g_is_animating = true và block cho đến khi xong
        
        // Sau khi animation xong, xả sạch stream buffer một lần nữa để tránh trigger lặp
        xStreamBufferReset(s_ww_stream);
        wakeword_reset(); // Reset trạng thái model KWS
      }
    }
  }
}

extern "C" void app_main() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  g_wifi_event_group = xEventGroupCreate();
  g_ws_mutex = xSemaphoreCreateMutex();
  g_oled_mutex = xSemaphoreCreateMutex();

  ESP_LOGI(TAG, "Booting ESP32-S3 audio client with ESP-SR VADNet pipeline");
  init_i2s();
  bool afe_ok = init_afe();
  init_oled();

  // Initialise loopy wakeword detector
  s_ww_stream = xStreamBufferCreate(WW_RING_SIZE * sizeof(int16_t),
                                    160 * sizeof(int16_t));
  if (wakeword_init(0.995f) != 0) {
    ESP_LOGE(TAG, "Wakeword init failed!");
  } else {
    ESP_LOGI(TAG, "Wakeword 'loopy' ready");
  }
#if ENABLE_WIFI
  init_wifi();
  init_ws();
  send_status(afe_ok ? "idle" : "afe_failed");
#else
  ESP_LOGW(TAG,
           "WiFi/WebSocket disabled (ENABLE_WIFI=0). Running AFE + OLED only.");
#endif

  if (afe_ok) {
    xTaskCreatePinnedToCore(feed_task, "afe_feed", 6 * 1024, nullptr, 5,
                            nullptr, 0);
    xTaskCreatePinnedToCore(fetch_task, "afe_fetch", 8 * 1024, nullptr, 6,
                            nullptr, 1);
    xTaskCreatePinnedToCore(wakeword_task, "wakeword", 10 * 1024, nullptr, 4,
                            nullptr, 1);
  } else {
    ESP_LOGE(TAG, "AFE unavailable: running without audio feed/fetch tasks");
  }
  xTaskCreatePinnedToCore(ping_task, "ws_ping", 3 * 1024, nullptr, 4, nullptr,
                          1);
}
