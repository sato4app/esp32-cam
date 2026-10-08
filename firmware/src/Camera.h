#pragma once
#include <Arduino.h>

// カメラの撮影・写真保存・録画を受け持つ。
// カメラから画像を取り出すのは専用タスク1つだけにし、配信・写真・録画はそのタスクが取った画像を使う。
namespace Camera {

struct Settings {
  int framesize; // framesize_t の値
  int quality;   // JPEG画質 10〜63
  bool vflip;
  bool hmirror;
};

struct RecordStatus {
  bool recording;
  char fileName[48];   // 録画中のファイル名
  uint32_t seconds;    // 録画開始からの秒数（分割をまたいで通算）
  uint32_t files;      // 今回の録画で作ったファイル数
};

bool begin();        // カメラを初期化し、撮影タスクを開始する
bool ready();
void end();          // 撮影タスクを止め、カメラの電源を切る（終了するとき用。録画は先に止めておく）

// 最新の画像を *buf にコピーする（足りなければ PSRAM に確保し直す）。
// *seq と異なる新しい画像が来るまで最大 timeoutMs 待つ。戻り値は画像のバイト数（0は失敗）
size_t copyLatest(uint8_t **buf, size_t *cap, uint32_t *seq, uint32_t timeoutMs);

// 撮影タスクに頼んで実行する（結果が出るまで待つ）。失敗時は message にエラー文
bool takePhoto(String &message);      // 成功時 message はファイル名
bool startRecording(String &message); // 成功時 message はファイル名
bool stopRecording(String &message);

RecordStatus recordStatus();
float fps();               // 直近1秒の撮影フレームレート
String lastError();        // 録画が異常終了したときなどの理由（なければ空）

Settings settings();
// var: framesize / quality / vflip / hmirror。録画中は解像度を変えられない
bool setControl(const char *var, int val, String &message);

void setFlash(int percent); // 0〜100
int flash();

} // namespace Camera
