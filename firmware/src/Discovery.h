#pragma once
#include <Arduino.h>

// Bluetooth（BLE）で今のアドレスをスマホに知らせる。
// 入口ページ（GitHub Pages の index.html）が Web Bluetooth で読み出し、カメラの画面へ移動する。
namespace Discovery {

// 知らせる内容（JSON）を設定する。Bluetoothが止まっていれば起動する。
// loop() と同じタスクから呼ぶこと
void announce(const String &infoJson);

// 映像の配信が始まったときに呼ぶ（どのタスクからでもよい）。次の loop() でBluetoothを止める
void onViewerConnected();

void loop();
bool active();

} // namespace Discovery
