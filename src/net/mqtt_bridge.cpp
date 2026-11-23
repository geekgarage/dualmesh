// SPDX-License-Identifier: MIT
#include "net/mqtt_bridge.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstdio>
#include <cstring>

#include "mbedtls/platform.h"
#include "mqtt_client.h"
#include "net/msg_store.h"
#include "storage/sd_log.h"

// ESP-IDF's indbyggede rodcertifikater (Arduinos egen esp_crt_bundle.h skygger for IDF's).
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace dm {

namespace {
constexpr uint32_t kTxGapMs = 10000;  // højst én afsendelse fra MQTT pr. 10 s
constexpr const char* kCaFile = "/sd/dualmesh/mqtt_ca.pem";

struct RxItem {
  char topic[128];
  char payload[256];
};

esp_mqtt_client_handle_t H(void* p) { return static_cast<esp_mqtt_client_handle_t>(p); }
}  // namespace

void MqttBridge::urlEncode(const char* in, char* out, size_t n) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (; *in && o + 4 < n; ++in) {
    const uint8_t c = uint8_t(*in);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~' || c == '!') {  // '!' (Meshtastic-id) er lovligt i MQTT-emner
      out[o++] = char(c);
    } else {
      out[o++] = '%';
      out[o++] = hex[c >> 4];
      out[o++] = hex[c & 15];
    }
  }
  out[o] = 0;
}

void MqttBridge::urlDecode(const char* in, char* out, size_t n) {
  auto hv = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  size_t o = 0;
  for (; *in && o + 1 < n; ++in) {
    if (*in == '%' && hv(in[1]) >= 0 && hv(in[2]) >= 0) {
      out[o++] = char(hv(in[1]) * 16 + hv(in[2]));
      in += 2;
    } else {
      out[o++] = *in;
    }
  }
  out[o] = 0;
}

void MqttBridge::jsonEscape(const char* in, char* out, size_t n) {
  size_t o = 0;
  for (; *in && o + 7 < n; ++in) {
    const uint8_t c = uint8_t(*in);
    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = char(c);
    } else if (c < 0x20) {
      o += snprintf(out + o, n - o, "\\u%04x", c);
    } else {
      out[o++] = char(c);
    }
  }
  out[o] = 0;
}

void MqttBridge::begin(TxFn tx, StatusFn status) {
  tx_fn_ = tx;
  status_fn_ = status;
  if (!rxq_) rxq_ = xQueueCreate(4, sizeof(RxItem));
}

const char* MqttBridge::stateText() const {
  if (!cfg_.enabled) return "fra";
  if (!cfg_.uri[0]) return "ingen broker";
  if (!wifi_up_) return "venter på WiFi";
  if (connected_) return "forbundet";
  return client_ ? "forbinder" : "stoppet";
}

void MqttBridge::configure(const Config& c) {
  const bool changed = c.enabled != cfg_.enabled || strcmp(c.uri, cfg_.uri) || strcmp(c.user, cfg_.user) ||
                       strcmp(c.pass, cfg_.pass) || strcmp(c.root, cfg_.root);
  cfg_ = c;
  if (cfg_.root[0]) {
    snprintf(root_, sizeof(root_), "%s", cfg_.root);
  } else {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(root_, sizeof(root_), "dualmesh/%02x%02x%02x", mac[3], mac[4], mac[5]);
  }
  size_t rl = strlen(root_);
  while (rl && root_[rl - 1] == '/') root_[--rl] = 0;  // ingen afsluttende '/'
  if (changed && client_) stop();  // startes igen i tick() med de nye værdier
}

