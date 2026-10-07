## esp32-cam-stream - ESP32-CAMの映像をスマホに表示

ESP32-CAMの映像をWi-Fi経由でスマホのブラウザに表示し、写真・動画をmicroSDカードに保存するアプリ。
スマホ用の画面はESP32-CAM自身が配信するため、スマホにアプリを入れる必要はない。

映像はデータ量が多くBluetooth（BLE）では送れないため、Wi-Fiで送る。
Bluetoothは、テザリングでつなぐたびに変わるアドレスをスマホに知らせるためだけに使う（入口ページ）。

## できること

| 機能 | 内容 |
|---|---|
| ライブ映像 | スマホのブラウザに映像を表示する（全画面・一時停止あり） |
| 写真 | 撮影した画像をmicroSDに保存する（JPEG） |
| 録画 | 映像をmicroSDに動画として保存する（AVI / MJPEG）。10分ごとに別ファイルに分ける |
| 静止画をスマホに保存 | 今の映像を1枚、スマホに直接ダウンロードする |
| ファイル管理 | microSDの写真を表示、写真・動画をスマホにダウンロード、削除 |
| ライト | 基板のフラッシュLEDの明るさを0〜100%で変える |
| カメラ設定 | 解像度（320×240〜1600×1200）、画質、上下・左右反転。ESP32-CAMに保存され、再起動後も残る |
| 入口ページ | GitHub Pages のページからBluetoothでESP32-CAMを探し、今のアドレスでカメラの画面を開く |

## 必要なもの

- AI Thinker ESP32-CAM（OV2640カメラ・PSRAM付き）
- ESP32-CAM-MB（USB書き込み基板。ESP32-CAMを差してUSBでPCにつなぐ）
- microSDカード（FAT32でフォーマットしたもの）
  - 64GB以上のカード（SDXC）は出荷時が exFAT のため、そのままでは使えない。FAT32にフォーマットし直せば使える
  - Windows 11 なら管理者のコマンドプロンプトで `format E: /FS:FAT32 /Q`（E: はカードのドライブ）。
    32GBを超えるカードを受け付けない場合は「Rufus」などのフォーマットツールでFAT32を選ぶ
- 2.4GHz帯のWi-Fi（ルーターまたはスマホのテザリング）。無い場合はESP32-CAM自身がアクセスポイントになる

他社のESP32-CAM互換基板はカメラの端子配置が違うことがある。その場合は `firmware/src/config.h` の端子番号を直す。

## ファイル構成

```
index.html                       入口ページ（GitHub Pages。BluetoothでESP32-CAMを探してカメラの画面を開く）
manifest.json                    入口ページのPWA設定
service-worker.js                入口ページのオフライン起動用キャッシュ制御
icons/                           PWAアイコン（192x192 / 512x512。元データは icon.svg）
esp32-cam-stream.code-workspace  VS Code 用（リポジトリと firmware を同時に開く）
firmware/                        ESP32-CAMのファーム（PlatformIOプロジェクト）
  platformio.ini                 ボード・ビルド設定
  web/index.html                 スマホ用の画面（ビルド時にファームへ埋め込む）
  src/main.cpp                   起動処理・Wi-Fi接続（接続できなければアクセスポイント）
  src/Discovery.cpp/.h           Bluetoothでアドレスを知らせる
  src/config.h                   端子番号・保存先・録画の分割時間などの設定
  src/secrets.example.h          Wi-Fi接続先のひな形（secrets.h にコピーして使う）
  src/Camera.cpp/.h              カメラ制御・写真保存・録画（専用タスクで撮影）
  src/AviWriter.cpp/.h           AVI（MJPEG）ファイルの書き出し
  src/Storage.cpp/.h             microSDのマウント・ファイル名・一覧・削除
  src/AppServer.cpp/.h           Webサーバ（画面・操作API・映像・ファイル）
```

## 準備と書き込み

### 1. Wi-Fiの接続先を書く

`firmware/src/secrets.example.h` を同じフォルダに `secrets.h` という名前でコピーし、SSIDとパスワードを書く。
複数書いておくと、見つかったうち電波の強いものにつなぐ（家のルーターとスマホのテザリングを両方書いておくと便利）。

```cpp
static const char *WIFI_LIST[][2] = {
  { "家のルーターのSSID", "パスワード" },
  { "スマホのテザリングのSSID", "パスワード" },
};
```

`secrets.h` は `.gitignore` 済みなのでリポジトリには入らない。
作らずにビルドした場合は、アクセスポイントモードだけで動く。

### 2. 書き込み

