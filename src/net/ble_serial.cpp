// SPDX-License-Identifier: MIT
// Efter MeshCores src/helpers/esp32/SerialBLEInterface.cpp (MIT,
// Copyright (c) 2025 Scott Powell / rippleradios.com).
#include "net/ble_serial.h"

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
constexpr const char* kService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kRxChar = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kTxChar = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr uint32_t kWriteGapMs = 60;       // som MeshCore: mellemrum mellem notify
constexpr uint32_t kAdvRestartMs = 1000;
constexpr int kQueue = 4;

struct Frame {
  uint8_t len;
  uint8_t buf[BleSerial::kMaxFrame];
};

BLEServer* g_server = nullptr;
BLEService* g_service = nullptr;
BLECharacteristic* g_tx = nullptr;
QueueHandle_t g_rxq = nullptr;
volatile bool g_connected = false;  // forbundet OG godkendt (parret)
volatile bool g_changed = false;
volatile uint32_t g_adv_restart = 0;
uint16_t g_conn_id = 0;
uint32_t g_pin = 123456;
Frame g_sendq[kQueue];
int g_sendq_len = 0;
uint32_t g_last_write = 0;
bool g_was_connected = false;

class Callbacks : public BLESecurityCallbacks, public BLEServerCallbacks, public BLECharacteristicCallbacks {
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
    g_changed = true;
  }
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    g_conn_id = param->connect.conn_id;
  }
  void onDisconnect(BLEServer*) override {
    g_connected = false;
    g_changed = true;
    g_adv_restart = millis() + kAdvRestartMs;
  }
  void onWrite(BLECharacteristic* c, esp_ble_gatts_cb_param_t*) override {
    const size_t len = c->getLength();
    if (!len || len > BleSerial::kMaxFrame || !g_rxq) return;
    Frame f;
    f.len = uint8_t(len);
    std::memcpy(f.buf, c->getData(), len);
    xQueueSend(g_rxq, &f, 0);  // fuld kø: rammen tabes (appen sender igen)
  }
};
Callbacks g_cb;
}  // namespace

bool BleSerial::begin(const char* name, uint32_t pin) {
  if (started_) return true;
  pin_ = g_pin = (pin >= 100000 && pin <= 999999) ? pin : 123456;
  bleName(name_, sizeof(name_), name);
  if (std::strlen(name) > std::strlen(name_)) DLOG.printf("[BLE] navnet afkortet til %u byte\n", unsigned(kBleNameMax));
  g_rxq = xQueueCreate(kQueue, sizeof(Frame));
  BLEDevice::init(name_);
  BLEDevice::setSecurityCallbacks(&g_cb);
  BLEDevice::setMTU(kMaxFrame);
  BLESecurity sec;
  sec.setStaticPIN(pin_);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(&g_cb);
  g_service = g_server->createService(kService);
  g_tx = g_service->createCharacteristic(kTxChar, BLECharacteristic::PROPERTY_READ |
                                                      BLECharacteristic::PROPERTY_NOTIFY);
  g_tx->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  g_tx->addDescriptor(new BLE2902());
  BLECharacteristic* rx = g_service->createCharacteristic(kRxChar, BLECharacteristic::PROPERTY_WRITE);
  rx->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  rx->setCallbacks(&g_cb);
  g_server->getAdvertising()->addServiceUUID(kService);
  g_service->start();
  g_server->getAdvertising()->start();
  started_ = true;
  DLOG.printf("[BLE] klar som \"%s\" (PIN skjult)\n", name_);
  return true;
}

void BleSerial::end() {
  if (!started_) return;
  g_server->getAdvertising()->stop();
  if (g_connected) g_server->disconnect(g_conn_id);
  g_service->stop();
  g_connected = false;
  g_sendq_len = 0;
  started_ = false;
  DLOG.println("[BLE] slaaet fra");
}

bool BleSerial::connected() const { return started_ && g_connected; }

bool BleSerial::writeFrame(const uint8_t* data, size_t len) {
  if (!connected() || !len || len > kMaxFrame) return false;
  if (g_sendq_len >= kQueue) {
    DLOG.println("[BLE] sendekoe fuld - ramme tabt");
    return false;
  }
  g_sendq[g_sendq_len].len = uint8_t(len);
  std::memcpy(g_sendq[g_sendq_len].buf, data, len);
  ++g_sendq_len;
  return true;
}

bool BleSerial::writeBusy() const { return g_sendq_len >= kQueue - 1 || millis() - g_last_write < kWriteGapMs; }

size_t BleSerial::poll(uint8_t* dest) {
  if (!started_) return 0;
  const uint32_t now = millis();
  if (g_sendq_len > 0 && now - g_last_write >= kWriteGapMs) {
    g_last_write = now;
    g_tx->setValue(g_sendq[0].buf, g_sendq[0].len);
    g_tx->notify();
    --g_sendq_len;
    for (int i = 0; i < g_sendq_len; ++i) g_sendq[i] = g_sendq[i + 1];
  }
  if (g_changed) {
    g_changed = false;
    if (g_connected != g_was_connected) {
      g_was_connected = g_connected;
      if (g_connected) {
        g_server->getAdvertising()->stop();
        g_adv_restart = 0;
        DLOG.println("[BLE] app forbundet (parret)");
      } else {
        g_sendq_len = 0;
        xQueueReset(g_rxq);
        DLOG.println("[BLE] app afbrudt");
      }
    }
  }
  if (g_adv_restart && int32_t(now - g_adv_restart) >= 0) {
    g_adv_restart = 0;
    if (g_server->getConnectedCount() == 0) g_server->getAdvertising()->start();
  }
  Frame f;
  if (g_rxq && xQueueReceive(g_rxq, &f, 0) == pdTRUE) {
    std::memcpy(dest, f.buf, f.len);
    return f.len;
  }
  return 0;
}

}  // namespace dm