// TLS i PSRAM: Arduino-ESP32's mbedtls allokerer kun i intern RAM (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC),
// og to TLS-buffere à 16 KB fik ikke plads ved siden af WiFi, BLE og LVGL (målt 22. nov 2026:
// mbedtls_ssl_setup -0x7F00, RSA -0x4290). Store blokke lægges nu i PSRAM, små i intern RAM.
static void* tlsCalloc(size_t n, size_t size) {
  const size_t total = n * size;
  if (total >= 2048) {
    if (void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
  }
  if (void* p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) return p;
  return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void tlsFree(void* p) { heap_caps_free(p); }

void MqttBridge::start() {
  if (client_ || !cfg_.uri[0]) return;
  static bool tls_mem = false;
  if (!tls_mem) {
    tls_mem = true;
    mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  }
  snprintf(lwt_topic_, sizeof(lwt_topic_), "%s/status", root_);
  static char client_id[32];
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(client_id, sizeof(client_id), "dualmesh-%02x%02x%02x", mac[3], mac[4], mac[5]);

  esp_mqtt_client_config_t mc = {};
  mc.uri = cfg_.uri;
  mc.client_id = client_id;
  if (cfg_.user[0]) mc.username = cfg_.user;
  if (cfg_.pass[0]) mc.password = cfg_.pass;
  mc.lwt_topic = lwt_topic_;
  mc.lwt_msg = "{\"online\":false}";
  mc.lwt_qos = 1;
  mc.lwt_retain = 1;
  mc.keepalive = 60;
  mc.buffer_size = 1024;
  mc.task_stack = 6144;
  mc.reconnect_timeout_ms = 15000;
  if (!strncmp(cfg_.uri, "mqtts://", 8) || !strncmp(cfg_.uri, "wss://", 6)) {
    // Egen CA (fx selvsigneret Mosquitto) fra SD, ellers de almindelige rodcertifikater.
    if (!ca_pem_) {
      if (FILE* f = fopen(kCaFile, "rb")) {
        fseek(f, 0, SEEK_END);
        const long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len > 0 && len < 16384) {
          ca_pem_ = static_cast<char*>(heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM));
          if (ca_pem_ && fread(ca_pem_, 1, len, f) == size_t(len)) ca_pem_[len] = 0;
        }
        fclose(f);
      }
    }
    if (ca_pem_) mc.cert_pem = ca_pem_;
    else mc.crt_bundle_attach = esp_crt_bundle_attach;
  }
  const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  esp_mqtt_client_handle_t h = esp_mqtt_client_init(&mc);
  if (!h) {
    DLOG.println("[MQTT] klienten kunne ikke oprettes (for lidt RAM?)");
    return;
  }
  esp_mqtt_client_register_event(h, MQTT_EVENT_ANY, reinterpret_cast<esp_event_handler_t>(&onEvent), this);
  if (esp_mqtt_client_start(h) != ESP_OK) {
    esp_mqtt_client_destroy(h);
    DLOG.println("[MQTT] klienten kunne ikke startes");
    return;
  }
  client_ = h;
  DLOG.printf("[MQTT] forbinder til %s som %s (rod %s, TLS %s, intern RAM %u KB fri, %u KB foer)\n",
              cfg_.uri, client_id, root_, mc.cert_pem ? "egen CA" : (mc.crt_bundle_attach ? "rod-CA'er" : "nej"),
              unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024), unsigned(before / 1024));
}

void MqttBridge::stop() {
  if (!client_) return;
  if (connected_) {  // pæn afmelding (LWT sendes kun ved uventet afbrydelse)
    char st[24] = "{\"online\":false}";
    esp_mqtt_client_publish(H(client_), lwt_topic_, st, 0, 1, 1);
  }
  esp_mqtt_client_stop(H(client_));
  esp_mqtt_client_destroy(H(client_));
  client_ = nullptr;
  connected_ = false;
  DLOG.println("[MQTT] stoppet");
}

