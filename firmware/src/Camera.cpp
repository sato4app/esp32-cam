#include "Camera.h"
#include "config.h"
#include "Storage.h"
#include "AviWriter.h"
#include <esp_camera.h>
#include <Preferences.h>

#define REC_MAX_FILE_BYTES (1000ULL * 1024 * 1024) // AVI 1ファイルの上限（古い再生ソフト向けに1GB未満にする）
#define COMMAND_TIMEOUT_MS 8000
#define CAMERA_INIT_RETRIES 3

namespace Camera {

enum CommandType { CMD_NONE, CMD_PHOTO, CMD_REC_START, CMD_REC_STOP };

// 撮影タスクへの依頼（同時に1件だけ）
static SemaphoreHandle_t commandMutex;
static SemaphoreHandle_t commandDone;
static volatile CommandType pendingCommand = CMD_NONE;
static bool commandOk;
static String commandMessage;

// 最新の画像（配信・スナップショット用）
static SemaphoreHandle_t frameMutex;
static uint8_t *latestBuf = nullptr;
static size_t latestCap = 0;
static size_t latestLen = 0;
static volatile uint32_t latestSeq = 0;

// 録画（撮影タスクだけが書き換える。表示用の文字列は stateMutex で守る）
static SemaphoreHandle_t stateMutex;
static AviWriter avi;
static volatile bool recording = false;
static char recName[48] = "";
static unsigned long recStartMs = 0;
static unsigned long fileStartMs = 0;
static uint32_t recFiles = 0;
static String errorText;

static bool cameraReady = false;
static volatile bool stopRequested = false; // end() から撮影タスクへの停止依頼
static volatile bool taskStopped = false;
static Settings current;
static int flashPercent = 0;
static volatile float currentFps = 0;

static const int ALLOWED_FRAMESIZES[] = {
  FRAMESIZE_QVGA, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_XGA,
  FRAMESIZE_HD, FRAMESIZE_SXGA, FRAMESIZE_UXGA,
};

static void setStatusLed(bool on) {
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
}

static void setError(const String &text) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  errorText = text;
  xSemaphoreGive(stateMutex);
  if (text.length() > 0) {
    Serial.println("エラー: " + text);
  }
}

static void setRecName(const String &name) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  strlcpy(recName, name.c_str(), sizeof(recName));
  xSemaphoreGive(stateMutex);
}

// ===== 撮影タスクの中だけで呼ぶ処理 =====

static void publishFrame(camera_fb_t *fb) {
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  if (latestCap < fb->len) {
    size_t newCap = fb->len + 16 * 1024;
    uint8_t *p = (uint8_t *)ps_realloc(latestBuf, newCap);
    if (p) {
      latestBuf = p;
      latestCap = newCap;
    }
  }
  if (latestCap >= fb->len) {
    memcpy(latestBuf, fb->buf, fb->len);
    latestLen = fb->len;
    latestSeq++;
  }
  xSemaphoreGive(frameMutex);
}

static bool ensureStorage(String &message) {
  if (!Storage::mounted() && !Storage::begin()) {
    message = "microSDカードが使えません（挿入・FAT32フォーマットを確認してください）";
    return false;
  }
  return true;
}

static bool savePhoto(camera_fb_t *fb, String &message) {
  if (!ensureStorage(message)) {
    return false;
  }
  String name = Storage::newFileName("IMG", "jpg");
  FILE *fp = fopen(Storage::fullPath(name).c_str(), "wb");
  if (!fp) {
    message = "ファイルを作れません";
    return false;
  }
  bool ok = fwrite(fb->buf, 1, fb->len, fp) == fb->len;
  ok = (fclose(fp) == 0) && ok;
  if (!ok) {
    Storage::remove(name.c_str());
    message = "microSDへの書き込みに失敗しました（空き容量を確認してください）";
    return false;
  }
  Serial.printf("写真を保存: %s (%u bytes)\n", name.c_str(), fb->len);
  message = name;
  return true;
}

