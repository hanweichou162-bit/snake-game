# lan/ — 局域网联机（单片机方案）· 草稿

> 状态：设备已到货（2026-10-03，技適 217-230892 已确认）。等 PR #4（v1.3.0）确认合并后开分支写代码，当前只有本文档。
> 目标版本：v1.4.0（以 v1.3.0 合并后为基础）。流程照常：分支 → PR → 用户确认 → 合并 → 自动打标记建 Release。

## 1. 这是什么

用一台 Seeed XIAO ESP32S3（型号 113991114，技適编号 217-230892）做"家里的小服务器"：
它同时提供游戏网页（HTTP 80）和联机转发（WebSocket 81）。
手机连**同一个 Wi-Fi**，扫码打开单片机上的网页即可加入对战。
线上 GitHub Pages 版本保持不变（电脑同屏双人、单人）。

```
[手机A] ══Wi-Fi══> [XIAO ESP32S3] <══Wi-Fi══ [手机B]
                       ├─ HTTP(80)：游戏网页
                       └─ WS(81)：房间配对＋消息原样广播（不解析游戏逻辑）
```

## 2. 一份代码，不维护两份

- 仓库根目录 `index.html` 是唯一真相来源。
- `lan/build.py`（到货后写）：读取根目录 `index.html`，把占位符替换为局域网默认配置
  （默认模式 lan 等），再转成 C 头文件 `lan/firmware/webpage.h`（PROGMEM 字符串）供固件编译进 flash。
- 生成的 `webpage.h` 是构建产物，**不提交仓库**（见 `.gitignore`），每次刷机前跑一遍脚本。
- 手机「局域网联机」模式仍只改原来那一处集中设置：模式列表加一项 `lan`；
  该模式下 WebSocket 地址取 `ws://` + `location.hostname` + `:81`。
  线上版（域名为 github.io）隐藏该选项；单片机版默认选中。

## 3. 加入方式

- 房主建房后，手机页面**直接显示二维码**，内容为 `http://<板子IP>/#room=<房间码>`；
  朋友扫码后直接进入房间，不用口头报房间码。房间码输入框保留作为备用。
- 二维码统一用板子 IP（部分安卓手机打不开 .local 地址）；
  固件在提供网页时把 `{{DEVICE_IP}}` 占位替换为实际 IP。
- 二维码用内嵌的 qrcode-generator（MIT，打包进单文件，无网络请求）生成。

## 4. 首次配网（USB 线＋串口监视器，不开热点）

板子**任何时候都不开 Wi-Fi 热点**（无配网 AP），配网只走 USB 串口：

1. 刷机完成后保持 USB 线连着电脑，在 Arduino IDE 打开**串口监视器**
   （工具 → 串口监视器；波特率 115200；行尾选"换行（NL）"）。
2. 板子检测到 NVS 里没有 Wi-Fi 配置，会在串口里依次提示输入：
   - `Wi-Fi name:` → 输入家里 Wi-Fi 名称，回车（正常回显）
   - `Wi-Fi password:` → 输入 Wi-Fi 密码，回车（**输入时不回显**，屏幕上看不到你打的字，输完直接回车即可）
   - `OTA password:` → 输入以后 Wi-Fi 更新用的密码，回车（同样不回显）
3. 三项全部只存单片机 NVS；**密码不回显、不写入任何日志**；
   仓库、本文档、聊天记录、Notion 里不出现任何密码值。
4. 保存成功后板子自动重启，加入家里 Wi-Fi。手机连家里 Wi-Fi，打开 `http://snake-game.local/`。

无配置时每次上电都会重复提示，直到配网成功为止。

### 以后换 Wi-Fi 或改密码（重配）

1. USB 线连电脑，Arduino IDE 打开串口监视器（同上设置）。
2. 在输入框输入 `config` 回车 → 板子重新进入第 4 节第 2 步的三个提示。
3. 按提示重新输入三项（密码仍不回显），保存后自动重启生效。
4. 输入 `help` 回车可查看串口可用指令。

## 5. 第一次刷机（Windows，USB 线）

### 5.1 安装 Python（只装一次）

1. 浏览器打开 https://www.python.org/downloads/ → 点黄色 "Download Python 3.x.x" 按钮 → 下载 Windows installer (64-bit)。
2. 双击安装包 → **第一屏最下方勾选 "Add python.exe to PATH"**（关键，不勾后面命令找不到）→ 点 "Install Now" → 等完成 → Close。
3. 验证：按 `Win+R` → 输入 `cmd` → 回车 → 在黑色窗口输入 `py --version` → 显示 `Python 3.x.x` 即成功。
   以后运行脚本都用 `py`（如 `py lan/build.py`）；如果 `py` 没反应再试 `python`。