// Kører i esp-mqtt's task: kun flag og kø, intet andet.
void MqttBridge::onEvent(void* arg, const char*, int32_t id, void* data) {
  auto* self = static_cast<MqttBridge*>(arg);
  auto* e = static_cast<esp_mqtt_event_handle_t>(data);
  switch (id) {
    case MQTT_EVENT_CONNECTED:
      self->connected_ = true;
      self->just_connected_ = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      self->connected_ = false;
      break;
    case MQTT_EVENT_ERROR:
      if (e && e->error_handle) self->last_err_ = e->error_handle->error_type;
      break;
    case MQTT_EVENT_DATA: {
      // Kun hele beskeder (et emne + payload i ét stykke); store deles op og ignoreres.
      if (!e || e->current_data_offset != 0 || e->data_len != e->total_data_len) break;
      RxItem it;
      const int tl = e->topic_len < int(sizeof(it.topic)) - 1 ? e->topic_len : int(sizeof(it.topic)) - 1;
      const int dl = e->data_len < int(sizeof(it.payload)) - 1 ? e->data_len : int(sizeof(it.payload)) - 1;
      memcpy(it.topic, e->topic, tl);
      it.topic[tl] = 0;
      memcpy(it.payload, e->data, dl);
      it.payload[dl] = 0;
      if (self->rxq_) xQueueSend(static_cast<QueueHandle_t>(self->rxq_), &it, 0);
      break;
    }
    default:
      break;
  }
}

void MqttBridge::publish(const char* topic, const char* payload, bool retain) {
  if (!client_ || !connected_) return;
  // enqueue = blokerer ikke loop-tasken; esp-mqtt sender fra sin egen task.
  if (esp_mqtt_client_enqueue(H(client_), topic, payload, 0, retain ? 1 : 0, retain ? 1 : 0, true) >= 0)
    ++n_pub_;
}

void MqttBridge::publishStatus() {
  char extra[384] = "";
  if (status_fn_) status_fn_(extra, sizeof(extra));
  char js[512];
  snprintf(js, sizeof(js), "{\"online\":true,\"uptime_s\":%lu,\"tx_allowed\":%s%s%s}",
           (unsigned long)(millis() / 1000), cfg_.tx_allowed ? "true" : "false", extra[0] ? "," : "", extra);
  publish(lwt_topic_, js, true);
  last_status_ms_ = millis();
}

void MqttBridge::tick(bool wifi_up) {
  if (wifi_up != wifi_up_) {
    wifi_up_ = wifi_up;
    if (!wifi_up && client_) stop();  // startes igen, når WiFi er tilbage
  }
  const bool want = cfg_.enabled && cfg_.uri[0] && wifi_up_;
  if (want && !client_) start();
  if (!want && client_) stop();
  if (!client_) return;

  if (just_connected_) {
    just_connected_ = false;
    char t[96];
    snprintf(t, sizeof(t), "%s/tx/#", root_);
    esp_mqtt_client_subscribe(H(client_), t, 1);
    DLOG.printf("[MQTT] forbundet; lytter paa %s\n", t);
    publishStatus();
    flushPending();
  }
  if (last_err_) {
    DLOG.printf("[MQTT] fejl (type %d) - proever igen om 15 s\n", last_err_);
    last_err_ = 0;
  }
  if (connected_ && cfg_.status_min && millis() - last_status_ms_ >= uint32_t(cfg_.status_min) * 60000u)
    publishStatus();

  RxItem it;
  while (rxq_ && xQueueReceive(static_cast<QueueHandle_t>(rxq_), &it, 0) == pdTRUE) handleTx(it.topic, it.payload);
}

void MqttBridge::result(const char* topic, bool ok, const char* err) {
  char et[96], tt[160], js[320];
  jsonEscape(err ? err : "", et, sizeof(et));
  jsonEscape(topic, tt, sizeof(tt));
  if (ok) snprintf(js, sizeof(js), "{\"ok\":true,\"topic\":\"%s\"}", tt);
  else snprintf(js, sizeof(js), "{\"ok\":false,\"topic\":\"%s\",\"error\":\"%s\"}", tt, et);
  char t[96];
  snprintf(t, sizeof(t), "%s/result", root_);
  publish(t, js, false);
  DLOG.printf("[MQTT] tx %s: %s%s%s\n", topic, ok ? "sat i koe" : "AFVIST", ok ? "" : " - ", ok ? "" : et);
}

