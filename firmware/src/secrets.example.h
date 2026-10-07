#pragma once

// Wi-Fiの接続先。このファイルを同じフォルダに secrets.h としてコピーし、自分の値に書き換える。
// secrets.h は .gitignore 済みなので、パスワードがリポジトリに入ることはない。
//
// 上から順に探し、見つかったうち電波の強いものにつなぐ（2.4GHz帯のみ。5GHzは不可）。
// つながらなければ ESP32-CAM 自身がアクセスポイント（AP_SSID）になる。

static const char *WIFI_LIST[][2] = {
  // { "SSID", "パスワード" },
  { "your-ssid", "your-password" },
};

// アクセスポイントモードのSSIDとパスワード（パスワードは8文字以上）
#define AP_SSID     "ESP32-CAM"
#define AP_PASSWORD "esp32cam"
