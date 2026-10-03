#!/usr/bin/env python3
"""lan/build.py — 从仓库根目录 index.html 生成固件内嵌网页。

用法（在仓库根目录执行）：
    python3 lan/build.py

产出：
    lan/snake-lan/webpage.h   （构建产物，不提交仓库；见 lan/.gitignore）

处理：
  1. 读取 ../index.html（v1.4.0，唯一真相来源）
  2. <!--{{QRCODE_LIB}}--> 占位替换为内嵌 qrcode-generator（MIT，打包进单文件）
  3. /*{{LAN_BUILD}}*/false → /*{{LAN_BUILD}}*/true（固件版默认联机模式）
  4. {{DEVICE_IP}} 保留，由固件 serving 时替换为板子实际 IP
  5. 零对外请求检查：不允许出现外部资源引用
  6. 转成 C++ PROGMEM raw literal 写入 webpage.h
"""
import re
import sys
from pathlib import Path

LAN_DIR = Path(__file__).resolve().parent
ROOT = LAN_DIR.parent
SRC = ROOT / "index.html"
QR = LAN_DIR / "vendor" / "qrcode-1.4.4.js"
OUT = LAN_DIR / "snake-lan" / "webpage.h"

DELIM = "SNAKEPAGE"


def fail(msg: str) -> None:
    print(f"build.py 失败：{msg}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    if not SRC.exists():
        fail(f"找不到 {SRC}")
    if not QR.exists():
        fail(f"找不到 {QR}（qrcode-generator 1.4.4，MIT）")

    html = SRC.read_text(encoding="utf-8")

    # 1. 内嵌 QR 库（仅固件版；Pages 版保持轻量）
    qr_js = QR.read_text(encoding="utf-8")
    if "<!--{{QRCODE_LIB}}-->" not in html:
        fail("index.html 缺少 <!--{{QRCODE_LIB}}--> 占位")
    html = html.replace(
        "<!--{{QRCODE_LIB}}-->",
        "<script>\n/* qrcode-generator 1.4.4 (c) Kazuhiko Arase, MIT */\n"
        + qr_js + "\n</script>",
        1,
    )

    # 2. 打开固件构建开关
    if "/*{{LAN_BUILD}}*/false" not in html:
        fail("index.html 缺少 /*{{LAN_BUILD}}*/false 占位")
    html = html.replace("/*{{LAN_BUILD}}*/false", "/*{{LAN_BUILD}}*/true", 1)

    # 3. {{DEVICE_IP}} 必须保留（固件运行时替换）
    if "{{DEVICE_IP}}" not in html:
        fail("index.html 缺少 {{DEVICE_IP}} 占位")

    # 4. 零对外请求检查：不允许加载外部资源
    bad = re.findall(
        r'''(?:src|href)\s*=\s*["']https?://[^"']+["']|url\(\s*https?://[^)]+\)|@import\s+["']https?://''',
        html,
        re.IGNORECASE,
    )
    if bad:
        fail(f"发现外部资源引用（单文件不允许）：{bad[:3]}")

    # 5. 版本号一致性检查
    m = re.search(r'<div id="ver">(v\d+\.\d+\.\d+)</div>', html)
    if not m:
        fail("index.html 缺少版本号")
    print(f"版本：{m.group(1)}，LAN_BUILD=true，QR 已内嵌")

    # 6. 生成 PROGMEM 头文件
    if DELIM + '"' in html or ")" + DELIM + '"' in html:
        fail("HTML 内容与 raw literal 定界符冲突")
    header = (
        "// 自动生成：不要手改。用 python3 lan/build.py 从根目录 index.html 重新生成。\n"
        "#pragma once\n"
        '#include <pgmspace.h>\n'
        f"const char WEBPAGE[] PROGMEM = R\"{DELIM}(\n" + html + f"\n){DELIM}\";\n"
    )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(header, encoding="utf-8")
    print(f"已生成 {OUT}（{OUT.stat().st_size} 字节）")


if __name__ == "__main__":
    main()