// <rod>/tx/<net>/<chan|dm>/<mål>
void MqttBridge::handleTx(const char* topic, const char* payload) {
  const size_t rl = strlen(root_);
  if (strncmp(topic, root_, rl) != 0 || strncmp(topic + rl, "/tx/", 4) != 0) return;
  char parts[3][96] = {};
  const char* p = topic + rl + 4;
  for (int i = 0; i < 3; ++i) {
    const char* slash = i < 2 ? strchr(p, '/') : nullptr;
    const size_t len = slash ? size_t(slash - p) : strlen(p);
    if (!len || len >= sizeof(parts[i])) return result(topic, false, "emnet skal være tx/<net>/<chan|dm>/<mål>");
    memcpy(parts[i], p, len);
    if (!slash && i < 2) return result(topic, false, "emnet skal være tx/<net>/<chan|dm>/<mål>");
    p = slash ? slash + 1 : p + len;
  }
  if (strcmp(parts[0], "meshcore") && strcmp(parts[0], "meshtastic"))
    return result(topic, false, "net skal være meshcore eller meshtastic");
  const bool dm = !strcmp(parts[1], "dm");
  if (!dm && strcmp(parts[1], "chan")) return result(topic, false, "type skal være chan eller dm");
  if (!cfg_.tx_allowed) return result(topic, false, "afsendelse fra MQTT er slået fra (Indst. > MQTT)");
  if (have_tx_ && millis() - last_tx_ms_ < kTxGapMs) return result(topic, false, "for hurtigt: højst én pr. 10 s");

  // Teksten: ren tekst eller {"text":"..."} (kun det felt læses; \" og \\ afkodes).
  char text[200];
  const char* s = payload;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
  if (*s == '{') {
    const char* k = strstr(s, "\"text\"");
    k = k ? strchr(k + 6, '"') : nullptr;
    if (!k) return result(topic, false, "JSON uden \"text\"");
    size_t o = 0;
    for (++k; *k && *k != '"' && o + 1 < sizeof(text); ++k) {
      if (*k == '\\' && k[1]) {
        ++k;
        text[o++] = *k == 'n' ? ' ' : *k;
      } else {
        text[o++] = *k;
      }
    }
    text[o] = 0;
  } else {
    snprintf(text, sizeof(text), "%s", s);
    size_t n = strlen(text);
    while (n && (text[n - 1] == '\r' || text[n - 1] == '\n' || text[n - 1] == ' ')) text[--n] = 0;
  }
  for (char* c = text; *c; ++c)
    if (*c == '\n' || *c == '\r' || *c == '\t') *c = ' ';
  if (!text[0]) return result(topic, false, "tom tekst");

  char target[96];
  urlDecode(parts[2], target, sizeof(target));
  char err[96] = "";
  const bool ok = tx_fn_ && tx_fn_(parts[0], dm, target, text, err, sizeof(err));
  if (ok) {
    last_tx_ms_ = millis();
    have_tx_ = true;
  }
  result(topic, ok, ok ? nullptr : (err[0] ? err : "ikke sendt"));
}