static bool openRecordFile(camera_fb_t *fb) {
  String name = Storage::newFileName("VID", "avi");
  uint32_t maxFrames = REC_SPLIT_SECONDS * REC_MAX_FPS + REC_MAX_FPS * 10;
  if (!avi.begin(Storage::fullPath(name).c_str(), fb->width, fb->height, maxFrames)) {
    return false;
  }
  fileStartMs = millis();
  setRecName(name);
  Serial.printf("録画ファイル: %s (%ux%u)\n", name.c_str(), fb->width, fb->height);
  return true;
}

static void closeRecordFile() {
  if (!avi.isOpen()) {
    return;
  }
  uint32_t frames = avi.frames();
  uint32_t ms = avi.elapsedMs();
  bool ok = avi.end();
  Serial.printf("録画ファイルを閉じました: %lu フレーム / %.1f 秒%s\n",
                (unsigned long)frames, ms / 1000.0, ok ? "" : "（書き込みエラー）");
}

static void stopRecordingWithError(const String &text) {
  closeRecordFile();
  recording = false;
  setRecName("");
  setError(text);
}

static bool startRecordingNow(camera_fb_t *fb, String &message) {
  if (recording) {
    message = "すでに録画中です";
    return false;
  }
  if (!ensureStorage(message)) {
    return false;
  }
  if (Storage::totalBytes() - Storage::usedBytes() < REC_MIN_FREE_BYTES) {
    message = "microSDの空き容量が足りません";
    return false;
  }
  if (!openRecordFile(fb)) {
    message = "録画ファイルを作れません";
    return false;
  }
  setError("");
  recStartMs = millis();
  recFiles = 1;
  recording = true;
  message = recName;
  return true;
}

static void writeRecordFrame(camera_fb_t *fb) {
  // 一定時間・一定サイズごとに次のファイルへ切り替える
  if (millis() - fileStartMs >= REC_SPLIT_SECONDS * 1000UL || avi.isFull() ||
      avi.fileBytes() + fb->len > REC_MAX_FILE_BYTES) {
    closeRecordFile();
    if (!openRecordFile(fb)) {
      stopRecordingWithError("次の録画ファイルを作れないため録画を止めました");
      return;
    }
    recFiles++;
  }
  if (!avi.addFrame(fb->buf, fb->len)) {
    stopRecordingWithError("microSDへの書き込みに失敗したため録画を止めました（空き容量を確認してください）");
  }
}

static bool runCommandInTask(CommandType type, camera_fb_t *fb, String &message) {
  if (type == CMD_REC_STOP) {
    if (recording) {
      closeRecordFile();
      recording = false;
      setRecName("");
    }
    message = "";
    return true;
  }
  if (!fb) {
    message = "カメラから画像を取得できません";
    return false;
  }
  if (type == CMD_PHOTO) {
    return savePhoto(fb, message);
  }
  return startRecordingNow(fb, message);
}

