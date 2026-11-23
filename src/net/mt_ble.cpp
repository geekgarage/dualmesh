// SPDX-License-Identifier: MIT
#include "net/mt_ble.h"
#include "net/ble_serial.h"  // bleName

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "storage/sd_log.h"

namespace dm {

namespace {
// Meshtastics GATT-tjeneste (docs/meshtastic_protokol.md afsnit 12).
constexpr const char* kService = "6ba1b218-15a8-461f-9fa8-5dcae273eafd";
constexpr const char* kToRadio = "f75c76d2-129e-4dad-a1dd-7866124401e7";
constexpr const char* kFromRadio = "2c55e69e-4993-11ed-b878-0242ac120002";
constexpr const char* kFromNum = "ed9da18c-a800-4f66-a670-aa7547e34453";
constexpr uint32_t kAdvRestartMs = 1000;
constexpr int kQueue = 6;

struct Frame {
  uint16_t len;
  uint8_t buf[MtBle::kMaxFrame];
};

BLEServer* g_server = nullptr;
BLEService* g_service = nullptr;
BLECharacteristic* g_from_num = nullptr;
QueueHandle_t g_rxq = nullptr;
volatile bool g_connected = false;
volatile uint32_t g_adv_restart = 0;
volatile int g_disc_reason = 0;
uint32_t g_conn_ms = 0;
uint16_t g_conn_id = 0;
uint32_t g_pin = 123456;
uint32_t g_num = 0;
MtBle::PopFn g_pop = nullptr;
uint8_t g_read_buf[MtBle::kMaxFrame];

class Callbacks : public BLESecurityCallbacks, public BLEServerCallbacks {
  uint32_t onPassKeyRequest() override { return g_pin; }
  void onPassKeyNotify(uint32_t) override {}
  bool onConfirmPIN(uint32_t) override { return true; }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    if (cmpl.success) {
      g_connected = true;
    } else {
      g_server->disconnect(g_server->getConnId());
      g_adv_restart = millis() + kAdvRestartMs;
    }
  }
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override { g_conn_id = param->connect.conn_id; }
  void onDisconnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    // Årsag (HCI): 0x08 forbindelsen tabt (timeout), 0x13 telefonen lukkede, 0x16 vi lukkede.
    g_disc_reason = param ? param->disconnect.reason : 0;
    g_connected = false;
    g_adv_restart = millis() + kAdvRestartMs;
  }
};
Callbacks g_cb;

class ToRadioCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c, esp_ble_gatts_cb_param_t*) override {
    const size_t len = c->getLength();
    if (!len || len > MtBle::kMaxFrame || !g_rxq) return;
    static Frame f;  // kun Bluetooth-tasken skriver her
    f.len = uint16_t(len);
    std::memcpy(f.buf, c->getData(), len);
    xQueueSend(g_rxq, &f, 0);
  }
};
ToRadioCb g_to_cb;

// Én FromRadio pr. læsning; tom = intet nyt (opstartsforløbet ligger klar i køen på forhånd).
class FromRadioCb : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* c, esp_ble_gatts_cb_param_t*) override {
    const size_t n = g_pop ? g_pop(g_read_buf, sizeof(g_read_buf)) : 0;
    c->setValue(g_read_buf, n);
  }
};
FromRadioCb g_from_cb;
}  // namespace

bool MtBle::begin(const char* name, uint32_t pin, PopFn pop) {
  if (started_) return true;
  g_pin = (pin >= 100000 && pin <= 999999) ? pin : 123456;
  g_pop = pop;
  g_rxq = xQueueCreate(kQueue, sizeof(Frame));
  char short_name[kBleNameMax + 1];
  bleName(short_name, sizeof(short_name), name);  // for langt navn = nedbrud i Bluedroid
  name = short_name;
  BLEDevice::init(name);
  BLEDevice::setSecurityCallbacks(&g_cb);
  BLEDevice::setMTU(517);
  BLESecurity sec;
  sec.setStaticPIN(g_pin);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(&g_cb);
  g_service = g_server->createService(kService);
  BLECharacteristic* to = g_service->createCharacteristic(kToRadio, BLECharacteristic::PROPERTY_WRITE);
  to->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  to->setCallbacks(&g_to_cb);
  BLECharacteristic* from = g_service->createCharacteristic(kFromRadio, BLECharacteristic::PROPERTY_READ);
  from->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  from->setCallbacks(&g_from_cb);
  g_from_num = g_service->createCharacteristic(kFromNum, BLECharacteristic::PROPERTY_READ |
                                                             BLECharacteristic::PROPERTY_NOTIFY);
  g_from_num->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  g_from_num->addDescriptor(new BLE2902());
  g_from_num->setValue(g_num);
  g_server->getAdvertising()->addServiceUUID(kService);
  g_service->start();
  g_server->getAdvertising()->start();
  started_ = true;
  DLOG.printf("[BLE] Meshtastic-app: klar som \"%s\" (PIN skjult)\n", name);
  return true;
}

bool MtBle::connected() const { return started_ && g_connected; }

size_t MtBle::poll(uint8_t* dest) {
  if (!started_ || !g_rxq) return 0;
  Frame f;
  if (xQueueReceive(g_rxq, &f, 0) != pdTRUE) return 0;
  std::memcpy(dest, f.buf, f.len);
  return f.len;
}

void MtBle::notify() {
  if (!started_ || !g_from_num) return;
  ++g_num;
  g_from_num->setValue(g_num);
  if (g_connected) g_from_num->notify();
}

void MtBle::loop() {
  static bool was = false;  // skift logges her (loop), ikke i Bluetooth-tasken
  if (started_ && g_connected != was) {
    was = g_connected;
    if (was) {
      g_conn_ms = millis();
      DLOG.println("[BLE] Meshtastic-app: Bluetooth forbundet (parret)");
    } else {
      const int r = g_disc_reason;
      DLOG.printf("[BLE] Meshtastic-app: Bluetooth afbrudt efter %lu s, aarsag 0x%02x (%s)\n",
                  (unsigned long)((millis() - g_conn_ms) / 1000), r,
                  r == 0x08 ? "forbindelsen tabt" : r == 0x13 ? "telefonen lukkede" : r == 0x16 ? "vi lukkede" : "andet");
    }
  }
  if (started_ && g_adv_restart && int32_t(millis() - g_adv_restart) >= 0) {
    g_adv_restart = 0;
    g_server->getAdvertising()->start();
  }
}

}  // namespace dm