void MqttBridge::onMessage(const Msg& m) {
  if (!cfg_.enabled || !cfg_.uri[0]) return;
  if (m.kind != Msg::ChannelIn && m.kind != Msg::DirectIn) return;
  if (!strncmp(m.conv, "zz_", 3)) return;  // selvtest
  const char* net = m.net == 2 ? "meshtastic" : "meshcore";
  const bool dm = m.kind == Msg::DirectIn;

  // Kanal: teksten er "afsender: tekst" (MeshCore: navn, Meshtastic: !nodeid).
  char from[48] = "", from_name[48] = "";
  const char* text = m.text;
  if (dm) {
    // Afsenderen: MeshCore = de 6 første byte af nøglen (samtale-id "mcd_<12 hex>"),
    // Meshtastic = "!nodeid" (samtale-id "mtd_<8 hex>"), så et svar kan sendes til samme navn
    // under <rod>/tx/meshtastic/dm/.
    if (!strncmp(m.conv, "mcd_", 4)) snprintf(from, sizeof(from), "%s", m.conv + 4);
    else if (!strncmp(m.conv, "mtd_", 4)) snprintf(from, sizeof(from), "!%s", m.conv + 4);
    else snprintf(from, sizeof(from), "%s", m.conv);
    snprintf(from_name, sizeof(from_name), "%s", m.peer);
  } else if (const char* sep = strstr(m.text, ": ")) {
    const size_t n = size_t(sep - m.text) < sizeof(from_name) - 1 ? size_t(sep - m.text) : sizeof(from_name) - 1;
    memcpy(from_name, m.text, n);
    from_name[n] = 0;
    text = sep + 2;
    if (m.net == 2 && from_name[0] == '!') snprintf(from, sizeof(from), "%s", from_name + 1);
  }

  char enc[128], topic[200];
  urlEncode(dm ? (from[0] ? from : from_name) : m.peer, enc, sizeof(enc));
  snprintf(topic, sizeof(topic), "%s/rx/%s/%s/%s", root_, net, dm ? "dm" : "chan", enc);

  char e_text[360], e_from[64], e_name[96], e_chan[64];
  jsonEscape(text, e_text, sizeof(e_text));
  jsonEscape(from, e_from, sizeof(e_from));
  jsonEscape(from_name, e_name, sizeof(e_name));
  jsonEscape(dm ? "" : m.peer, e_chan, sizeof(e_chan));
  char js[700];
  int n = snprintf(js, sizeof(js), "{\"net\":\"%s\",\"type\":\"%s\"", net, dm ? "dm" : "chan");
  if (!dm) n += snprintf(js + n, sizeof(js) - n, ",\"channel\":\"%s\"", e_chan);
  if (from[0]) n += snprintf(js + n, sizeof(js) - n, ",\"from\":\"%s\"", e_from);
  if (from_name[0]) n += snprintf(js + n, sizeof(js) - n, ",\"from_name\":\"%s\"", e_name);
  n += snprintf(js + n, sizeof(js) - n, ",\"text\":\"%s\",\"hops\":%u", e_text, unsigned(m.hops));
  if (m.ts) n += snprintf(js + n, sizeof(js) - n, ",\"ts\":%lu", (unsigned long)m.ts);
  if (m.has_sig)  // signalet ved modtagelsen (sidste hop)
    n += snprintf(js + n, sizeof(js) - n, ",\"rssi\":%d,\"snr\":%.2f", int(m.rssi), m.snr_q4 / 4.0);
  snprintf(js + n, sizeof(js) - n, "}");
  if (client_ && connected_) {
    publish(topic, js, false);
    return;
  }
  // Ikke forbundet endnu (opstart, WiFi forbinder, broker genforbindes): gem de seneste
  // kPendMax og send dem, når forbindelsen er oppe (før gik de tabt).
  if (!pend_) pend_ = static_cast<Pending*>(heap_caps_calloc(kPendMax, sizeof(Pending), MALLOC_CAP_SPIRAM));
  if (!pend_) return;
  if (n_pend_ == kPendMax) {  // fuld: den ældste går tabt
    memmove(pend_, pend_ + 1, sizeof(Pending) * (kPendMax - 1));
    --n_pend_;
    ++n_dropped_;
  }
  snprintf(pend_[n_pend_].topic, sizeof(pend_[n_pend_].topic), "%s", topic);
  snprintf(pend_[n_pend_].js, sizeof(pend_[n_pend_].js), "%s", js);
  ++n_pend_;
}

void MqttBridge::flushPending() {
  if (!n_pend_) return;
  for (int i = 0; i < n_pend_; ++i) publish(pend_[i].topic, pend_[i].js, false);
  DLOG.printf("[MQTT] %d beskeder modtaget foer forbindelsen sendt nu%s\n", n_pend_,
              n_dropped_ ? " (de aeldste gik tabt: koeen var fuld)" : "");
  n_pend_ = 0;
  n_dropped_ = 0;
}

}  // namespace dm