static void cameraTask(void *) {
  uint32_t frames = 0;
  unsigned long fpsStartMs = millis();

  for (;;) {
    if (stopRequested) {
      setStatusLed(false);
      taskStopped = true;
      vTaskSuspend(nullptr);
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      publishFrame(fb);
      frames++;
    }

    CommandType type = pendingCommand;
    if (type != CMD_NONE) {
      commandOk = runCommandInTask(type, fb, commandMessage);
      pendingCommand = CMD_NONE;
      xSemaphoreGive(commandDone);
    }

    if (fb) {
      if (recording) {
        writeRecordFrame(fb);
      }
      esp_camera_fb_return(fb);
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    unsigned long now = millis();
    if (now - fpsStartMs >= 1000) {
      currentFps = frames * 1000.0f / (now - fpsStartMs);
      frames = 0;
      fpsStartMs = now;
    }
    // 録画中は赤色LEDを0.5秒ごとに点滅させる
    setStatusLed(recording && (now / 500) % 2 == 0);
  }
}

// ===== 他のタスク（Webサーバ）から呼ぶ処理 =====

static bool runCommand(CommandType type, String &message) {
  if (!cameraReady) {
    message = "カメラが使えません（起動時の初期化に失敗しています）";
    return false;
  }
  xSemaphoreTake(commandMutex, portMAX_DELAY);
  xSemaphoreTake(commandDone, 0); // 前回タイムアウトした分の通知が残っていれば捨てる
  pendingCommand = type;
  bool ok;
  if (xSemaphoreTake(commandDone, pdMS_TO_TICKS(COMMAND_TIMEOUT_MS)) == pdTRUE) {
    ok = commandOk;
    message = commandMessage;
  } else {
    pendingCommand = CMD_NONE;
    ok = false;
    message = "カメラの応答がありません";
  }
  xSemaphoreGive(commandMutex);
  return ok;
}

static void loadSettings() {
  Preferences prefs;
  prefs.begin("camera", true);
  current.framesize = prefs.getInt("framesize", DEFAULT_FRAMESIZE);
  current.quality = prefs.getInt("quality", DEFAULT_QUALITY);
  current.vflip = prefs.getBool("vflip", false);
  current.hmirror = prefs.getBool("hmirror", false);
  prefs.end();
}

static void saveSettings() {
  Preferences prefs;
  prefs.begin("camera", false);
  prefs.putInt("framesize", current.framesize);
  prefs.putInt("quality", current.quality);
  prefs.putBool("vflip", current.vflip);
  prefs.putBool("hmirror", current.hmirror);
  prefs.end();
}

static bool framesizeAllowed(int size) {
  // PSRAMが無い基板では大きな画像を扱えない
  if (!psramFound() && size > FRAMESIZE_SVGA) {
    return false;
  }
  for (int allowed : ALLOWED_FRAMESIZES) {
    if (allowed == size) {
      return true;
    }
  }
  return false;
}

static String initErrorText(esp_err_t err) {
  char code[16];
  snprintf(code, sizeof(code), "0x%x", err);
  switch (err) {
    case ESP_ERR_CAMERA_NOT_DETECTED:
    case ESP_ERR_NOT_FOUND:
      return String("カメラが見つかりません（") + code + "）。カメラのフラットケーブルがコネクタの奥まで差さり、黒いロックが閉じているか確認してください";
    case ESP_ERR_CAMERA_NOT_SUPPORTED:
      return String("対応していないカメラです（") + code + "）";
    case ESP_ERR_NO_MEM:
      return String("カメラ用のメモリが足りません（") + code + "）。PSRAM付きの基板か確認してください";
    default:
      return String("カメラの初期化に失敗しました（") + code + "）";
  }
}

bool begin() {
  commandMutex = xSemaphoreCreateMutex();
  commandDone = xSemaphoreCreateBinary();
  frameMutex = xSemaphoreCreateMutex();
  stateMutex = xSemaphoreCreateMutex();

  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(false);
  ledcSetup(FLASH_LED_CHANNEL, 5000, 8);
  ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);
  setFlash(0);

  loadSettings();
  if (!framesizeAllowed(current.framesize)) {
    current.framesize = DEFAULT_FRAMESIZE;
  }
  current.quality = constrain(current.quality, 10, 63);

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.jpeg_quality = current.quality;
  if (psramFound()) {
    // 後から最大解像度に変えられるよう、最大サイズでバッファを確保しておく
    config.frame_size = FRAMESIZE_UXGA;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    Serial.println("PSRAMが見つかりません。解像度はSVGAまでに制限します");
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  // 電源投入直後はカメラが応答しないことがあるので、電源を入れ直して数回試す
  esp_err_t err = ESP_FAIL;
  for (int attempt = 1; attempt <= CAMERA_INIT_RETRIES; attempt++) {
    err = esp_camera_init(&config);
    if (err == ESP_OK) {
      break;
    }
    Serial.printf("カメラの初期化に失敗（%d/%d回目）: 0x%x\n", attempt, CAMERA_INIT_RETRIES, err);
    // 失敗時の後片付けは esp_camera_init の中で済んでいる
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH); // カメラの電源を切る
    delay(200);
    digitalWrite(PWDN_GPIO_NUM, LOW);
    delay(300);
  }
  if (err != ESP_OK) {
    String reason = initErrorText(err);
    Serial.println(reason);
    setError(reason);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  Serial.printf("カメラ: センサーID 0x%02X（OV2640は 0x26）\n", s->id.PID);
  s->set_framesize(s, (framesize_t)current.framesize);
  s->set_quality(s, current.quality);
  s->set_vflip(s, current.vflip);
  s->set_hmirror(s, current.hmirror);

  cameraReady = true;
  xTaskCreatePinnedToCore(cameraTask, "camera", 8192, nullptr, 3, nullptr, 1);
  return true;
}

bool ready() {
  return cameraReady;
}

void end() {
  setFlash(0);
  if (cameraReady) {
    cameraReady = false;
    stopRequested = true;
    // 撮影タスクが画像を返して止まるのを待ってから、カメラを解放する
    unsigned long start = millis();
    while (!taskStopped && millis() - start < 3000) {
      delay(10);
    }
    if (taskStopped) {
      esp_camera_deinit();
    }
  }
  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, HIGH); // カメラの電源を切る
  digitalWrite(STATUS_LED_PIN, HIGH); // 赤色LEDを消す
}

