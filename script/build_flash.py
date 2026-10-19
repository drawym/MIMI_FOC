#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MIMI-FOC 一键「编译 + 烧录」工具（J-Link SWD）
================================================
用法：
  python script/build_flash.py               # 编译 + 烧录 + 复位运行（默认）
  python script/build_flash.py --build       # 只编译
  python script/build_flash.py --flash       # 只烧录（不编译）
  python script/build_flash.py --rtt         # 烧录成功后自动打开 J-Link RTT Viewer
  python script/build_flash.py --no-run      # 烧录后不自动复位运行
  python script/build_flash.py --hex <文件>  # 指定烧录文件（默认 build/at32_app.hex）

注意：烧录/调试时 J-Link 只能被一个程序独占，
      请先关闭 JLinkRTTViewer / IDE 调试会话再烧录。
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

# 工程根目录（本脚本位于 <root>/script/ 下，自动定位）
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(ROOT, "build")
DEFAULT_HEX = os.path.join(BUILD_DIR, "at32_app.hex")

DEVICE = "-AT32F435RGT7"           # J-Link 设备名（带减号，见 JLinkDevices.xml）
JLINK_CANDIDATES = [               # J-Link 常见安装路径（自动探测）
    r"D:\jlink\JLink_V828\JLink.exe",
    r"C:\Program Files\SEGGER\JLink\JLink.exe",
    r"C:\Program Files (x86)\SEGGER\JLink\JLink.exe",
    r"C:\Users\86131\.eide\tools\jlink\JLink.exe",
]
JLINK_TIMEOUT_S = 180              # J-Link 连接/烧录超时


def _utf8():
    """让中文输出在 Windows 终端（Windows Terminal/VS Code）正常显示"""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass


def find_jlink():
    """探测 J-Link 可执行文件路径"""
    for p in JLINK_CANDIDATES:
        if os.path.isfile(p):
            return p
    return shutil.which("JLink.exe")


def build():
    print("==> [1/2] 编译工程: cmake --build build")
    r = subprocess.run(["cmake", "--build", BUILD_DIR], cwd=ROOT)
    if r.returncode != 0:
        print("[失败] 编译出错，请查看上方错误信息")
        sys.exit(1)
    print("[OK] 编译完成")


def flash(hex_file, do_run):
    jlink = find_jlink()
    if not jlink:
        print("[错误] 未找到 JLink.exe，请检查 J-Link 软件安装路径（可修改 JLINK_CANDIDATES）")
        sys.exit(1)
    if not os.path.isfile(hex_file):
        print(f"[错误] 烧录文件不存在: {hex_file}")
        sys.exit(1)

    print("==> [2/2] 通过 J-Link 烧录:")
    print(f"      工具: {jlink}")
    print(f"      设备: {DEVICE}")
    print(f"      文件: {hex_file}")

    # 生成 J-Link 命令脚本（临时文件，正斜杠路径）
    cmds = [
        f"device {DEVICE}",
        "si SWD",
        "speed 4000",
        "connect",
        f"loadfile {hex_file.replace(os.sep, '/')}",
    ]
    if do_run:
        cmds += ["r", "g"]
    cmds += ["exit"]

    fd, script_path = tempfile.mkstemp(suffix=".jlink", prefix="mimi_flash_")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write("\n".join(cmds) + "\n")

        r = subprocess.run(
            [jlink, "-device", DEVICE, "-if", "SWD", "-speed", "4000",
             "-autoconnect", "1", "-CommanderScript", script_path],
            capture_output=True, text=True, timeout=JLINK_TIMEOUT_S,
        )
        out = (r.stdout or "") + (r.stderr or "")
        print(out)
        if r.returncode != 0:
            print("[失败] 烧录出错。常见原因：")
            print("   1. J-Link 未连接 / 目标板未供电 / SWD 接线错误")
            print("   2. JLinkRTTViewer 等程序正占用 J-Link，请先关闭")
            sys.exit(1)
        print("[成功] 烧录完成" + ("，程序已复位运行" if do_run else ""))
    except subprocess.TimeoutExpired:
        print(f"[失败] J-Link 在 {JLINK_TIMEOUT_S}s 内未完成，"
              f"可能被其他程序占用（如 RTT Viewer），请关闭后重试")
        sys.exit(1)
    finally:
        if os.path.exists(script_path):
            os.unlink(script_path)


def open_rtt_viewer():
    jlink = find_jlink()
    if not jlink:
        print("[提示] 未找到 J-Link，无法启动 RTT Viewer")
        return
    rtt = os.path.join(os.path.dirname(jlink), "JLinkRTTViewer.exe")
    if not os.path.isfile(rtt):
        print(f"[提示] 未找到 JLinkRTTViewer.exe: {rtt}")
        return
    subprocess.Popen([rtt])  # 非阻塞启动
    print(f"[提示] 已启动 J-Link RTT Viewer: {rtt}")
    print(f"       在 Target Device 选择 {DEVICE}，连接后 Channel 0 即可看到 hello 打印")


def main():
    _utf8()
    ap = argparse.ArgumentParser(description="MIMI-FOC 一键编译 + J-Link 烧录")
    ap.add_argument("--build", action="store_true", help="只编译，不烧录")
    ap.add_argument("--flash", action="store_true", help="只烧录，不编译")
    ap.add_argument("--rtt", action="store_true", help="烧录成功后打开 J-Link RTT Viewer")
    ap.add_argument("--no-run", action="store_true", help="烧录后不自动复位运行")
    ap.add_argument("--hex", default=DEFAULT_HEX, help=f"指定烧录文件（默认 {DEFAULT_HEX}）")
    args = ap.parse_args()

    do_build = not args.flash
    do_flash = not args.build

    if do_build:
        build()
    if do_flash:
        flash(args.hex, do_run=not args.no_run)
    if args.rtt and do_flash:
        open_rtt_viewer()


if __name__ == "__main__":
    main()
