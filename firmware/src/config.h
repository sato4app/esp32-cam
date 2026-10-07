#pragma once

// ===== Wi-Fi =====
// 接続先は、入口ページからBluetoothで送って本体に保存したもの（WifiStore）と、secrets.h の WIFI_LIST の両方を使う
#define HOSTNAME "esp32cam"             // http://esp32cam.local/ で開ける（mDNS対応端末のみ）
#define WIFI_CONNECT_TIMEOUT_MS 20000   // この時間内につながらなければアクセスポイントモードにする
#define WIFI_RETRY_INTERVAL_MS  30000   // アクセスポイントモード中、登録したWi-Fiを探し直す間隔（テザリングを後からONにした場合など）
#define WIFI_LOST_TO_AP_MS      60000   // Wi-Fiが切れてからこの時間たっても戻らなければアクセスポイントモードにする
#define WIFI_STORE_MAX 5                // Bluetoothで保存できる接続先の数（超えると最も古いものを消す）

// ===== Bluetooth（アドレスの通知・Wi-Fiの設定） =====
// テザリングはつなぐたびにアドレスが変わるので、Bluetoothで今のアドレスを知らせる。
// Wi-Fiの接続先（SSID・パスワード）もBluetoothで受け取る。受け取りにはペアリング（secrets.h の BLE_PASSKEY）が必要。
// Bluetoothを動かしている間はWi-Fiの省電力を切れず映像が遅くなるため、映像の配信が始まったら止める。
#define BLE_DISCOVERY 1                 // 0にするとBluetoothを使わない（Wi-Fiの接続先は secrets.h だけになる）
#define BLE_NAME "ESP32-CAM"            // スマホの検索画面に出る名前（入口ページの index.html と合わせる）
#define BLE_SETUP_WINDOW_MS 300000      // Wi-Fiの設定を受け付けるのは電源を入れてからこの時間だけ（5分）
#define BLE_SERVICE_UUID     "65cfeafb-2789-4da5-b58f-29f337456ab4"
#define BLE_INFO_CHAR_UUID   "226570ef-9168-409d-9147-c84f40b6e087" // 読み出し: アドレスなど（ペアリング不要）
#define BLE_WIFI_CHAR_UUID   "f07241dd-9ed4-4847-b3fd-83871a65d4e5" // 書き込み: Wi-Fiの設定コマンド（ペアリング必須）
#define BLE_RESULT_CHAR_UUID "ff87d3b4-3849-4a26-be1e-9aa77e4412be" // 読み出し: コマンドの結果（ペアリング必須）

// ===== AI Thinker ESP32-CAM のカメラ端子 =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===== LED =====
// microSDを1ビットモードで使うと GPIO4（フラッシュLED）が空くので、PWMで明るさを変えられる
#define FLASH_LED_PIN      4
#define FLASH_LED_CHANNEL  7   // カメラのXCLKがLEDCチャンネル0を使うので重ならない番号にする
#define STATUS_LED_PIN    33   // 基板裏の赤色LED（LOWで点灯）

// ===== 保存 =====
#define SD_MOUNT_POINT "/sdcard"
#define MEDIA_DIR      "/DCIM"           // 写真・動画の保存先（SDカード内）
#define REC_SPLIT_SECONDS 600            // 録画はこの秒数ごとに別ファイルへ分ける
#define REC_MAX_FPS 30                   // 1ファイルの索引を確保する上限フレームレート
#define REC_MIN_FREE_BYTES (20UL * 1024 * 1024) // 空きがこれ未満なら録画を始めない
#define FILE_LIST_MAX 300                // 一覧で返すファイル数の上限（新しい順）

// ===== 初期設定（画面で変更でき、NVSに保存される） =====
#define DEFAULT_FRAMESIZE FRAMESIZE_VGA  // 640x480
#define DEFAULT_QUALITY   12             // JPEG画質 10〜63（小さいほど高画質・大きいファイル）
