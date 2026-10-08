#pragma once
#include <Arduino.h>

// Bluetooth（BLE）でスマホとやり取りする。
//   ・今のアドレスを知らせる（ペアリング不要）
//   ・Wi-Fiの接続先（SSID・パスワード）を受け取り、本体に保存する（ペアリング必須・電源投入後5分間のみ）
// 入口ページ（GitHub Pages の index.html）が Web Bluetooth で使う。
namespace Discovery {

void setPasskey(uint32_t passkey); // ペアリングの6桁の暗証番号。begin()・announce() より前に呼ぶ

// Wi-Fiの接続が決まるのを待たずにBluetoothを始める。WiFi.mode() の後に呼ぶ。
// announce() を呼ぶまでは、情報の読み出しに {"mode":"WAIT"}（Wi-Fiに接続中）を返す
void begin();

// 知らせる内容（JSON）を設定する。Bluetoothが止まっていれば起動する。
// loop() と同じタスクから呼ぶこと
void announce(const String &infoJson);

// 映像の配信が始まったときに呼ぶ（どのタスクからでもよい）。次の loop() でBluetoothを止める
void onViewerConnected();

// Wi-Fiの接続先が追加・削除されたら一度だけ true を返す
bool consumeWifiChanged();

void end(); // 終了するときに呼ぶ。Bluetoothを止める

void loop();
bool active();

} // namespace Discovery
