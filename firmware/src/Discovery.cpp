#include "Discovery.h"
#include "config.h"
#include <WiFi.h>
#if BLE_DISCOVERY
#include <NimBLEDevice.h>
#endif

namespace Discovery {

static volatile bool viewerConnected = false;
static bool running = false;

#if BLE_DISCOVERY

static NimBLECharacteristic *infoChar = nullptr;

static void start(const String &infoJson) {
  // Wi-FiとBluetoothを同時に使うときは、Wi-Fiの省電力（モデムスリープ）が必須。
  // 切ったままBluetoothを起動すると異常終了するため、先に有効にする
  WiFi.setSleep(true);

  NimBLEDevice::init(BLE_NAME);
  NimBLEServer *server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true); // 読み出し後に切断されたら、また見つけられるようにする

  NimBLEService *service = server->createService(BLE_SERVICE_UUID);
  infoChar = service->createCharacteristic(BLE_INFO_CHAR_UUID, NIMBLE_PROPERTY::READ);
  infoChar->setValue((const uint8_t *)infoJson.c_str(), infoJson.length());

  // 128bitのUUIDと名前は広告パケット（31バイト）に両方は入らないので、名前だけ載せる。
  // Web側は名前で絞り込み、サービスは optionalServices で指定する
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setName(BLE_NAME);
  advertising->start();

  running = true;
  Serial.println("Bluetoothでアドレスの通知を開始（入口ページから見つけられます）");
}

static void stop() {
  NimBLEDevice::deinit(true);
  infoChar = nullptr;
  running = false;
  delay(100);
  WiFi.setSleep(false); // 映像の遅れを減らすため、Wi-Fiの省電力を切る
  Serial.println("映像の配信が始まったため、Bluetoothを止めました");
}

void announce(const String &infoJson) {
  viewerConnected = false;
  if (running) {
    infoChar->setValue((const uint8_t *)infoJson.c_str(), infoJson.length());
  } else {
    start(infoJson);
  }
}

void loop() {
  if (running && viewerConnected) {
    stop();
  }
}

#else

void announce(const String &) {
  WiFi.setSleep(false); // Bluetoothを使わないので、Wi-Fiの省電力は常に切っておく
}

void loop() {}

#endif

void onViewerConnected() {
  viewerConnected = true;
}

bool active() {
  return running;
}

} // namespace Discovery
