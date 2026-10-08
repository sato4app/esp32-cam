## esp32-cam - ESP32-CAMの映像をスマホに表示

ESP32-CAMの映像をWi-Fi経由でスマホのブラウザに表示し、写真・動画をmicroSDカードに保存するアプリ。
スマホ用の画面はESP32-CAM自身が配信するため、スマホにアプリを入れる必要はない。

映像はデータ量が多くBluetooth（BLE）では送れないため、Wi-Fiで送る。
Bluetoothは、テザリングでつなぐたびに変わるアドレスをスマホに知らせることと、
Wi-Fiの接続先（SSID・パスワード）をスマホから送ることに使う（入口ページ）。

使い方と注意点の詳しい説明は [docs/使い方.md](docs/使い方.md) にまとめている。

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
| Wi-Fiの登録 | 入口ページからBluetoothでWi-Fiの接続先を送り、ESP32-CAMに保存する（ペアリング必須・電源投入後5分間のみ） |

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
index.html                       入口ページ（GitHub Pages。BluetoothでESP32-CAMを探してカメラの画面を開く・Wi-Fiを登録する）
manifest.json                    入口ページのPWA設定
service-worker.js                入口ページのオフライン起動用キャッシュ制御
icons/                           PWAアイコン（192x192 / 512x512。元データは icon.svg）
esp32-cam.code-workspace         VS Code 用（リポジトリと firmware を同時に開く）
docs/使い方.md                   使い方と注意点
firmware/                        ESP32-CAMのファーム（PlatformIOプロジェクト）
  platformio.ini                 ボード・ビルド設定
  web/index.html                 スマホ用の画面（ビルド時にファームへ埋め込む）
  src/main.cpp                   起動処理・Wi-Fi接続（接続できなければアクセスポイント）
  src/Discovery.cpp/.h           Bluetooth（アドレスの通知・Wi-Fiの設定の受け取り）
  src/WifiStore.cpp/.h           Bluetoothで受け取ったWi-Fiの接続先の保存（NVS）
  src/config.h                   端子番号・保存先・録画の分割時間などの設定
  src/secrets.example.h          暗証番号・パスワード類のひな形（secrets.h にコピーして使う）
  src/Camera.cpp/.h              カメラ制御・写真保存・録画（専用タスクで撮影）
  src/AviWriter.cpp/.h           AVI（MJPEG）ファイルの書き出し
  src/Storage.cpp/.h             microSDのマウント・ファイル名・一覧・削除
  src/AppServer.cpp/.h           Webサーバ（画面・操作API・映像・ファイル）
```

## 準備と書き込み

### 1. secrets.h を作る

`firmware/src/secrets.example.h` を同じフォルダに `secrets.h` という名前でコピーし、次の値を書く。
`secrets.h` は `.gitignore` 済みなのでリポジトリには入らない。作らないとビルドできない。

| 項目 | 内容 |
|---|---|
| `BLE_PASSKEY` | Bluetoothのペアリングに使う6桁の暗証番号（必須）。先頭を0にしない。他で使っていない番号にする |
| `AP_SSID` / `AP_PASSWORD` | アクセスポイントモードのWi-Fi名とパスワード。パスワードは他で使っていない12文字以上 |
| `WIFI_LIST` | Wi-Fiの接続先（任意）。普段は入口ページからBluetoothで送るので書かなくてよい |

```cpp
#define BLE_PASSKEY 482915
#define AP_SSID     "ESP32-CAM"
#define AP_PASSWORD "k7mq2xv9rt4p"

static const char *WIFI_LIST[][2] = {
  { "your-ssid", "your-password" },   // 書かない場合はこの行を残す
};
```

Wi-Fiの接続先は、入口ページから送ったもの（本体に最大5件保存）と `WIFI_LIST` の両方を使い、
見つかったうち電波の強いものにつなぐ。

### 2. 書き込み

1. ESP32-CAMをESP32-CAM-MBに差し、USBケーブルでPCにつなぐ
2. VS Codeで `esp32-cam.code-workspace` を開き、PlatformIOでビルド・書き込みする

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
- **つながらなかった場合（アクセスポイントモード）**: スマホのWi-Fi設定で `ESP32-CAM`（パスワードは `AP_PASSWORD`）につなぎ、`http://192.168.4.1/` を開く。
  「インターネットに接続されていません」と出ても、そのまま接続を維持する
- mDNSに対応した端末（iPhone・PCなど）は `http://esp32cam.local/` でも開ける
- **USBを外して使う場合**は、次の「入口ページ」を使うとアドレスを調べずに開ける

### 4. 入口ページ（テザリングで使う場合）

スマホのテザリングは、つなぐたびにアドレスが変わる（Android 11以降）。
そこで、ESP32-CAMがBluetoothで今のアドレスを知らせ、入口ページがそれを読み取ってカメラの画面を開く。
Wi-Fiの接続先も、入口ページからBluetoothで送って登録する。

1. GitHub Pages を有効にする（Settings → Pages → Branch: `main` / `(root)`）
2. Androidの Chrome で `https://sato4app.github.io/esp32-cam/` を開き、メニューの「ホーム画面に追加」（またはアプリをインストール）をする
3. 最初に一度だけ、テザリングを登録する
   1. スマホのテザリングをONにし、ESP32-CAMの電源を入れる（登録はここから5分以内）
   2. 入口ページの「Wi-Fiの接続先を登録」にテザリングのWi-Fi名とパスワードを入れ、「ESP32-CAMに送る」→ 一覧の「ESP32-CAM」を選ぶ
   3. 初回はペアリングの画面が出るので、`BLE_PASSKEY` の6桁を入力する
   4. ESP32-CAMがつながると、入口ページにカメラの画面のアドレスが出る
