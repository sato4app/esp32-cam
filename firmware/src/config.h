#pragma once

// ===== Wi-Fi =====
// 接続先は secrets.h に書く（secrets.example.h をコピーして作る）
#define HOSTNAME "esp32cam"             // http://esp32cam.local/ で開ける（mDNS対応端末のみ）
#define WIFI_CONNECT_TIMEOUT_MS 20000   // この時間内につながらなければアクセスポイントモードにする
#define WIFI_RETRY_INTERVAL_MS  30000   // アクセスポイントモード中、登録したWi-Fiを探し直す間隔（テザリングを後からONにした場合など）

// ===== Bluetooth（スマホにアドレスを知らせる） =====
// テザリングはつなぐたびにアドレスが変わるので、Bluetoothで今のアドレスを知らせる。
// Bluetoothを動かしている間はWi-Fiの省電力を切れず映像が遅くなるため、映像の配信が始まったら止める。
#define BLE_DISCOVERY 1                 // 0にするとBluetoothを使わない
#define BLE_NAME "ESP32-CAM"            // スマホの検索画面に出る名前（入口ページの index.html と合わせる）
#define BLE_SERVICE_UUID   "65cfeafb-2789-4da5-b58f-29f337456ab4"
#define BLE_INFO_CHAR_UUID "226570ef-9168-409d-9147-c84f40b6e087" // 読み出すとアドレスなどをJSONで返す

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
