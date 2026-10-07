#pragma once
#include <Arduino.h>
#include <stdio.h>

// MJPEG形式のAVIファイルを書き出す。
// JPEGをそのまま順に書き、終了時に索引（idx1）を付けてヘッダの総フレーム数・フレームレートを書き直す。
// フレームレートは実際に書けたフレーム数と経過時間から求めるため、撮影速度が変わっても再生時間は実時間に合う。
class AviWriter {
public:
  ~AviWriter();

  // path は "/sdcard/..." のフルパス。maxFrames はこのファイルに入れられるフレーム数の上限
  bool begin(const char *path, uint16_t width, uint16_t height, uint32_t maxFrames);
  bool addFrame(const uint8_t *jpeg, size_t len);
  bool end();       // 索引とヘッダを書いて閉じる
  void abort();     // ヘッダを書かずに閉じる（書き込みエラー時）

  bool isOpen() const { return fp != nullptr; }
  bool isFull() const { return frameCount >= maxFrames; }
  uint32_t frames() const { return frameCount; }
  uint64_t fileBytes() const { return HEADER_SIZE + (uint64_t)moviBytes; }
  uint32_t elapsedMs() const;

private:
  static const size_t HEADER_SIZE = 224;

  void buildHeader(uint8_t *hdr, uint32_t usPerFrame, uint32_t idxBytes) const;

  FILE *fp = nullptr;
  uint32_t *frameSizes = nullptr; // 各フレームのJPEGサイズ（索引の作成用）
  uint32_t maxFrames = 0;
  uint32_t frameCount = 0;
  uint32_t moviBytes = 0;         // movi リスト内のチャンクの合計バイト数
  uint32_t maxFrameBytes = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  int64_t firstFrameUs = 0;
  int64_t lastFrameUs = 0;
  int64_t lastSyncUs = 0;
};
