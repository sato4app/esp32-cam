#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <esp_sntp.h>
#include "config.h"
#include "Camera.h"
#include "Storage.h"
#include "AppServer.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "secrets.h がありません。secrets.example.h をコピーして作ってください（今はアクセスポイントモードで起動します）"
#include "secrets.example.h"
#endif

WiFiMulti wifiMulti;
bool stationMode = false;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

void onTimeSynced(struct timeval *tv) {
  Storage::setTimeSource(Storage::TIME_NTP);
  Serial.println("NTPで時計を合わせました");
}

void printAccessInfo() {
  IPAddress ip = stationMode ? WiFi.localIP() : WiFi.softAPIP();
  Serial.println("----------------------------------------");
  if (stationMode) {
    Serial.printf("Wi-Fi接続: %s（電波 %d dBm）\n", WiFi.SSID().c_str(), WiFi.RSSI());
  } else {
    Serial.printf("アクセスポイント: SSID %s / パスワード %s\n", AP_SSID, AP_PASSWORD);
  }
  Serial.printf("スマホのブラウザで開く: http://%s/\n", ip.toString().c_str());
  Serial.printf("（mDNS対応の端末なら http://%s.local/ でも開ける）\n", HOSTNAME);
  Serial.println("----------------------------------------");
}

// secrets.h の接続先に順に試し、つながらなければ自分がアクセスポイントになる
void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false); // 省電力を切って映像の遅れを減らす

  int count = 0;
  for (auto &entry : WIFI_LIST) {
    if (strcmp(entry[0], "your-ssid") != 0) {
      wifiMulti.addAP(entry[0], entry[1]);
      count++;
    }
  }

  if (count > 0) {
    Serial.print("Wi-Fiに接続中");
    unsigned long start = millis();
    while (millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
      if (wifiMulti.run(5000) == WL_CONNECTED) {
        stationMode = true;
        break;
      }
      Serial.print(".");
    }
    Serial.println();
  }

  if (stationMode) {
    lastWifiStatus = WL_CONNECTED;
    sntp_set_time_sync_notification_cb(onTimeSynced);
    configTzTime("JST-9", "ntp.nict.jp", "pool.ntp.org");
  } else {
    Serial.println("Wi-Fiにつながらないため、アクセスポイントモードで起動します");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  }

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
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

  startWifi();
  AppServer::begin();
  printAccessInfo();
}

void loop() {
  // Wi-Fiの切断・再接続をシリアルに知らせる（再接続はWi-Fiライブラリが自動で行う）
  if (stationMode) {
    wl_status_t status = WiFi.status();
    if (status != lastWifiStatus) {
      lastWifiStatus = status;
      if (status == WL_CONNECTED) {
        Serial.println("Wi-Fiに再接続しました");
        printAccessInfo();
      } else {
        Serial.println("Wi-Fiが切断されました。再接続を待っています");
      }
    }
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
