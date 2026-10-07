#include "Storage.h"
#include "config.h"
#include <SD_MMC.h>
#include <Preferences.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

namespace Storage {

static bool isMounted = false;
static uint64_t cachedUsed = 0;
static unsigned long cachedUsedAt = 0;
static volatile TimeSource source = TIME_NONE;

bool begin() {
  if (isMounted) {
    return true;
  }
  // 1ビットモードで使い、GPIO4（フラッシュLED）を空けておく
  if (!SD_MMC.begin(SD_MOUNT_POINT, true)) {
    Serial.println("microSDカードをマウントできません（未挿入・未フォーマットなど）");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("microSDカードが見つかりません");
    SD_MMC.end();
    return false;
  }
  if (!SD_MMC.exists(MEDIA_DIR) && !SD_MMC.mkdir(MEDIA_DIR)) {
    Serial.println("保存フォルダを作れません: " MEDIA_DIR);
    SD_MMC.end();
    return false;
  }
  isMounted = true;
  cachedUsedAt = 0;
  Serial.printf("microSD: %lluMB / %lluMB 使用中\n", usedBytes() / (1024 * 1024), totalBytes() / (1024 * 1024));
  return true;
}

bool mounted() {
  return isMounted;
}

uint64_t totalBytes() {
  return isMounted ? SD_MMC.totalBytes() : 0;
}

uint64_t usedBytes() {
  if (!isMounted) {
    return 0;
  }
  // 空き容量の計算はカードによって時間がかかるので、状態表示の問い合わせごとには行わない
  if (cachedUsedAt == 0 || millis() - cachedUsedAt > 10000) {
    cachedUsed = SD_MMC.usedBytes();
    cachedUsedAt = millis();
  }
  return cachedUsed;
}

String fullPath(const String &name) {
  return String(SD_MOUNT_POINT MEDIA_DIR "/") + name;
}

static bool exists(const String &name) {
  struct stat st;
  return stat(fullPath(name).c_str(), &st) == 0;
}

String newFileName(const char *prefix, const char *ext) {
  char base[40];
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(base, sizeof(base), "%s_%04d%02d%02d_%02d%02d%02d", prefix,
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {
    // 時刻が分からないときは電源を切っても続く連番にする
    Preferences prefs;
    prefs.begin("storage", false);
    uint32_t seq = prefs.getUInt("seq", 0) + 1;
    prefs.putUInt("seq", seq);
    prefs.end();
    snprintf(base, sizeof(base), "%s_%04lu", prefix, (unsigned long)seq);
  }

  String name = String(base) + "." + ext;
  // 同じ秒に複数枚撮ったときなど、名前が重なったら _2, _3 ... を付ける
  for (int i = 2; exists(name) && i < 100; i++) {
    name = String(base) + "_" + i + "." + ext;
  }
  return name;
}

static bool hasExt(const char *name, const char *ext) {
  size_t n = strlen(name), e = strlen(ext);
  return n > e && strcasecmp(name + n - e, ext) == 0;
}

bool isValidName(const char *name) {
  size_t len = strlen(name);
  if (len == 0 || len > 64 || name[0] == '.') {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    char c = name[i];
    if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') {
      return false;
    }
  }
  return hasExt(name, ".jpg") || hasExt(name, ".avi");
}

// 並べ替えのキー。先頭の "IMG_" / "VID_" を除いた日時部分で比べ、写真と動画を撮った順に混ぜる
static const char *sortKey(const String &name) {
  return name.length() > 4 && name[3] == '_' ? name.c_str() + 4 : name.c_str();
}

String listJson() {
  struct Entry {
    String name;
    uint32_t size;
  };
  std::vector<Entry> entries;

  if (isMounted) {
    DIR *dir = opendir(SD_MOUNT_POINT MEDIA_DIR);
    if (dir) {
      struct dirent *ent;
      while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_type == DT_DIR || !isValidName(ent->d_name)) {
          continue;
        }
        struct stat st;
        String name = ent->d_name;
        uint32_t size = stat(fullPath(name).c_str(), &st) == 0 ? st.st_size : 0;
        entries.push_back({name, size});
      }
      closedir(dir);
    }
  }

  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    return strcmp(sortKey(a.name), sortKey(b.name)) > 0;
  });
  if (entries.size() > FILE_LIST_MAX) {
    entries.resize(FILE_LIST_MAX);
  }

  // isValidName を通った名前だけなので、JSONのエスケープは不要
  String json = "[";
  for (size_t i = 0; i < entries.size(); i++) {
    if (i > 0) {
      json += ",";
    }
    json += "{\"name\":\"" + entries[i].name + "\",\"size\":" + entries[i].size + "}";
  }
  json += "]";
  return json;
}

bool remove(const char *name) {
  if (!isMounted || !isValidName(name)) {
    return false;
  }
  bool ok = unlink(fullPath(name).c_str()) == 0;
  cachedUsedAt = 0;
  return ok;
}

TimeSource timeSource() {
  return source;
}

void setTimeSource(TimeSource src) {
  source = src;
}

bool timeValid() {
  return source != TIME_NONE && time(nullptr) > 1700000000;
}

} // namespace Storage
