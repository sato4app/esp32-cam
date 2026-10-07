#include "AppServer.h"
#include "Camera.h"
#include "Storage.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>
#include <esp_camera.h>
#include <sys/time.h>

// ファームに埋め込んだ画面（platformio.ini の board_build.embed_txtfiles）
extern const char index_html_start[] asm("_binary_web_index_html_start");
extern const char index_html_end[] asm("_binary_web_index_html_end");

#define STREAM_BOUNDARY "frame"
#define FILE_CHUNK_SIZE (16 * 1024)

namespace AppServer {

static volatile int streamClients = 0;

// ===== 共通 =====

static String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if ((uint8_t)c < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

static esp_err_t sendJson(httpd_req_t *req, const String &json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t sendResult(httpd_req_t *req, bool ok, const String &message) {
  if (!ok) {
    httpd_resp_set_status(req, "400 Bad Request");
  }
  return sendJson(req, String("{\"ok\":") + (ok ? "true" : "false") +
                       ",\"message\":\"" + jsonEscape(message) + "\"}");
}

// クエリ文字列 ?key=value の値を取り出す
static bool queryValue(httpd_req_t *req, const char *key, char *out, size_t outLen) {
  char query[160];
  size_t len = httpd_req_get_url_query_len(req);
  if (len == 0 || len >= sizeof(query)) {
    return false;
  }
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, out, outLen) == ESP_OK;
}

static bool queryInt(httpd_req_t *req, const char *key, long *out) {
  char value[24];
  if (!queryValue(req, key, value, sizeof(value))) {
    return false;
  }
  char *end;
  *out = strtol(value, &end, 10);
  return end != value && *end == '\0';
}

// ===== ポート80: 画面と操作API =====

static esp_err_t indexHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  // 埋め込みテキストは末尾に終端文字が付いているので、その分を除く
  return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);
}

static esp_err_t statusHandler(httpd_req_t *req) {
  Camera::RecordStatus rec = Camera::recordStatus();
  Camera::Settings cs = Camera::settings();
  bool ap = WiFi.getMode() == WIFI_AP;

  char timeText[24] = "";
  if (Storage::timeValid()) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    strftime(timeText, sizeof(timeText), "%Y-%m-%d %H:%M:%S", &t);
  }
  static const char *TIME_SOURCES[] = {"none", "browser", "ntp"};

  char json[1024];
  snprintf(json, sizeof(json),
           "{\"camera\":%s,\"recording\":%s,\"recName\":\"%s\",\"recSec\":%lu,\"recFiles\":%lu,"
           "\"fps\":%.1f,\"framesize\":%d,\"quality\":%d,\"vflip\":%d,\"hmirror\":%d,\"flash\":%d,"
           "\"psram\":%s,\"sd\":%s,\"sdTotal\":%llu,\"sdUsed\":%llu,"
           "\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,"
           "\"time\":\"%s\",\"timeSource\":\"%s\",\"streamClients\":%d,\"uptime\":%lu,"
           "\"error\":\"%s\"}",
           Camera::ready() ? "true" : "false", rec.recording ? "true" : "false", rec.fileName,
           (unsigned long)rec.seconds, (unsigned long)rec.files,
           Camera::fps(), cs.framesize, cs.quality, cs.vflip, cs.hmirror, Camera::flash(),
           psramFound() ? "true" : "false", Storage::mounted() ? "true" : "false",
           Storage::totalBytes(), Storage::usedBytes(),
           ap ? "AP" : "STA", jsonEscape(ap ? WiFi.softAPSSID() : WiFi.SSID()).c_str(),
           (ap ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(), ap ? 0 : WiFi.RSSI(),
           timeText, TIME_SOURCES[Storage::timeSource()], streamClients, millis() / 1000,
           jsonEscape(Camera::lastError()).c_str());
  return sendJson(req, json);
}

static esp_err_t photoHandler(httpd_req_t *req) {
  String message;
  bool ok = Camera::takePhoto(message);
  return sendResult(req, ok, message);
}

