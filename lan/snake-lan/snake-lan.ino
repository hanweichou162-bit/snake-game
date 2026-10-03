// snake-lan.ino — XIAO ESP32S3 贪吃蛇局域网联机服务器（v1.4.0）
//
// 功能：
//   USB 串口配网（不开 Wi-Fi 热点）/ HTTP(80) 游戏网页 /
//   WebSocket(81) 房间配对＋消息原样转发 / /update Wi-Fi 无线更新（要密码）
//
// 配网：见 lan/README.md 第 4 节。密码在串口输入时不回显、不写日志，只存 NVS。
// 房间：最多 4 个房间，每房 2 人，共 8 个并发 WebSocket 连接（硬上限）。

#define WEBSOCKETS_SERVER_CLIENT_MAX 8

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "webpage.h"

#define FW_VERSION "v1.4.0"
#define MAX_ROOMS 4
#define ROOM_CODE_LEN 6

Preferences prefs;
WebServer http(80);
WebSocketsServer ws(81);

String cfgSsid, cfgPass, cfgOta;

struct Room {
  String code;
  int8_t client[2]; // WebSocket 客户端编号，-1 为空
};
Room rooms[MAX_ROOMS];
int8_t clientRoom[WEBSOCKETS_SERVER_CLIENT_MAX];
int8_t clientSlot[WEBSOCKETS_SERVER_CLIENT_MAX];

// ---------- 串口（USB）配网：密码不回显 ----------
String readLine(bool echo) {
  String s = "";
  while (true) {
    while (!Serial.available()) { delay(10); yield(); }
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (echo) Serial.println();
      while (Serial.available()) {
        char d = (char)Serial.peek();
        if (d == '\r' || d == '\n') Serial.read();
        else break;
      }
      return s;
    }
    if (c == 127 || c == 8) { // 退格
      if (s.length() > 0) {
        s.remove(s.length() - 1);
        if (echo) Serial.print("\b \b");
      }
      continue;
    }
    if (c >= 32 && c < 127) {
      s += c;
      if (echo) Serial.print(c); // 密码 echo=false，不回显
    }
  }
}

void printHelp() {
  Serial.println("可用指令：config=重新配网 / status=查看状态 / help=本帮助");
}

void serialProvision() {
  Serial.println("\n=== 贪吃蛇联机配网（USB 串口）===");
  Serial.print("Wi-Fi name: ");
  String ssid = readLine(true);
  Serial.print("Wi-Fi password: ");
  String pass = readLine(false);
  Serial.println("[已隐藏]");
  Serial.print("OTA password: ");
  String ota = readLine(false);
  Serial.println("[已隐藏]");
  if (ssid.length() == 0 || pass.length() == 0 || ota.length() == 0) {
    Serial.println("输入不能为空，配网取消。");
    return;
  }
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("ota", ota);
  // 注意：密码值永不打印、不写日志
  Serial.println("已保存，重启中…");
  delay(800);
  ESP.restart();
}

void serialCommands() {
  if (!Serial.available()) return;
  String cmd = readLine(true);
  cmd.trim();
  if (cmd == "config") {
    serialProvision();
    cfgSsid = prefs.getString("ssid", "");
    cfgPass = prefs.getString("pass", "");
    cfgOta = prefs.getString("ota", "");
  } else if (cmd == "status") {
    Serial.print("Wi-Fi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "已连接" : "未连接");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP().toString());
    Serial.print("版本: ");
    Serial.println(FW_VERSION);
  } else if (cmd == "help") {
    printHelp();
  } else if (cmd.length() > 0) {
    Serial.println("未知指令，输入 help 查看。");
  }
}

// ---------- WebSocket 房间（哑转发：只配对＋原样中继） ----------
void wsSend(uint8_t num, const String &s) {
  String tmp = s; // sendTXT 只要非 const 引用（String&），转一份再发
  ws.sendTXT(num, tmp);
}

int8_t findRoom(const String &code) {
  for (int8_t i = 0; i < MAX_ROOMS; i++)
    if (rooms[i].code == code) return i;
  return -1;
}

void leaveRoom(uint8_t num) {
  int8_t r = clientRoom[num];
  if (r < 0 || r >= MAX_ROOMS) return;
  int8_t slot = clientSlot[num];
  int8_t peer = (slot == 0) ? rooms[r].client[1] : rooms[r].client[0];
  rooms[r].client[slot] = -1;
  clientRoom[num] = -1;
  clientSlot[num] = -1;
  if (peer >= 0) {
    wsSend(peer, "{\"t\":\"left\"}");
    // 对方也清掉，房间解散（两个人各回大厅）
    clientRoom[peer] = -1;
    clientSlot[peer] = -1;
    rooms[r].client[0] = rooms[r].client[1] = -1;
    rooms[r].code = "";
  } else {
    // 房间空了就回收
    if (rooms[r].client[0] < 0 && rooms[r].client[1] < 0) rooms[r].code = "";
  }
}

void handleJoin(uint8_t num, const String &code) {
  if (code.length() != ROOM_CODE_LEN) return;
  leaveRoom(num); // 先退旧房间（重进/换房）
  int8_t r = findRoom(code);
  if (r < 0) {
    // 建新房间
    for (int8_t i = 0; i < MAX_ROOMS; i++) {
      if (rooms[i].code == "") { r = i; break; }
    }
    if (r < 0) { wsSend(num, "{\"t\":\"full\"}"); return; } // 房间数到上限
    rooms[r].code = code;
    rooms[r].client[0] = rooms[r].client[1] = -1;
  }
  int8_t slot = -1;
  if (rooms[r].client[0] < 0) slot = 0;
  else if (rooms[r].client[1] < 0) slot = 1;
  if (slot < 0) { wsSend(num, "{\"t\":\"full\"}"); return; } // 房满
  rooms[r].client[slot] = num;
  clientRoom[num] = r;
  clientSlot[num] = slot;
  wsSend(num, "{\"t\":\"ok\",\"you\":" + String(slot) + "}");
  if (rooms[r].client[0] >= 0 && rooms[r].client[1] >= 0) {
    // 两个人齐了，通知双方（房主收到后发开始信号）
    wsSend(rooms[r].client[0], "{\"t\":\"paired\"}");
    wsSend(rooms[r].client[1], "{\"t\":\"paired\"}");
  }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_DISCONNECTED:
      leaveRoom(num);
      break;
    case WStype_CONNECTED:
      clientRoom[num] = -1;
      clientSlot[num] = -1;
      break;
    case WStype_TEXT: {
      JsonDocument doc;
      if (deserializeJson(doc, payload, len)) return;
      const char *t = doc["t"];
      if (t && strcmp(t, "join") == 0) {
        const char *room = doc["room"] | "";
        handleJoin(num, String(room));
      } else {
        // 非 join 消息：原样转发给同房另一人
        int8_t r = clientRoom[num];
        if (r >= 0 && r < MAX_ROOMS) {
          int8_t slot = clientSlot[num];
          int8_t peer = (slot == 0) ? rooms[r].client[1] : rooms[r].client[0];
          if (peer >= 0) ws.sendTXT(peer, payload, len);
        }
      }
      break;
    }
    default:
      break;
  }
}