### 5.2 下载代码（ZIP，不用装 Git）

1. 浏览器打开 https://github.com/hanweichou162-bit/snake-game
2. 点左上分支按钮（显示 main）→ 找到并点 **`feature/lan-multiplayer`**（注意别下成 main）。
3. 点绿色 "<> Code" 按钮 → "Download ZIP"。
4. 右键下载好的 ZIP → "全部解压缩" → 解压到桌面 → 得到文件夹 `snake-game-feature-lan-multiplayer`。
5. 打开这个文件夹 → 点一下顶部地址栏 → 输入 `cmd` → 回车 → 弹出黑色窗口（终端，且已经在这个文件夹里了）。

### 5.3 安装 Arduino IDE 并配置

1. 浏览器打开 arduino.cc → Downloads → 下载 Windows 版 → 一路下一步安装。
2. 打开 Arduino IDE → 左上角「文件」→「首选项」→「附加开发板管理器网址」粘贴
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` → 点「好」。
3. 左侧边栏点开发板管理器图标（或「工具」→「开发板」→「开发板管理器」）→
   搜索框输入 `esp32` → 找到 "esp32 by Espressif Systems" → 版本下拉选 **3.3.12** → 点「安装」。
4. 顶部工具栏的开发板下拉框（或「工具」→「开发板」→「esp32」）→ 搜索 `XIAO_ESP32S3` → 选中。
5. 「工具」菜单 → USB CDC On Boot → Enabled；PSRAM → OPI PSRAM；
   分区方案保持 **Default**（XIAO ESP32S3 默认 8MB 分区自带双 OTA 槽，直接支持 Wi-Fi 更新）。
6. 左侧边栏点库管理器图标（或「工具」→「管理库」）→
   搜索 `WebSockets` → "WebSockets by Markus Sattler" → 版本 **2.7.2** → 安装；
   再搜索 `ArduinoJson` → "ArduinoJson by Benoit Blanchon" → 版本 **7.4.3** → 安装。
   （mDNS、无线更新为开发包自带，不用装。）

### 5.4 生成网页并上传

1. USB-C 线连接板子和电脑 →「工具」→「端口」→ 选择新出现的 COMx。
   （如果没出现新端口，先换一根线——有些线只能充电不能传数据。）
2. 回到 5.2 步的黑色窗口 → 输入 `py lan/build.py` → 看到「已生成 lan/firmware/webpage.h」即成功。
3. Arduino IDE →「文件」→「打开」→ 选择桌面临时文件夹里的 `lan/firmware/snake-lan.ino`。
4. 点左上角「上传」按钮（→ 箭头）→ 等底部输出栏显示上传完成。

### 5.5 配网（串口监视器）

1. IDE 右上角点**放大镜图标**（或菜单「工具」→「串口监视器」，快捷键 `Ctrl+Shift+M`）。
2. 串口监视器面板**右上方**：波特率下拉 → 选 **115200**；它旁边的行尾下拉 → 选 **Newline**。
3. 板子无配网信息时会自动提示，在顶部输入框依次输入（输完按回车或点发送）：
   - `Wi-Fi name:` → 家里 Wi-Fi 名称，回车（正常回显）
   - `Wi-Fi password:` → Wi-Fi 密码，回车（**不回显**，屏幕上看不到字是正常的）
   - `OTA password:` → 以后 Wi-Fi 更新用的密码，回车（同样不回显）
4. 看到「已连接，IP: 192.168.x.x」即配网成功。

### 5.6 刷机后测试清单

| # | 检查项 | 通过的样子 |
|---|---|---|
| 1 | 板子连上 Wi-Fi | 串口监视器显示 `已连接，IP: 192.168.x.x`，接着显示 `HTTP(80) + WebSocket(81) 已启动`（而不是一直打印 `...`） |
| 2 | 手机打开游戏页 | 手机连**家里同一 Wi-Fi** → 浏览器打开 `http://<上面那个IP>/` → 游戏页打开，页顶显示 **v1.4.0**，默认选中「双人联机」 |
| 3 | 建房 | 点「创建房间」→ 显示 6 位房间码（如 `A7K9P2`）+ 二维码正常渲染 |
| 4 | 第二台手机加入 | 第二台手机连同一 Wi-Fi → 扫码 → 自动进入房间并等待 → **两台手机同时进入游戏画面**（P1 绿、P2 蓝两条蛇出现） |
| 5 | 对战 | 两台手机分别滑动控制自己的蛇，移动跟手；故意撞墙/撞对方身体 → 输方显示「你输了！」、赢方显示「你赢了！」，P1/P2 计分正常 |
| 6 | 版本号 | 两台手机页顶都是 **v1.4.0** |

扫码不方便时，第二台手机也可以打开 `http://<IP>/` 后手动输入 6 位房间码加入。