static esp_err_t recordHandler(httpd_req_t *req) {
  char action[8];
  if (!queryValue(req, "action", action, sizeof(action))) {
    return sendResult(req, false, "action を指定してください");
  }
  String message;
  bool ok;
  if (strcmp(action, "start") == 0) {
    ok = Camera::startRecording(message);
  } else if (strcmp(action, "stop") == 0) {
    ok = Camera::stopRecording(message);
  } else {
    return sendResult(req, false, "action は start か stop です");
  }
  return sendResult(req, ok, message);
}

static esp_err_t controlHandler(httpd_req_t *req) {
  char var[16];
  long val;
  if (!queryValue(req, "var", var, sizeof(var)) || !queryInt(req, "val", &val)) {
    return sendResult(req, false, "var と val を指定してください");
  }
  String message;
  bool ok = Camera::setControl(var, val, message);
  return sendResult(req, ok, message);
}

static esp_err_t flashHandler(httpd_req_t *req) {
  long val;
  if (!queryInt(req, "val", &val) || val < 0 || val > 100) {
    return sendResult(req, false, "val は0〜100で指定してください");
  }
  Camera::setFlash(val);
  return sendResult(req, true, "");
}

// ブラウザの時刻を受け取る（NTPで合わせられないアクセスポイントモード用）
static esp_err_t timeHandler(httpd_req_t *req) {
  long epoch;
  if (!queryInt(req, "epoch", &epoch) || epoch < 1700000000) {
    return sendResult(req, false, "epoch を指定してください");
  }
  if (Storage::timeSource() != Storage::TIME_NTP) {
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
    if (Storage::timeSource() == Storage::TIME_NONE) {
      Serial.println("ブラウザの時刻で時計を合わせました");
    }
    Storage::setTimeSource(Storage::TIME_BROWSER);
  }
  return sendResult(req, true, "");
}

static esp_err_t filesHandler(httpd_req_t *req) {
  return sendJson(req, String("{\"files\":") + Storage::listJson() + "}");
}

static esp_err_t deleteHandler(httpd_req_t *req) {
  char name[72];
  if (!queryValue(req, "name", name, sizeof(name)) || !Storage::isValidName(name)) {
    return sendResult(req, false, "ファイル名が正しくありません");
  }
  if (strcmp(name, Camera::recordStatus().fileName) == 0) {
    return sendResult(req, false, "録画中のファイルは削除できません");
  }
  bool ok = Storage::remove(name);
  return sendResult(req, ok, ok ? "" : "削除できませんでした");
}

// 最新の画像を1枚返す（スマホに保存する用）
static esp_err_t snapshotHandler(httpd_req_t *req) {
  uint8_t *buf = nullptr;
  size_t cap = 0;
  uint32_t seq = 0;
  size_t len = Camera::copyLatest(&buf, &cap, &seq, 3000);
  if (len == 0) {
    free(buf);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  esp_err_t res = httpd_resp_send(req, (const char *)buf, len);
  free(buf);
  return res;
}

// ===== ポート81: ライブ映像 =====

static esp_err_t streamHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  uint8_t *buf = nullptr;
  size_t cap = 0;
  uint32_t seq = 0;
  esp_err_t res = ESP_OK;
  streamClients++;
  Serial.println("映像の配信を開始");

  while (res == ESP_OK) {
    size_t len = Camera::copyLatest(&buf, &cap, &seq, 5000);
    if (len == 0) {
      res = ESP_FAIL; // カメラが止まっている
      break;
    }
    char part[96];
    int partLen = snprintf(part, sizeof(part),
                           "\r\n--" STREAM_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                           (unsigned)len);
    res = httpd_resp_send_chunk(req, part, partLen);
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)buf, len);
    }
  }

  free(buf);
  streamClients--;
  Serial.println("映像の配信を終了");
  return res;
}