size_t copyLatest(uint8_t **buf, size_t *cap, uint32_t *seq, uint32_t timeoutMs) {
  unsigned long start = millis();
  for (;;) {
    size_t len = 0;
    xSemaphoreTake(frameMutex, portMAX_DELAY);
    if (latestLen > 0 && latestSeq != *seq) {
      if (*cap < latestLen) {
        size_t newCap = latestLen + 16 * 1024;
        uint8_t *p = (uint8_t *)ps_realloc(*buf, newCap);
        if (p) {
          *buf = p;
          *cap = newCap;
        }
      }
      if (*cap >= latestLen) {
        memcpy(*buf, latestBuf, latestLen);
        len = latestLen;
        *seq = latestSeq;
      }
    }
    xSemaphoreGive(frameMutex);

    if (len > 0) {
      return len;
    }
    if (millis() - start >= timeoutMs) {
      return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool takePhoto(String &message) {
  return runCommand(CMD_PHOTO, message);
}

bool startRecording(String &message) {
  return runCommand(CMD_REC_START, message);
}

bool stopRecording(String &message) {
  return runCommand(CMD_REC_STOP, message);
}

RecordStatus recordStatus() {
  RecordStatus st;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  st.recording = recording;
  strlcpy(st.fileName, recName, sizeof(st.fileName));
  st.seconds = recording ? (millis() - recStartMs) / 1000 : 0;
  st.files = recording ? recFiles : 0;
  xSemaphoreGive(stateMutex);
  return st;
}

float fps() {
  return cameraReady ? currentFps : 0;
}

String lastError() {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  String text = errorText;
  xSemaphoreGive(stateMutex);
  return text;
}

Settings settings() {
  return current;
}

bool setControl(const char *var, int val, String &message) {
  if (!cameraReady) {
    message = "カメラが使えません";
    return false;
  }
  sensor_t *s = esp_camera_sensor_get();
  int res = -1;
  if (strcmp(var, "framesize") == 0) {
    if (recording) {
      message = "録画中は解像度を変えられません";
      return false;
    }
    if (!framesizeAllowed(val)) {
      message = "この解像度は使えません";
      return false;
    }
    res = s->set_framesize(s, (framesize_t)val);
    if (res == 0) current.framesize = val;
  } else if (strcmp(var, "quality") == 0) {
    if (val < 10 || val > 63) {
      message = "画質は10〜63で指定してください";
      return false;
    }
    res = s->set_quality(s, val);
    if (res == 0) current.quality = val;
  } else if (strcmp(var, "vflip") == 0) {
    res = s->set_vflip(s, val ? 1 : 0);
    if (res == 0) current.vflip = val != 0;
  } else if (strcmp(var, "hmirror") == 0) {
    res = s->set_hmirror(s, val ? 1 : 0);
    if (res == 0) current.hmirror = val != 0;
  } else {
    message = "不明な設定項目です";
    return false;
  }
  if (res != 0) {
    message = "カメラが設定を受け付けませんでした";
    return false;
  }
  saveSettings();
  return true;
}

void setFlash(int percent) {
  flashPercent = constrain(percent, 0, 100);
  ledcWrite(FLASH_LED_CHANNEL, flashPercent * 255 / 100);
}

int flash() {
  return flashPercent;
}

} // namespace Camera
