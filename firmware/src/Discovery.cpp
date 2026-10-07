#include "Discovery.h"
#include "config.h"
#include "WifiStore.h"
#include <WiFi.h>
#include <mutex>
#if BLE_DISCOVERY
#include <NimBLEDevice.h>
#endif

namespace Discovery {

static volatile bool viewerConnected = false;
static volatile bool wifiChanged = false;
static bool running = false;
static uint32_t blePasskey = 0;

void setPasskey(uint32_t passkey) {
  blePasskey = passkey;
}

bool consumeWifiChanged() {
  if (!wifiChanged) {
    return false;
  }
  wifiChanged = false;
  return true;
}

#if BLE_DISCOVERY

// infoJson は loop() のタスクが書き、Bluetoothのタスクが読むので排他する
static std::mutex infoMutex;
static String infoJson;

static NimBLECharacteristic *resultChar = nullptr;

static uint32_t setupRemainingSec() {
  unsigned long now = millis();
  return now < BLE_SETUP_WINDOW_MS ? (BLE_SETUP_WINDOW_MS - now) / 1000 : 0;
}

static String jsonEscape(const String &s) {
  String out = s;
  out.replace("\\", "\\\\");
  out.replace("\"", "\\\"");
  return out;
}

static void setResult(bool ok, const String &message) {
  String json = String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + jsonEscape(message) +
                "\",\"saved\":" + WifiStore::ssidListJson() + "}";
  resultChar->setValue((const uint8_t *)json.c_str(), json.length());
}

// アドレスの読み出し。読まれるたびに、設定を受け付ける残り秒数を付け足す
class InfoCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    String json;
    {
      std::lock_guard<std::mutex> lock(infoMutex);
      json = infoJson;
    }
    // {"url":...} → {"setup":<秒>,"url":...}
    json = String("{\"setup\":") + setupRemainingSec() + "," + json.substring(1);
    c->setValue((const uint8_t *)json.c_str(), json.length());
  }
};

// Wi-Fiの設定コマンド（UTF-8の文字列、行区切り）
//   ADD\n<SSID>\n<パスワード>   接続先を追加（同じSSIDは上書き）
//   DEL\n<SSID>                 接続先を削除
//   LIST                        保存している接続先の一覧（結果の saved に入る）
// 結果は BLE_RESULT_CHAR_UUID を読み出して受け取る
class WifiCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
    // 特性の設定でもペアリング必須にしているが、念のためここでも確かめる
    if (!connInfo.isEncrypted() || !connInfo.isAuthenticated()) {
      setResult(false, "ペアリングしていないため受け付けません");
      return;
    }
    NimBLEAttValue value = c->getValue();
    String text((const char *)value.data(), value.size());
    int nl1 = text.indexOf('\n');
    String command = nl1 < 0 ? text : text.substring(0, nl1);
    String rest = nl1 < 0 ? "" : text.substring(nl1 + 1);

    if (command == "LIST") {
      setResult(true, "");
      return;
    }
    if (setupRemainingSec() == 0) {
      setResult(false, "受付時間外です。ESP32-CAMの電源を入れ直してから5分以内に送ってください");
      return;
    }

    if (command == "ADD") {
      int nl2 = rest.indexOf('\n');
      if (nl2 < 0) {
        setResult(false, "形式が正しくありません");
        return;
      }
      String ssid = rest.substring(0, nl2);
      String message;
      bool ok = WifiStore::add(ssid, rest.substring(nl2 + 1), message);
      if (ok) {
        Serial.printf("Bluetoothで接続先を保存しました: %s\n", ssid.c_str());
        wifiChanged = true;
      }
      setResult(ok, message);
    } else if (command == "DEL") {
      bool ok = WifiStore::remove(rest);
      if (ok) {
        Serial.printf("Bluetoothで接続先を削除しました: %s\n", rest.c_str());
        wifiChanged = true;
      }
      setResult(ok, ok ? "削除しました" : "見つかりません");
    } else {
      setResult(false, "不明なコマンドです");
    }
  }
};

static void start() {
  // Wi-FiとBluetoothを同時に使うときは、Wi-Fiの省電力（モデムスリープ）が必須。
  // 切ったままBluetoothを起動すると異常終了するため、先に有効にする
  WiFi.setSleep(true);

  NimBLEDevice::init(BLE_NAME);
  // ペアリングは「ESP32-CAMが暗証番号を表示し、スマホで入力する」方式にする（表示はしないので固定の番号）。
  // 暗号化（LE Secure Connections）と、なりすまし対策（MITM保護）を有効にし、ペアリング情報を保存する
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(blePasskey);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->advertiseOnDisconnect(true); // 読み出し後に切断されたら、また見つけられるようにする

  NimBLEService *service = server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *infoChar = service->createCharacteristic(BLE_INFO_CHAR_UUID, NIMBLE_PROPERTY::READ);
  infoChar->setCallbacks(new InfoCallbacks());

  NimBLECharacteristic *wifiChar = service->createCharacteristic(
      BLE_WIFI_CHAR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  wifiChar->setCallbacks(new WifiCallbacks());

  resultChar = service->createCharacteristic(
      BLE_RESULT_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  setResult(true, "");

  // 128bitのUUIDと名前は広告パケット（31バイト）に両方は入らないので、名前だけ載せる。
  // Web側は名前で絞り込み、サービスは optionalServices で指定する
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setName(BLE_NAME);
  advertising->start();

  running = true;
  Serial.println("Bluetoothを開始（入口ページからアドレスの確認・Wi-Fiの設定ができます）");
}

static void stop() {
  NimBLEDevice::deinit(true);
  resultChar = nullptr;
  running = false;
  delay(100);
  WiFi.setSleep(false); // 映像の遅れを減らすため、Wi-Fiの省電力を切る
  Serial.println("映像の配信が始まったため、Bluetoothを止めました");
}

void announce(const String &json) {
  viewerConnected = false;
  {
    std::lock_guard<std::mutex> lock(infoMutex);
    infoJson = json;
  }
  if (!running) {
    start();
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