1. ESP32-CAMをESP32-CAM-MBに差し、USBケーブルでPCにつなぐ
2. VS Codeで `esp32-cam-stream.code-workspace` を開き、PlatformIOでビルド・書き込みする

CLIの場合:

```bash
pio run -d firmware                  # ビルド
pio run -d firmware -t upload        # 書き込み
pio device monitor -d firmware       # シリアルモニタ（115200bps）
```

書き込みに失敗する場合は、ESP32-CAM-MBの「IO0」ボタンを押したまま「RST」ボタンを押して離し、もう一度書き込む。

### 3. スマホで開く

起動するとシリアルモニタに開くアドレスが表示される（Enterを押すと再表示）。

```
Wi-Fi接続: 家のルーター（電波 -52 dBm）
スマホのブラウザで開く: http://192.168.1.23/
```

- **Wi-Fiにつながった場合**: スマホを同じWi-Fiにつなぎ、表示されたアドレスをブラウザで開く
- **つながらなかった場合（アクセスポイントモード）**: スマホのWi-Fi設定で `ESP32-CAM`（パスワード `esp32cam`）につなぎ、`http://192.168.4.1/` を開く。
  「インターネットに接続されていません」と出ても、そのまま接続を維持する
- mDNSに対応した端末（iPhone・PCなど）は `http://esp32cam.local/` でも開ける
- **USBを外して使う場合**は、次の「入口ページ」を使うとアドレスを調べずに開ける

### 4. 入口ページ（テザリングで使う場合）

スマホのテザリングは、つなぐたびにアドレスが変わる（Android 11以降）。
そこで、ESP32-CAMがBluetoothで今のアドレスを知らせ、入口ページがそれを読み取ってカメラの画面を開く。

1. GitHub Pages を有効にする（Settings → Pages → Branch: `main` / `(root)`）
2. Androidの Chrome で `https://sato4app.github.io/esp32-cam-stream/` を開き、メニューの「ホーム画面に追加」（またはアプリをインストール）をする
3. 使うとき: テザリングをON → ESP32-CAMの電源を入れる → 入口ページの「Bluetoothで探して開く」→ 一覧の「ESP32-CAM」を選ぶ

- 一度開いたアドレスは入口ページに「前回のアドレス」として残る
- ESP32-CAMがアクセスポイントモードの場合は、入口ページにその旨と `http://192.168.4.1/` が出る
- スマホのBluetoothと位置情報をONにしておく。SafariとFirefoxはWeb Bluetooth非対応

テザリングの設定（Pixelの場合: 設定 → ネットワークとインターネット → アクセスポイントとテザリング → Wi-Fiアクセスポイント）

- 「互換性を拡張」をONにする（OFFだと5GHz帯になり、2.4GHzにしかつながらないESP32-CAMから見えない）
- セキュリティは「WPA2-Personal」にする

テザリングを後からONにした場合も、ESP32-CAMはアクセスポイントモードのまま30秒ごとに登録したWi-Fiを探し、
見つかれば切り替わる（アクセスポイントにスマホがつながっている間は探さない）。

#### Bluetoothの動き

Wi-FiとBluetoothを同時に動かしている間は、ESP32の制約でWi-Fiの省電力を切れず、映像が遅くなる。
そのため、Bluetoothは次のように動かしている。

| とき | Bluetooth |
|---|---|
| 起動時 | アドレスの通知を始める |
| 映像の配信が始まった | 止める（Wi-Fiの省電力を切って映像を速くする） |
| Wi-Fiにつなぎ直してアドレスが変わった | 新しいアドレスで通知を再開する |

映像を見た後にもう一度入口ページで探したい場合は、ESP32-CAMのRSTボタンを押すか電源を入れ直す。
Bluetoothを使わない場合は `config.h` の `BLE_DISCOVERY` を `0` にする。

## 使い方

- **📷 撮影**: 今の映像をmicroSDに写真として保存する
- **● 録画**: 押すと録画を始め、もう一度押すと止める。録画中は映像の左上に「REC」と経過時間が出て、基板裏の赤色LEDが点滅する
- **📱 静止画**: 今の映像をスマホに直接保存する（microSDには保存しない）
- **microSDカード**: 保存したファイルの一覧。写真は「表示」で見られ、動画は「保存」でスマホにダウンロードする
- **ライト**: フラッシュLEDの明るさ。暗い場所で使う

### ファイル名と時刻

ファイルは microSD の `DCIM` フォルダに保存する。

| 種類 | ファイル名 |
|---|---|
| 写真 | `IMG_20261007_123456.jpg` |
| 動画 | `VID_20261007_123456.avi` |