// ---------- HTTP：游戏页 /update ----------
void handleRoot() {
  String page = FPSTR(WEBPAGE);
  page.replace("{{DEVICE_IP}}", WiFi.localIP().toString());
  http.send(200, "text/html", page);
}

const char UPDATE_HTML[] PROGMEM = R"UPD(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>固件更新 v1.4.0</title></head>
<body style="font-family:system-ui;padding:24px;max-width:420px;margin:auto">
<h2>贪吃蛇联机服务 · 固件更新</h2>
<form method="POST" action="/update" enctype="multipart/form-data">
<p>OTA 密码：<input type="password" name="pass" required></p>
<p>固件 .bin 文件：<input type="file" name="fw" accept=".bin" required></p>
<p><input type="submit" value="上传更新"></p>
</form>
<p style="color:#888;font-size:12px">更新后板子自动重启；游戏网页随固件一起更新。</p>
</body></html>
)UPD";

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);

  for (int8_t i = 0; i < MAX_ROOMS; i++) {
    rooms[i].code = "";
    rooms[i].client[0] = rooms[i].client[1] = -1;
  }
  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    clientRoom[i] = -1;
    clientSlot[i] = -1;
  }

  prefs.begin("snakelan", false);
  cfgSsid = prefs.getString("ssid", "");
  cfgPass = prefs.getString("pass", "");
  cfgOta = prefs.getString("ota", "");
  if (cfgSsid == "") {
    serialProvision(); // 无配置：进串口配网（不返回，直接重启）
  }

  Serial.println("\n贪吃蛇联机服务 " FW_VERSION);
  WiFi.mode(WIFI_STA); // 只做 Station，永不开热点
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  Serial.print("正在连接 Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    // 连不上时：串口可输 config 重配；不 fallback 到热点
    serialCommands();
    static unsigned long lastDot = 0;
    if (millis() - lastDot > 500) { Serial.print("."); lastDot = millis(); }
    delay(50);
  }
  Serial.println("\n已连接，IP: " + WiFi.localIP().toString());

  if (MDNS.begin("snake-game")) Serial.println("mDNS: http://snake-game.local/");

  http.on("/", handleRoot);
  http.on("/update", HTTP_GET, []() {
    http.send(200, "text/html", FPSTR(UPDATE_HTML));
  });
  http.on(
      "/update", HTTP_POST,
      []() {
        // 上传完成后的收尾：密码在 UPLOAD_FILE_START 已校验
        if (http.arg("pass") != cfgOta) {
          http.send(403, "text/plain", "密码错误");
          return;
        }
        if (Update.hasError()) {
          http.send(500, "text/plain", "更新失败");
        } else {
          http.send(200, "text/plain", "更新成功，重启中…");
          delay(800);
          ESP.restart();
        }
      },
      []() {
        HTTPUpload &up = http.upload();
        if (up.status == UPLOAD_FILE_START) {
          if (http.arg("pass") != cfgOta) return; // 密码不对就不写 flash
          Serial.printf("开始接收固件：%s\n", up.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Serial.println("Update.begin 失败");
        } else if (up.status == UPLOAD_FILE_WRITE) {
          if (Update.write(up.buf, up.currentSize) != up.currentSize) Serial.println("写入失败");
        } else if (up.status == UPLOAD_FILE_END) {
          if (Update.end(true)) Serial.println("固件接收完成");
          else Serial.println("Update.end 失败");
        }
      });
  http.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  Serial.println("HTTP(80) + WebSocket(81) 已启动");
  printHelp();
}

void loop() {
  ws.loop();
  http.handleClient();
  serialCommands();
}
