// snake-lan.ino — XIAO ESP32S3 贪吃蛇局域网联机服务器（v1.5.0）
//
// 功能：
//   USB 串口配网（不开 Wi-Fi 热点）/ HTTP(80) 游戏网页 /
//   WebSocket(81) 房间配对＋消息原样转发 / /update Wi-Fi 无线更新（要密码）
//
// 配网：见 lan/README.md 第 4 节。密码在串口输入时不回显、不写日志，只存 NVS。
// 网络：NVS 存两组 Wi-Fi（网络1家里 / 网络2手机热点），开机扫描后自动连上
//   能找到的那个（都看到优先网络1）；串口 config 菜单可分别设置。
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

#define FW_VERSION "v1.5.0"
#define MAX_ROOMS 4
#define ROOM_CODE_LEN 6

Preferences prefs;
WebServer http(80);
WebSocketsServer ws(81);

String cfgSsid1, cfgPass1; // 网络1：家里 Wi-Fi
String cfgSsid2, cfgPass2; // 网络2：手机热点
String cfgOta;
int8_t activeProfile = 0; // 当前连的是网络几（1/2），0=未连接

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
  Serial.println("可用指令：config=配网菜单 / status=查看状态 / help=本帮助");
}

// 配置某一组网络（1=家里，2=手机热点）。密码不回显、不写日志。
bool provisionProfile(int8_t prof) {
  Serial.print("网络");
  Serial.print(prof);
  Serial.println(prof == 1 ? "（家里 Wi-Fi）：" : "（手机热点）：");
  Serial.print("Wi-Fi name: ");
  String ssid = readLine(true);
  Serial.print("Wi-Fi password: ");
  String pass = readLine(false);
  Serial.println("[已隐藏]");
  if (ssid.length() == 0 || pass.length() == 0) {
    Serial.println("输入不能为空，取消。");
    return false;
  }
  if (prof == 1) {
    prefs.putString("ssid1", ssid);
    prefs.putString("pass1", pass);
  } else {
    prefs.putString("ssid2", ssid);
    prefs.putString("pass2", pass);
  }
  // 注意：密码值永不打印、不写日志
  Serial.println("已保存。");
  return true;
}

bool provisionOta() {
  Serial.print("OTA password: ");
  String ota = readLine(false);
  Serial.println("[已隐藏]");
  if (ota.length() == 0) {
    Serial.println("输入不能为空，取消。");
    return false;
  }
  prefs.putString("ota", ota);
  Serial.println("已保存。");
  return true;
}

// 首次配网：必须配网络1＋OTA（不开机就配不了别的）
void firstProvision() {
  Serial.println("\n=== 贪吃蛇联机配网（USB 串口）===");
  Serial.println("先配置网络1（家里 Wi-Fi），以后可用 config 菜单加网络2（手机热点）。");
  if (!provisionProfile(1)) return; // 失败则返回，下次上电再提示
  if (!provisionOta()) return;
  Serial.println("已保存，重启中…");
  delay(800);
  ESP.restart();
}

void configMenu() {
  Serial.println("\n=== 配网菜单 ===");
  Serial.println("1) 设置网络1（家里 Wi-Fi）");
  Serial.println("2) 设置网络2（手机热点）");
  Serial.println("3) 查看已配置网络（只显示名称，不显示密码）");
  Serial.println("4) 清除某个网络");
  Serial.println("5) 修改 OTA 密码");
  Serial.println("0) 取消");
  Serial.print("选 0-5: ");
  String c = readLine(true);
  c.trim();
  bool changed = false;
  if (c == "1") {
    changed = provisionProfile(1);
  } else if (c == "2") {
    changed = provisionProfile(2);
  } else if (c == "3") {
    Serial.print("网络1: ");
    Serial.println(cfgSsid1 == "" ? "（未配置）" : cfgSsid1);
    Serial.print("网络2: ");
    Serial.println(cfgSsid2 == "" ? "（未配置）" : cfgSsid2);
    Serial.println("OTA 密码：已设置（值不显示）");
  } else if (c == "4") {
    Serial.print("清除网络1还是网络2（输入 1/2）：");
    String w = readLine(true);
    w.trim();
    if (w == "1") {
      prefs.remove("ssid1");
      prefs.remove("pass1");
      Serial.println("网络1已清除。");
      changed = true;
    } else if (w == "2") {
      prefs.remove("ssid2");
      prefs.remove("pass2");
      Serial.println("网络2已清除。");
      changed = true;
    } else {
      Serial.println("取消。");
    }
  } else if (c == "5") {
    changed = provisionOta();
  } else {
    Serial.println("取消。");
  }
  if (changed) {
    cfgSsid1 = prefs.getString("ssid1", "");
    cfgPass1 = prefs.getString("pass1", "");
    cfgSsid2 = prefs.getString("ssid2", "");
    cfgPass2 = prefs.getString("pass2", "");
    cfgOta = prefs.getString("ota", "");
    Serial.println("重启后生效，重启中…");
    delay(800);
    ESP.restart();
  }
}