ESP32-CAMは時計を持たないため、Wi-Fiにつながった場合はインターネット（NTP）で、
アクセスポイントモードの場合は画面を開いたスマホの時刻で時計を合わせる。
時計が合う前に保存したファイルは `IMG_0001.jpg` のような連番になる。

### 動画について

- 形式はAVI（MJPEG）。スマホのブラウザでは再生できないため、ダウンロードして **VLC** などの動画アプリで再生する（PCでも同じ）
- 1ファイルは最長10分。長く録画すると自動で次のファイルに切り替わる（`config.h` の `REC_SPLIT_SECONDS` で変更可）
- フレームレートはカメラの撮影速度で決まり、解像度が高いほど下がる（目安: VGAで15〜25fps、UXGAで数fps）。再生時間は実際の時間に合わせてある
- 録画中に電源を切ると、そのファイルは索引が書かれず再生できないことがある。VLCなら「壊れたAVIを修復」で再生できる場合が多い
- microSDがいっぱいになると録画は自動で止まり、画面にエラーが出る

## 通信仕様

ESP32-CAMは3つのWebサーバを動かす。映像やファイルの送信中も操作が止まらないよう、ポートを分けている。

| ポート | 用途 |
|---|---|
| 80 | 画面（`/`）・操作API |
| 81 | ライブ映像 `GET /stream`（MJPEG / multipart/x-mixed-replace） |
| 82 | microSDのファイル `GET /sd/<ファイル名>`（`?dl=1` でダウンロード） |

操作API（ポート80）

| API | 内容 |
|---|---|
| `GET /api/status` | 状態（録画中か、fps、設定、microSDの容量、Wi-Fi、時刻など）をJSONで返す |
| `GET /api/files` | 保存したファイルの一覧（新しい順・最大300件） |
| `GET /api/snapshot` | 今の映像を1枚JPEGで返す |
| `POST /api/photo` | 写真をmicroSDに保存する |
| `POST /api/record?action=start` / `stop` | 録画の開始・停止 |
| `POST /api/control?var=<項目>&val=<値>` | カメラ設定。項目は `framesize`・`quality`（10〜63）・`vflip`・`hmirror` |
| `POST /api/flash?val=<0〜100>` | ライトの明るさ |
| `POST /api/time?epoch=<UNIX秒>` | 時計を合わせる（NTPで合わせた後は無視） |
| `POST /api/delete?name=<ファイル名>` | ファイルを削除する |

成功時は `{"ok":true,"message":"..."}`、失敗時はHTTP 400で `{"ok":false,"message":"理由"}` を返す。

Bluetooth（BLE）

デバイス名は `ESP32-CAM`。広告パケットには名前だけを載せる（128bitのUUIDと両方は入らないため）。

| 用途 | UUID |
|---|---|
| Service | `65cfeafb-2789-4da5-b58f-29f337456ab4` |
| 情報（読み出し） | `226570ef-9168-409d-9147-c84f40b6e087` |

情報を読み出すと、次のJSONを返す。

```json
{"url":"http://10.20.30.40/","mode":"STA","ssid":"Pixel_XXXX"}
```

`mode` は `STA`（Wi-Fiにつながっている）または `AP`（アクセスポイントモード。`url` は `http://192.168.4.1/`）。

## 制限・注意

- 映像を同時に見られるのは1台だけ（2台目は1台目が閉じるまで表示されない）。画面を閉じる・別アプリに切り替えると自動で配信を止め、枠を空ける
- カメラの画面はHTTP（暗号化なし）で配信するため、PWAとしてのインストールやオフライン起動はできない（入口ページはPWAにできる）
- 入口ページ（HTTPS）から映像を直接表示することはできない（HTTPSのページからHTTPの映像は読み込めないため）。入口ページはカメラの画面へ移動するだけ
- テザリングを切ったままにすると、ESP32-CAMはWi-Fiの再接続を待ち続ける（アクセスポイントモードには戻らない）。戻すには再起動する
- 動画のダウンロード中は、microSDへの書き込みと読み出しが重なるため録画のコマ数が落ちることがある
- 長時間動かすと基板がかなり熱くなる。ライトを明るくすると特に熱くなる
- 起動直後に再起動を繰り返す（シリアルに `Brownout detector was triggered` と出る）場合は電源不足。短く太いUSBケーブルを使う、PCの別のUSBポートやACアダプタにつなぐ

## ライセンス

ソースコードは [MIT License](LICENSE) で公開している（Copyright (c) 2026 sato4app）。

ファーム側が使う Arduino framework・ESP32カメラドライバ（esp32-camera）・ESP-IDFのHTTPサーバ・NimBLE-Arduino はビルド時に取得するもので、
本リポジトリには含まれない（それぞれの提供元の利用条件に従う）。
