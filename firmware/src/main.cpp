#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <esp_sntp.h>
#include "config.h"
#include "Camera.h"
#include "Storage.h"
#include "AppServer.h"
#include "Discovery.h"
#include "WifiStore.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h がありません。secrets.example.h を secrets.h という名前でコピーし、暗証番号とパスワードを書いてください"
#endif

#if BLE_DISCOVERY
#ifndef BLE_PASSKEY
#error "secrets.h に BLE_PASSKEY（Bluetoothのペアリングに使う6桁の暗証番号）を追加してください。secrets.example.h を参照"
#endif
static_assert(BLE_PASSKEY >= 100000 && BLE_PASSKEY <= 999999,
              "BLE_PASSKEY は6桁の数字にしてください（先頭を0にしない）");
#endif

WiFiMulti *wifiMulti = nullptr;
int wifiCount = 0;               // 登録している接続先の数（Bluetoothで保存したもの＋secrets.h）
bool stationMode = false;        // true: Wi-Fiにつながっている / false: アクセスポイントモード
wl_status_t lastWifiStatus = WL_IDLE_STATUS;
unsigned long lastRetryMs = 0;
unsigned long wifiLostMs = 0;    // Wi-Fiが切れた時刻（つながっている間は0）
IPAddress announcedIp;           // Bluetoothで最後に知らせたアドレス
bool announcedStation = false;

void onTimeSynced(struct timeval *tv) {
  Storage::setTimeSource(Storage::TIME_NTP);
  Serial.println("NTPで時計を合わせました");
}

IPAddress currentIp() {
  return stationMode ? WiFi.localIP() : WiFi.softAPIP();
}

String currentSsid() {
  return stationMode ? WiFi.SSID() : String(AP_SSID);
}

void printAccessInfo() {
  Serial.println("----------------------------------------");
  if (stationMode) {
    Serial.printf("Wi-Fi接続: %s（電波 %d dBm）\n", WiFi.SSID().c_str(), WiFi.RSSI());
  } else {
    Serial.printf("アクセスポイント: SSID %s / パスワード %s\n", AP_SSID, AP_PASSWORD);
  }
  Serial.printf("スマホのブラウザで開く: http://%s/\n", currentIp().toString().c_str());
  Serial.printf("（mDNS対応の端末なら http://%s.local/ でも開ける）\n", HOSTNAME);
  Serial.println("----------------------------------------");
}

void startMdns() {
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }
}

// 今のアドレスが前回知らせたものと違えば、Bluetoothで知らせ直す
void announceIfChanged() {
  IPAddress ip = currentIp();
  if (ip == IPAddress(0, 0, 0, 0) || (ip == announcedIp && stationMode == announcedStation)) {
    return;
  }
  announcedIp = ip;
  announcedStation = stationMode;

  String ssid = currentSsid();
  ssid.replace("\\", "\\\\");
  ssid.replace("\"", "\\\"");
  String json = String("{\"url\":\"http://") + ip.toString() + "/\",\"mode\":\"" +
                (stationMode ? "STA" : "AP") + "\",\"ssid\":\"" + ssid + "\"}";
  Discovery::announce(json);
}

void onStationConnected() {
  stationMode = true;
  lastWifiStatus = WL_CONNECTED;
  configTzTime("JST-9", "ntp.nict.jp", "pool.ntp.org");
  startMdns();
}

void startAccessPoint() {
  Serial.println("Wi-Fiにつながらないため、アクセスポイントモードにします");
  stationMode = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  startMdns();
  lastRetryMs = millis();
}

// アクセスポイントモード中に、登録したWi-Fi（テザリングなど）が使えるようになっていないか探す
void retryStation() {
  Serial.println("登録したWi-Fiを探しています…");
  WiFi.mode(WIFI_AP_STA);
  if (wifiMulti->run(8000) == WL_CONNECTED) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    onStationConnected();
    Serial.println("Wi-Fiにつながったので、アクセスポイントモードを終了しました");
    announceIfChanged();
    printAccessInfo();
  } else {
    WiFi.mode(WIFI_AP);
  }
  lastRetryMs = millis();
}