4. 使うとき: テザリングをON → ESP32-CAMの電源を入れる → 入口ページの「Bluetoothで探して開く」→ 一覧の「ESP32-CAM」を選ぶ

- 一度開いたアドレスは入口ページに「前回のアドレス」として残る
- ESP32-CAMがアクセスポイントモードの場合は、入口ページにその旨と `http://192.168.4.1/` が出る
- スマホのBluetoothと位置情報をONにしておく。SafariとFirefoxはWeb Bluetooth非対応

テザリングの設定（Pixelの場合: 設定 → ネットワークとインターネット → アクセスポイントとテザリング → Wi-Fiアクセスポイント）

- 「互換性を拡張」をONにする（OFFだと5GHz帯になり、2.4GHzにしかつながらないESP32-CAMから見えない）
- セキュリティは「WPA2-Personal」にする

テザリングを後からONにした場合も、ESP32-CAMはアクセスポイントモードのまま30秒ごとに登録したWi-Fiを探し、
見つかれば切り替わる（アクセスポイントにスマホがつながっている間は探さない）。
つながっていたWi-Fiが1分たっても戻らない場合（テザリングを切ったときなど）は、アクセスポイントモードに切り替わる。

#### Wi-Fiの登録の安全対策

| 対策 | 内容 |
|---|---|
| ペアリング必須 | 接続先の書き込みと結果の読み出しは、暗証番号（`BLE_PASSKEY`）でペアリングしたスマホだけができる。通信は暗号化される（LE Secure Connections・MITM保護） |
| 受付時間 | 接続先の追加・削除は、電源を入れてから5分間だけ受け付ける（`config.h` の `BLE_SETUP_WINDOW_MS`） |
| パスワードは書き込み専用 | 保存したパスワードはBluetoothで読み出せない（一覧はSSIDだけ） |

ペアリング情報はスマホとESP32-CAMの両方に保存され、2回目からは暗証番号の入力は不要。
ESP32-CAMを「Erase Flash」で全消去した場合は、スマホのBluetooth設定で「ESP32-CAM」を削除してからペアリングし直す。

#### Bluetoothの動き

Wi-FiとBluetoothを同時に動かしている間は、ESP32の制約でWi-Fiの省電力を切れず、映像が遅くなる。
そのため、Bluetoothは次のように動かしている。

| とき | Bluetooth |
|---|---|
| 起動時 | アドレスの通知とWi-Fiの設定の受け付けを始める（設定は5分間のみ） |
| 映像の配信が始まった | 止める（Wi-Fiの省電力を切って映像を速くする） |
| Wi-Fiにつなぎ直してアドレスが変わった・アクセスポイントモードになった | 新しいアドレスで通知を再開する |

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
| 情報（読み出し・ペアリング不要） | `226570ef-9168-409d-9147-c84f40b6e087` |
| Wi-Fiの設定（書き込み・ペアリング必須） | `f07241dd-9ed4-4847-b3fd-83871a65d4e5` |
| 設定の結果（読み出し・ペアリング必須） | `ff87d3b4-3849-4a26-be1e-9aa77e4412be` |

情報を読み出すと、次のJSONを返す。

```json
{"setup":245,"url":"http://10.20.30.40/","mode":"STA","ssid":"Pixel_XXXX"}
```

- `setup`: Wi-Fiの設定を受け付ける残り秒数（0なら受付時間外）
- `mode`: `STA`（Wi-Fiにつながっている）または `AP`（アクセスポイントモード。`url` は `http://192.168.4.1/`）

Wi-Fiの設定は、次のコマンド（UTF-8・改行区切り）を書き込み、続けて結果を読み出す。

| コマンド | 内容 |
|---|---|
| `ADD` 改行 `<SSID>` 改行 `<パスワード>` | 接続先を追加する（同じSSIDは上書き。パスワードなしのWi-Fiは空にする） |
| `DEL` 改行 `<SSID>` | 接続先を削除する |
| `LIST` | 保存している接続先の一覧を返す（受付時間外でも使える） |

```json
{"ok":true,"message":"保存しました","saved":["Pixel_XXXX"]}
```

## 制限・注意

- 映像を同時に見られるのは1台だけ（2台目は1台目が閉じるまで表示されない）。画面を閉じる・別アプリに切り替えると自動で配信を止め、枠を空ける
- カメラの画面はHTTP（暗号化なし）で配信するため、PWAとしてのインストールやオフライン起動はできない（入口ページはPWAにできる）
- 入口ページ（HTTPS）から映像を直接表示することはできない（HTTPSのページからHTTPの映像は読み込めないため）。入口ページはカメラの画面へ移動するだけ
- Wi-Fiの接続先の登録は、ESP32-CAMの電源を入れてから5分以内しかできない。映像を表示するとBluetoothが止まるので、登録は映像を見る前に行う
- 動画のダウンロード中は、microSDへの書き込みと読み出しが重なるため録画のコマ数が落ちることがある
- 長時間動かすと基板がかなり熱くなる。ライトを明るくすると特に熱くなる
- 起動直後に再起動を繰り返す（シリアルに `Brownout detector was triggered` と出る）場合は電源不足。短く太いUSBケーブルを使う、PCの別のUSBポートやACアダプタにつなぐ

## ライセンス

ソースコードは [MIT License](LICENSE) で公開している（Copyright (c) 2026 sato4app）。

ファーム側が使う Arduino framework・ESP32カメラドライバ（esp32-camera）・ESP-IDFのHTTPサーバ・NimBLE-Arduino はビルド時に取得するもので、
本リポジトリには含まれない（それぞれの提供元の利用条件に従う）。