// ===== ポート82: microSDのファイル =====
// 大きな動画のダウンロード中も、画面の操作と映像が止まらないよう別サーバにしている

static esp_err_t fileHandler(httpd_req_t *req) {
  // URI は /sd/<ファイル名>[?dl=1]
  const char *p = req->uri + strlen("/sd/");
  char name[72];
  size_t n = strcspn(p, "?");
  if (n == 0 || n >= sizeof(name)) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
  }
  memcpy(name, p, n);
  name[n] = '\0';
  if (!Storage::isValidName(name)) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
  }
  if (strcmp(name, Camera::recordStatus().fileName) == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recording");
    return ESP_FAIL;
  }

  FILE *fp = Storage::mounted() ? fopen(Storage::fullPath(name).c_str(), "rb") : nullptr;
  if (!fp) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    return ESP_FAIL;
  }
  char *chunk = (char *)malloc(FILE_CHUNK_SIZE);
  if (!chunk) {
    fclose(fp);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    return ESP_FAIL;
  }

  bool isJpeg = strcasecmp(name + n - 4, ".jpg") == 0;
  httpd_resp_set_type(req, isJpeg ? "image/jpeg" : "video/x-msvideo");
  char disposition[100];
  char dl[4];
  if (queryValue(req, "dl", dl, sizeof(dl))) {
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"%s\"", name);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
  }
  // 保存済みのファイルは書き換えないので、ブラウザにキャッシュさせる
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");

  esp_err_t res = ESP_OK;
  size_t readLen;
  while (res == ESP_OK && (readLen = fread(chunk, 1, FILE_CHUNK_SIZE, fp)) > 0) {
    res = httpd_resp_send_chunk(req, chunk, readLen);
  }
  fclose(fp);
  free(chunk);
  if (res == ESP_OK) {
    res = httpd_resp_send_chunk(req, nullptr, 0);
  }
  return res;
}

// ===== 起動 =====

static httpd_handle_t startServer(uint16_t port, uint16_t ctrlPort, uint16_t maxSockets,
                                  const httpd_uri_t *uris, size_t count) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = port;
  config.ctrl_port = ctrlPort;
  config.max_open_sockets = maxSockets;
  config.max_uri_handlers = count;
  config.stack_size = 8192;
  config.lru_purge_enable = true; // 古い接続を閉じて新しい接続を受け付ける
  config.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    Serial.printf("Webサーバ（ポート%u）を起動できません\n", port);
    return nullptr;
  }
  for (size_t i = 0; i < count; i++) {
    httpd_register_uri_handler(server, &uris[i]);
  }
  return server;
}

void begin() {
  static const httpd_uri_t mainUris[] = {
    {"/", HTTP_GET, indexHandler, nullptr},
    {"/api/status", HTTP_GET, statusHandler, nullptr},
    {"/api/files", HTTP_GET, filesHandler, nullptr},
    {"/api/snapshot", HTTP_GET, snapshotHandler, nullptr},
    {"/api/photo", HTTP_POST, photoHandler, nullptr},
    {"/api/record", HTTP_POST, recordHandler, nullptr},
    {"/api/control", HTTP_POST, controlHandler, nullptr},
    {"/api/flash", HTTP_POST, flashHandler, nullptr},
    {"/api/time", HTTP_POST, timeHandler, nullptr},
    {"/api/delete", HTTP_POST, deleteHandler, nullptr},
  };
  static const httpd_uri_t streamUris[] = {
    {"/stream", HTTP_GET, streamHandler, nullptr},
  };
  static const httpd_uri_t fileUris[] = {
    {"/sd/*", HTTP_GET, fileHandler, nullptr},
  };

  // LWIPのソケット上限（16）に収まるよう、サーバごとの同時接続数を絞る
  startServer(80, 32768, 4, mainUris, sizeof(mainUris) / sizeof(mainUris[0]));
  startServer(81, 32769, 2, streamUris, 1);
  startServer(82, 32770, 2, fileUris, 1);
}

} // namespace AppServer
