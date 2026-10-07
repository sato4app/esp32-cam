#include "WifiStore.h"
#include "config.h"
#include <Preferences.h>
#include <mutex>

namespace WifiStore {

// Bluetoothのタスクとloop()の両方から呼ばれるので、NVSの読み書きを1つずつにする
static std::mutex storeMutex;

static const char *NVS_NAMESPACE = "wifinet";

static std::vector<Network> loadLocked() {
  std::vector<Network> list;
  Preferences prefs;
  // 読み取り専用で開くと、一度も保存していないときにエラーが表示されるので書き込み可で開く
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return list;
  }
  int count = prefs.getInt("count", 0);
  for (int i = 0; i < count && i < WIFI_STORE_MAX; i++) {
    char ssidKey[8], passKey[8];
    snprintf(ssidKey, sizeof(ssidKey), "s%d", i);
    snprintf(passKey, sizeof(passKey), "p%d", i);
    Network net = {prefs.getString(ssidKey, ""), prefs.getString(passKey, "")};
    if (net.ssid.length() > 0) {
      list.push_back(net);
    }
  }
  prefs.end();
  return list;
}

static bool saveLocked(const std::vector<Network> &list) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    return false;
  }
  prefs.clear();
  bool ok = true;
  for (size_t i = 0; i < list.size(); i++) {
    char ssidKey[8], passKey[8];
    snprintf(ssidKey, sizeof(ssidKey), "s%d", (int)i);
    snprintf(passKey, sizeof(passKey), "p%d", (int)i);
    ok = ok && prefs.putString(ssidKey, list[i].ssid) > 0;
    // パスワードなし（暗号化なしのWi-Fi）は空文字。putString は0を返すので成否を見ない
    prefs.putString(passKey, list[i].password);
  }
  ok = ok && prefs.putInt("count", list.size()) > 0;
  prefs.end();
  return ok;
}

static bool hasControlChar(const String &s) {
  for (size_t i = 0; i < s.length(); i++) {
    if ((uint8_t)s[i] < 0x20 || s[i] == 0x7F) {
      return true;
    }
  }
  return false;
}

std::vector<Network> load() {
  std::lock_guard<std::mutex> lock(storeMutex);
  return loadLocked();
}

bool add(const String &ssid, const String &password, String &message) {
  // バイト数で確認する（日本語のSSIDは1文字3バイト）
  if (ssid.length() == 0 || ssid.length() > 32 || hasControlChar(ssid)) {
    message = "SSIDは1〜32バイトで入力してください";
    return false;
  }
  if ((password.length() > 0 && password.length() < 8) || password.length() > 63 || hasControlChar(password)) {
    message = "パスワードは8〜63文字で入力してください";
    return false;
  }

  std::lock_guard<std::mutex> lock(storeMutex);
  std::vector<Network> list = loadLocked();
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i].ssid == ssid) {
      list.erase(list.begin() + i);
      break;
    }
  }
  if (list.size() >= WIFI_STORE_MAX) {
    list.erase(list.begin()); // 最も古いものを消す
  }
  list.push_back({ssid, password});
  if (!saveLocked(list)) {
    message = "保存できませんでした";
    return false;
  }
  message = "保存しました";
  return true;
}

bool remove(const String &ssid) {
  std::lock_guard<std::mutex> lock(storeMutex);
  std::vector<Network> list = loadLocked();
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i].ssid == ssid) {
      list.erase(list.begin() + i);
      return saveLocked(list);
    }
  }
  return false;
}

String ssidListJson() {
  std::vector<Network> list = load();
  String json = "[";
  for (size_t i = 0; i < list.size(); i++) {
    if (i > 0) {
      json += ",";
    }
    String ssid = list[i].ssid;
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    json += "\"" + ssid + "\"";
  }
  json += "]";
  return json;
}

} // namespace WifiStore
