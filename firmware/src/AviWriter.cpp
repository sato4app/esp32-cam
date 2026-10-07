#include "AviWriter.h"
#include <esp_timer.h>
#include <unistd.h>

// ファイル構成（RIFF）
//   RIFF 'AVI '
//     LIST 'hdrl'
//       avih（56バイト）
//       LIST 'strl'
//         strh（56バイト）
//         strf（40バイト）
//     LIST 'movi'
//       '00dc' <JPEG> ...（フレームごと）
//     idx1（フレームごとに16バイト）

#define SYNC_INTERVAL_US 5000000 // 電源断に備え、この間隔でファイルの中身を確定させる

static void put32(uint8_t *&p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
  p += 4;
}

static void put16(uint8_t *&p, uint16_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p += 2;
}

static void putFourcc(uint8_t *&p, const char *cc) {
  memcpy(p, cc, 4);
  p += 4;
}

AviWriter::~AviWriter() {
  abort();
}

bool AviWriter::begin(const char *path, uint16_t w, uint16_t h, uint32_t frameLimit) {
  abort();
  frameSizes = (uint32_t *)ps_malloc(sizeof(uint32_t) * frameLimit);
  if (!frameSizes) {
    frameSizes = (uint32_t *)malloc(sizeof(uint32_t) * frameLimit);
  }
  if (!frameSizes) {
    return false;
  }
  fp = fopen(path, "wb");
  if (!fp) {
    free(frameSizes);
    frameSizes = nullptr;
    return false;
  }
  width = w;
  height = h;
  maxFrames = frameLimit;
  frameCount = 0;
  moviBytes = 0;
  maxFrameBytes = 0;
  firstFrameUs = lastFrameUs = lastSyncUs = esp_timer_get_time();

  // ヘッダは仮の値で書いておき、end() で書き直す
  uint8_t hdr[HEADER_SIZE];
  buildHeader(hdr, 100000, 0);
  if (fwrite(hdr, 1, HEADER_SIZE, fp) != HEADER_SIZE) {
    abort();
    return false;
  }
  return true;
}

bool AviWriter::addFrame(const uint8_t *jpeg, size_t len) {
  if (!fp || isFull()) {
    return false;
  }
  uint8_t chunk[8];
  uint8_t *p = chunk;
  putFourcc(p, "00dc");
  put32(p, len);
  static const uint8_t pad = 0;
  size_t padLen = len & 1; // チャンクは偶数バイトにそろえる
  if (fwrite(chunk, 1, 8, fp) != 8 ||
      fwrite(jpeg, 1, len, fp) != len ||
      (padLen && fwrite(&pad, 1, 1, fp) != 1)) {
    return false;
  }

  int64_t now = esp_timer_get_time();
  if (frameCount == 0) {
    firstFrameUs = now;
  }
  lastFrameUs = now;
  frameSizes[frameCount++] = len;
  moviBytes += 8 + len + padLen;
  if (len > maxFrameBytes) {
    maxFrameBytes = len;
  }

  if (now - lastSyncUs >= SYNC_INTERVAL_US) {
    fflush(fp);
    fsync(fileno(fp));
    lastSyncUs = now;
  }
  return true;
}

uint32_t AviWriter::elapsedMs() const {
  return (uint32_t)((lastFrameUs - firstFrameUs) / 1000);
}