// 接続先の一覧を作り直す（Bluetoothで保存したもの＋secrets.h の WIFI_LIST）。
// 見つかった中で電波の強いものにつなぐ
void rebuildWifiList() {
  delete wifiMulti;
  wifiMulti = new WiFiMulti();
  wifiCount = 0;
  for (const WifiStore::Network &net : WifiStore::load()) {
    wifiMulti->addAP(net.ssid.c_str(), net.password.c_str());
    wifiCount++;
  }
  for (auto &entry : WIFI_LIST) {
    if (entry[0] && entry[0][0] != '\0' && strcmp(entry[0], "your-ssid") != 0) {
      wifiMulti->addAP(entry[0], entry[1]);
      wifiCount++;
    }
  }
  Serial.printf("登録している接続先: %d件\n", wifiCount);
}

// 登録した接続先に順に試し、つながらなければ自分がアクセスポイントになる
void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  sntp_set_time_sync_notification_cb(onTimeSynced);
  // Wi-Fiの接続が決まるのを待たずにBluetoothを始め、電源を入れてすぐ入口ページで見つけられるようにする
  Discovery::begin();
  rebuildWifiList();

  bool connected = false;
  if (wifiCount > 0) {
    Serial.print("Wi-Fiに接続中");
    unsigned long start = millis();
    while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      // 接続中にBluetoothで接続先が追加・削除されたら、一覧を作り直して試す
      if (Discovery::consumeWifiChanged()) {
        rebuildWifiList();
      }
      if (wifiMulti->run(5000) == WL_CONNECTED) {
        connected = true;
        break;
      }
      Serial.print(".");
    }
    Serial.println();
  }

  if (connected) {
    onStationConnected();
  } else {
    startAccessPoint();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("ESP32-CAM Stream 起動");

  // 日本時間で扱う（NTPで合わせるまではブラウザの時刻を使う）
  setenv("TZ", "JST-9", 1);
  tzset();

  // カメラより先にmicroSDを初期化する（1ビットモードでGPIO4を空けてからフラッシュLEDに使う）
  Storage::begin();
  if (!Camera::begin()) {
    Serial.println("カメラなしで起動を続けます（画面でエラーを確認できます）");
  }

#if BLE_DISCOVERY
  Discovery::setPasskey(BLE_PASSKEY);
#endif
  startWifi();
  AppServer::begin();
  announceIfChanged();
  printAccessInfo();
}

void loop() {
  Discovery::loop();

  // Bluetoothで接続先が追加・削除された
  if (Discovery::consumeWifiChanged()) {
    rebuildWifiList();
    if (!stationMode && wifiCount > 0) {
      retryStation(); // すぐに新しい接続先を試す
    }
  }

  if (stationMode) {
    // Wi-Fiの切断・再接続をシリアルに知らせる（再接続はWi-Fiライブラリが自動で行う）。
    // テザリングを入れ直すとアドレスが変わることがあるので、つながったらBluetoothで知らせ直す
    wl_status_t status = WiFi.status();
    if (status != lastWifiStatus) {
      lastWifiStatus = status;
      if (status == WL_CONNECTED) {
        wifiLostMs = 0;
        Serial.println("Wi-Fiに再接続しました");
        announceIfChanged();
        printAccessInfo();
      } else if (wifiLostMs == 0) {
        wifiLostMs = millis();
        Serial.println("Wi-Fiが切断されました。再接続を待っています");
      }
    }
    // テザリングを切ったままなど、しばらく戻らなければアクセスポイントモードにする
    // （その後も30秒ごとに登録した接続先を探す）
    if (wifiLostMs != 0 && millis() - wifiLostMs >= WIFI_LOST_TO_AP_MS) {
      wifiLostMs = 0;
      startAccessPoint();
      announceIfChanged();
      printAccessInfo();
    }
  } else if (wifiCount > 0 && WiFi.softAPgetStationNum() == 0 &&
             millis() - lastRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    // アクセスポイントにスマホがつながっていないときだけ探す（探す間は一瞬アクセスポイントが不安定になるため）
    retryStation();
  }

  // シリアルモニタで Enter を押すと接続先を表示する
  if (Serial.available()) {
    while (Serial.available()) {
      Serial.read();
    }
    printAccessInfo();
  }
  delay(200);
}
