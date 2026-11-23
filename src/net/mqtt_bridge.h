// SPDX-License-Identifier: MIT
// DualMesh – MQTT-bro (fase 6, se docs/spec.md "MQTT-bro").
//
// Bygger på ESP-IDF's MQTT-klient (esp-mqtt, Apache-2.0), der kører i sin egen task.
// Emner (<rod> = mqtt_root, standard "dualmesh/<6 hex af MAC>"):
//   <rod>/rx/<net>/chan/<kanal>      modtaget kanalbesked (JSON)
//   <rod>/rx/<net>/dm/<afsender>     modtaget DM til os (JSON)
//   <rod>/tx/<net>/chan/<kanal>      send tekst på en kanal (tekst eller {"text":"..."})
//   <rod>/tx/<net>/dm/<modtager>     send DM (MeshCore: navn eller hex-præfiks af nøglen)
//   <rod>/result                     svar på hver tx (ok/fejl)
//   <rod>/status                     online, batteri, GPS-fix, net (retained; LWT = offline)
// <net> = meshcore | meshtastic. Kanalnavne URL-kodes (#test -> %23test).
// Kun beskeder, vi selv har dekrypteret, sendes ud. Afsendelse fra MQTT er som standard
// FRA, har en egen grænse (højst én pr. 10 s) og tæller med i schedulerens 10 %-kvote.
// Broen kører kun, når WiFi er forbundet. Bruges kun fra loop()-tasken (undtagen
// event-handleren, der kun lægger indgående tx i en kø).
#pragma once
#include <cstddef>
#include <cstdint>

namespace dm {

struct Msg;

class MqttBridge {
 public:
  struct Config {
    bool enabled = false;
    char uri[128] = "";    // mqtt://host:1883 eller mqtts://host:8883
    char user[64] = "";
    char pass[128] = "";
    char root[64] = "";    // tom = dualmesh/<id>
    bool tx_allowed = false;
    uint16_t status_min = 5;  // 0 = kun ved forbindelse
  };
  // Afsendelse fra MQTT: net = "meshcore"/"meshtastic", dm = DM (ellers kanal), target =
  // kanalnavn/modtager (afkodet). Returnerer true = sat i kø; ellers fejltekst i err.
  using TxFn = bool (*)(const char* net, bool dm, const char* target, const char* text, char* err,
                        size_t err_n);
  // Statusfelter (JSON uden { }), fx "\"batt_v\":4.10,\"gps_fix\":true".
  using StatusFn = void (*)(char* out, size_t n);

  void begin(TxFn tx, StatusFn status);
  void configure(const Config& c);  // starter/stopper klienten efter behov
  void tick(bool wifi_up);
  void onMessage(const Msg& m);     // fra MsgStore (alle nye beskeder; kun indgående sendes)

  bool connected() const { return connected_; }
  const char* stateText() const;
  const char* root() const { return root_; }
  uint32_t published() const { return n_pub_; }

  // Hjælpere (også brugt af test): URL-kodning af et emne-led og JSON-escape.
  static void urlEncode(const char* in, char* out, size_t n);
  static void urlDecode(const char* in, char* out, size_t n);
  static void jsonEscape(const char* in, char* out, size_t n);

 private:
  static void onEvent(void* arg, const char* base, int32_t id, void* data);
  void start();
  void stop();
  void publish(const char* topic, const char* payload, bool retain);
  void publishStatus();
  void handleTx(const char* topic, const char* payload);
  void result(const char* topic, bool ok, const char* err);
  void flushPending();

  // Indgående beskeder, mens broen ikke er forbundet (PSRAM, oprettes ved første behov).
  static constexpr int kPendMax = 16;
  struct Pending {
    char topic[200];
    char js[700];
  };
  Pending* pend_ = nullptr;
  int n_pend_ = 0;
  uint32_t n_dropped_ = 0;

  Config cfg_;
  TxFn tx_fn_ = nullptr;
  StatusFn status_fn_ = nullptr;
  void* client_ = nullptr;   // esp_mqtt_client_handle_t
  void* rxq_ = nullptr;      // QueueHandle_t med indgående tx
  char* ca_pem_ = nullptr;   // /sd/dualmesh/mqtt_ca.pem (PSRAM), hvis den findes
  char root_[64] = "";
  char lwt_topic_[80] = "";
  volatile bool connected_ = false;
  volatile bool just_connected_ = false;
  volatile int last_err_ = 0;
  bool wifi_up_ = false;
  uint32_t last_status_ms_ = 0, last_tx_ms_ = 0;
  uint32_t n_pub_ = 0;
  bool have_tx_ = false;
};

}  // namespace dm