bool AviWriter::end() {
  if (!fp) {
    return false;
  }
  bool ok = true;

  // 1フレームあたりの時間 = 最初から最後のフレームまでの時間 ÷ 間隔の数
  uint32_t usPerFrame = 100000;
  if (frameCount > 1) {
    usPerFrame = (uint32_t)((lastFrameUs - firstFrameUs) / (frameCount - 1));
    if (usPerFrame == 0) {
      usPerFrame = 1;
    }
  }

  // 索引（idx1）。オフセットは 'movi' の4文字の先頭から数える
  uint32_t idxBytes = frameCount * 16;
  uint8_t head[8];
  uint8_t *p = head;
  putFourcc(p, "idx1");
  put32(p, idxBytes);
  ok = ok && fwrite(head, 1, 8, fp) == 8;

  const uint32_t ENTRIES_PER_WRITE = 64;
  uint8_t entries[ENTRIES_PER_WRITE * 16];
  uint32_t offset = 4;
  for (uint32_t i = 0; ok && i < frameCount; i += ENTRIES_PER_WRITE) {
    uint32_t n = min(ENTRIES_PER_WRITE, frameCount - i);
    p = entries;
    for (uint32_t j = 0; j < n; j++) {
      uint32_t size = frameSizes[i + j];
      putFourcc(p, "00dc");
      put32(p, 0x10); // AVIIF_KEYFRAME（MJPEGは全フレームがキーフレーム）
      put32(p, offset);
      put32(p, size);
      offset += 8 + size + (size & 1);
    }
    ok = fwrite(entries, 1, n * 16, fp) == n * 16;
  }

  // 確定した値でヘッダを書き直す
  uint8_t hdr[HEADER_SIZE];
  buildHeader(hdr, usPerFrame, idxBytes);
  ok = ok && fseek(fp, 0, SEEK_SET) == 0;
  ok = ok && fwrite(hdr, 1, HEADER_SIZE, fp) == HEADER_SIZE;
  ok = (fclose(fp) == 0) && ok;
  fp = nullptr;
  free(frameSizes);
  frameSizes = nullptr;
  return ok;
}

void AviWriter::abort() {
  if (fp) {
    fclose(fp);
    fp = nullptr;
  }
  if (frameSizes) {
    free(frameSizes);
    frameSizes = nullptr;
  }
}

void AviWriter::buildHeader(uint8_t *hdr, uint32_t usPerFrame, uint32_t idxBytes) const {
  uint32_t bytesPerSec = (uint32_t)((uint64_t)maxFrameBytes * 1000000 / usPerFrame);
  uint8_t *p = hdr;

  putFourcc(p, "RIFF");
  put32(p, 4 + (8 + 192) + (12 + moviBytes) + (idxBytes ? 8 + idxBytes : 0));
  putFourcc(p, "AVI ");

  putFourcc(p, "LIST");
  put32(p, 192);
  putFourcc(p, "hdrl");

  putFourcc(p, "avih");
  put32(p, 56);
  put32(p, usPerFrame);       // dwMicroSecPerFrame
  put32(p, bytesPerSec);      // dwMaxBytesPerSec
  put32(p, 0);                // dwPaddingGranularity
  put32(p, 0x10);             // dwFlags = AVIF_HASINDEX
  put32(p, frameCount);       // dwTotalFrames
  put32(p, 0);                // dwInitialFrames
  put32(p, 1);                // dwStreams
  put32(p, maxFrameBytes);    // dwSuggestedBufferSize
  put32(p, width);
  put32(p, height);
  put32(p, 0);                // dwReserved[4]
  put32(p, 0);
  put32(p, 0);
  put32(p, 0);

  putFourcc(p, "LIST");
  put32(p, 116);
  putFourcc(p, "strl");

  putFourcc(p, "strh");
  put32(p, 56);
  putFourcc(p, "vids");       // fccType
  putFourcc(p, "MJPG");       // fccHandler
  put32(p, 0);                // dwFlags
  put16(p, 0);                // wPriority
  put16(p, 0);                // wLanguage
  put32(p, 0);                // dwInitialFrames
  put32(p, usPerFrame);       // dwScale
  put32(p, 1000000);          // dwRate（fps = dwRate / dwScale）
  put32(p, 0);                // dwStart
  put32(p, frameCount);       // dwLength
  put32(p, maxFrameBytes);    // dwSuggestedBufferSize
  put32(p, 0xFFFFFFFF);       // dwQuality（既定値）
  put32(p, 0);                // dwSampleSize
  put16(p, 0);                // rcFrame
  put16(p, 0);
  put16(p, width);
  put16(p, height);

  putFourcc(p, "strf");
  put32(p, 40);
  put32(p, 40);               // biSize
  put32(p, width);            // biWidth
  put32(p, height);           // biHeight
  put16(p, 1);                // biPlanes
  put16(p, 24);               // biBitCount
  putFourcc(p, "MJPG");       // biCompression
  put32(p, (uint32_t)width * height * 3); // biSizeImage
  put32(p, 0);                // biXPelsPerMeter
  put32(p, 0);                // biYPelsPerMeter
  put32(p, 0);                // biClrUsed
  put32(p, 0);                // biClrImportant

  putFourcc(p, "LIST");
  put32(p, 4 + moviBytes);
  putFourcc(p, "movi");
}
