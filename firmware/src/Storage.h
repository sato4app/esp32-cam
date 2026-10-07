#pragma once
#include <Arduino.h>

// microSDカードと保存ファイルの管理
namespace Storage {

bool begin();          // マウントして保存フォルダを作る（失敗時は false）
bool mounted();
uint64_t totalBytes();
uint64_t usedBytes();  // 数秒間はキャッシュした値を返す

// 新しいファイル名（例: IMG_20261007_123456.jpg）。時刻が未設定なら連番（IMG_0001.jpg）
String newFileName(const char *prefix, const char *ext);
String fullPath(const String &name); // "/sdcard/DCIM/<name>"

// ファイル名として受け付けるか（英数字・_・-・. のみ、.jpg/.avi）。パスの指定は受け付けない
bool isValidName(const char *name);

// 一覧をJSON配列で返す（[{"name":"...","size":123}, ...]、新しい順）
String listJson();
bool remove(const char *name);

// 時刻
enum TimeSource { TIME_NONE, TIME_BROWSER, TIME_NTP };
TimeSource timeSource();
void setTimeSource(TimeSource src);
bool timeValid();

} // namespace Storage