## 6. 以后更新走 Wi-Fi（要 OTA 密码）

1. **本地先重新生成网页**：黑色窗口进入代码文件夹 → 运行 `py lan/build.py`
   （确保 `webpage.h` 是最新的，网页随固件一起更新）。
2. **导出 .bin 文件**：Arduino IDE 打开 `lan/firmware/snake-lan.ino` →
   菜单「项目」→「导出已编译的二进制文件」。
3. **选对文件**：在 `lan/firmware/` 文件夹里找到 **`snake-lan.ino.bin`**
   （如果没看到，检查该目录下的 `build` 子文件夹）。
   > ⚠️ 不要用 `snake-lan.ino.merged.bin`。
   > 理由：`/update` 用的是 ESP32 的 `Update` 库，它往 OTA 应用分区写入**纯应用镜像**；
   > `snake-lan.ino.bin` 就是纯应用镜像。`merged.bin` 里还打包了 bootloader 和分区表，
   > 是给 USB 整片烧录用的——传给 `/update` 会写错位置，板子起不来。
4. **打开板子的更新页面**：手机/电脑连**家里 Wi-Fi** → 浏览器打开
   `http://<板子IP>/update`
   （板子 IP 可用串口监视器输 `status` 查看；`http://snake-game.local/update` 一般也能打开）。
5. **输入密码并上传**：在页面输入 OTA 更新密码（配网第 4 节设置的那个）→
   点「选择文件」选中第 3 步的 `snake-lan.ino.bin` → 点「上传更新」→ 等待页面显示「更新成功，重启中」。
6. **确认版本**：板子自动重启后，手机打开游戏页，看页顶版本号是否为新版本（如 v1.4.0）。

## 7. 性能上限

XIAO ESP32S3 = ESP32-S3R8：双核 240MHz，512KB SRAM + 8MB PSRAM，8MB Flash。
保守估算（连接缓冲放 PSRAM）：

- 单个 WebSocket 连接（含 TCP 缓冲）：约 32KB
- 单个房间状态：约 4KB
- **上限：4 个房间 × 每房 2 人 = 8 个并发连接**；内存约 272KB＋系统/协议栈约 200KB，
  总量 < 1MB，8MB PSRAM 绰绰有余
- 消息量：tick 10Hz、每条 < 200 字节，8 人全开约 16KB/s，无压力
- 硬上限写进固件，超了拒绝新连接并提示"房间已满"

## 8. 安全与隐私

- Wi-Fi 名称/密码、OTA 更新密码只在 USB 串口配网时由用户在电脑上输入，存单片机 NVS；
  输入时不回显、不记日志；**仓库、本文档、聊天记录、Notion 里不出现任何密码值**。
- 仅家庭局域网：Station 模式只连家里 Wi-Fi，不做端口转发；板子任何时候都不开热点。
- 不收集个人信息：固件不写日志到 flash（调试日志只走 USB 串口）；房间数据只在内存，断电即失；无统计、无上报。
- 停用方法：拔电源。

## 9. 库清单（名称、版本、许可证）

| 库 | 版本 | 许可证 | 用途 |
|---|---|---|---|
| Espressif esp32 Arduino 开发包 | **3.3.12**（2026-10-03 核实为 3.x 最新） | LGPL-2.1（ArduinoOTA/Update 等文件为 Apache-2.0） | 开发包本体、Wi-Fi、mDNS、无线更新 |
| WebSockets（Markus Sattler / Links2004） | **2.7.2**（2026-10-03 核实最新） | LGPL-2.1 | WebSocket 联机转发 |
| ArduinoJson（Benoit Blanchon） | **7.4.3**（2026-10-03 核实最新） | MIT | 房间/消息 JSON 解析 |
| qrcode-generator（Kazuhiko Arase，构建时内嵌进固件版网页） | **1.4.4** | MIT | 房主手机显示加入二维码（打包进单文件，无网络请求） |

以上均为宽松许可证，与仓库 MIT 兼容。
ElegantOTA 不用：开源版为 AGPL-3.0，与仓库 MIT 不兼容（2026-10-01 已核实，见运维页）。
> 版本钉死说明：上表版本为 2026-10-03 实际核实的最新稳定版；刷机时以 Arduino IDE 库管理器/开发板管理器显示为准，如有更新先在测试环境验证后再改表。

## 10. 目录结构（到货后）

```
lan/
  README.md        ← 本文件
  build.py         ← 从根目录 index.html 生成 firmware/webpage.h
  .gitignore       ← 忽略生成的 webpage.h
  firmware/
    snake-lan.ino  ← USB 串口配网 / HTTP / WebSocket / 房间管理 / /update
    webpage.h      ← 构建产物，不提交
```
