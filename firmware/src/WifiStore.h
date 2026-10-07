#pragma once
#include <Arduino.h>
#include <vector>

// Bluetoothで受け取ったWi-Fiの接続先を本体（NVS）に保存する。
// 電源を切っても、ファームを書き込み直しても残る（Erase Flash では消える）
namespace WifiStore {

struct Network {
  String ssid;
  String password;
};

std::vector<Network> load();

// 同じSSIDがあれば上書きする。いっぱいなら最も古いものを消す。失敗時は message に理由
bool add(const String &ssid, const String &password, String &message);
bool remove(const String &ssid);

// 保存しているSSIDの一覧（JSON配列。パスワードは含めない）
String ssidListJson();

} // namespace WifiStore