void serialCommands() {
  if (!Serial.available()) return;
  String cmd = readLine(true);
  cmd.trim();
  if (cmd == "config") {
    configMenu();
  } else if (cmd == "status") {
    Serial.print("Wi-Fi: ");
    if (WiFi.status() == WL_CONNECTED && activeProfile > 0) {
      Serial.print("已连接网络");
      Serial.println(activeProfile);
    } else {
      Serial.println("未连接");
    }
    Serial.print("SSID: ");
    Serial.println(activeProfile == 1 ? cfgSsid1 : (activeProfile == 2 ? cfgSsid2 : "（无）"));
    Serial.print("IP: ");
    Serial.println(WiFi.localIP().toString());
    Serial.println("mDNS: http://snake-game.local/");
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
<title>固件更新 v1.5.0</title></head>
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

// 试连两组网络：先扫描决定顺序（都看到优先网络1），每组最多 12 秒。
// 返回 true=连上（activeProfile 置为 1/2），false=都没连上。
bool tryConnect() {
  Serial.println("正在扫描 Wi-Fi…");
  int8_t want = 0;
  int n = WiFi.scanNetworks();
  bool seen1 = false, seen2 = false;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (cfgSsid1 != "" && s == cfgSsid1) seen1 = true;
    if (cfgSsid2 != "" && s == cfgSsid2) seen2 = true;
  }
  WiFi.scanDelete();
  if (seen1) want = 1;
  else if (seen2) want = 2;

  // 扫描没看到时，按 1→2 顺序各试一次（SSID 可能隐藏）
  int8_t order[2] = {(int8_t)(want == 2 ? 2 : 1), (int8_t)(want == 2 ? 1 : 2)};
  for (int8_t k = 0; k < 2; k++) {
    int8_t prof = order[k];
    String ssid = (prof == 1) ? cfgSsid1 : cfgSsid2;
    String pass = (prof == 1) ? cfgPass1 : cfgPass2;
    if (ssid == "") continue;
    Serial.print("正在连接网络");
    Serial.print(prof);
    Serial.print("（");
    Serial.print(ssid);
    Serial.print("）");
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long t0 = millis();
    unsigned long lastDot = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
      // 连不上时：串口可输 config 重配（改完自动重启）
      serialCommands();
      if (millis() - lastDot > 500) { Serial.print("."); lastDot = millis(); }
      delay(50);
    }
    if (WiFi.status() == WL_CONNECTED) {
      activeProfile = prof;
      return true;
    }
    Serial.println("\n网络" + String(prof) + " 连接超时，换下一个。");
    WiFi.disconnect();
  }
  return false;
}

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
  // v1.4.0 单组配置迁移：旧 ssid/pass 转为网络1
  cfgSsid1 = prefs.getString("ssid1", "");
  if (cfgSsid1 == "") {
    String oldS = prefs.getString("ssid", "");
    if (oldS != "") {
      prefs.putString("ssid1", oldS);
      prefs.putString("pass1", prefs.getString("pass", ""));
      prefs.remove("ssid");
      prefs.remove("pass");
      cfgSsid1 = oldS;
      Serial.println("旧单组配置已迁移为网络1。");
    }
  }
  cfgPass1 = prefs.getString("pass1", "");
  cfgSsid2 = prefs.getString("ssid2", "");
  cfgPass2 = prefs.getString("pass2", "");
  cfgOta = prefs.getString("ota", "");
  if (cfgSsid1 == "") {
    firstProvision(); // 无配置：进串口配网（成功则重启，不返回）
  }

  Serial.println("\n贪吃蛇联机服务 " FW_VERSION);
  WiFi.mode(WIFI_STA); // 只做 Station，永不开 Wi-Fi 热点

  // 两组都连不上：每 60 秒重试（热点晚开也能自己连上），随时可输 config
  while (!tryConnect()) {
    Serial.println("\n两个网络都连不上，60 秒后重试（可随时输入 config 重配）。");
    unsigned long wt0 = millis();
    while (millis() - wt0 < 60000) { serialCommands(); delay(100); }
  }
  Serial.println("\n已连接网络" + String(activeProfile) + "，IP: " + WiFi.localIP().toString());

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
