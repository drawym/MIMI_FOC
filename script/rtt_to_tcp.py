#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
rtt_to_tcp.py  —  把 J-Link RTT 的二进制电流数据转发成本地 TCP 文本流，喂给 VOFA+

一、为什么要有这个脚本
    串口只有 115200，10kHz 电流数据根本送不出来（14 字节/帧 → 140kB/s，差 12 倍）。
    J-Link 的 RTT 走的是调试口，能到几百 kB/s，但 RTT Viewer 只能看文本，没有波形。
    这个脚本把两者接起来：
        固件(10kHz) --RTT 通道1 二进制 8B/帧--> 本脚本 --TCP 文本 "u,v,w\n" (安培, 浮点)--> VOFA+
    VOFA+ 里照旧用 FireWater 协议、3 通道，不需要装任何新软件。
    帧长 8 字节 = 2 字节同步头(AA 55) + 3 个 int16(mA)，带同步头才能容忍 RTT 丢字节。

二、用法
    # 1. 不需要硬件，先验证脚本本身（数据是合成的三相正弦）
    python rtt_to_tcp.py --demo

    # 2. 连真实 J-Link
    python rtt_to_tcp.py

    # 3. VOFA+ 里配置：数据源 = 网络 -> TCP 客户端 -> 127.0.0.1 : 3333
    #    协议 = FireWater，通道数 = 3

三、常用参数
    --decimate 10      每 10 个样本发 1 个（默认 10 → 10kHz 抽成 1kHz）
    --port 3333        监听端口，要和 VOFA+ 里填的一致
    --channel 1        RTT 通道号，必须与固件 FOC_SCOPE_RTT_CH 一致
    --device           J-Link 器件名，连不上会自动退回 Cortex-M4
    --csv out.csv      同时存一份 CSV，便于离线分析

四、依赖
    pip install pylink-square          （--demo 模式不需要）

五、自检
    python rtt_to_tcp.py --selftest    # 只测帧解析，不开服务
"""

import argparse
import socket
import sys
import threading
import time

# ---- 帧格式（与固件 user/include/foc_scope.h 严格一致）---------------------
#   [0]     0xAA      同步头高字节
#   [1]     0x55      同步头低字节
#   [2..3]  int16 U 相电流 mA（小端，有符号）
#   [4..5]  int16 V 相电流 mA
#   [6..7]  int16 W 相电流 mA
SYNC0 = 0xAA
SYNC1 = 0x55
SYNC_BYTES = bytes((SYNC0, SYNC1))
FRAME_BYTES = 8

# 输出给 VOFA+ 的单位换算：固件发 mA，上位机显示 A（带 3 位小数）
MA_TO_A = 0.001


# ---------------------------------------------------------------------------
# 一、帧解析：按同步头 AA 55 重新对齐，逐帧取出三相电流
# ---------------------------------------------------------------------------
def parse_frames(raw_bytes, leftover):
    """
    把字节流切成 (iu, iv, iw) 样本列表，单位 mA。

    :param raw_bytes: 本次新收到的字节
    :param leftover:  上次没凑够一帧的残余字节
    :return: (样本列表, 本次剩余字节)

    为什么按同步头找而不是按固定长度切：
        RTT 是 NO_BLOCK_SKIP 模式，缓冲区满时丢弃的字节数是任意的。
        如果只按"每帧固定 N 字节"切分，一旦丢掉 1 个字节，
        后面所有帧就永久错位，解析出来全是 0、1 这种随机小数字。
        带同步头后，每次找 AA 55 重新对齐，丢多少都能自愈。
    """
    buffer = leftover + raw_bytes
    samples = []
    pos = 0

    while True:
        start = buffer.find(SYNC_BYTES, pos)

        if start < 0:
            # 没有帧头：只保留可能是"半个同步头"的末尾字节
            keep = buffer[-1:] if buffer.endswith(bytes((SYNC0,))) else b""
            return samples, keep

        if len(buffer) - start < FRAME_BYTES:
            # 帧不完整，剩下的是下一帧的开头，留着等下一包
            return samples, buffer[start:]

        frame = buffer[start:start + FRAME_BYTES]
        # 小端有符号 16 位：signed=True 让负数电流正确还原
        current_u = int.from_bytes(frame[2:4], "little", signed=True)
        current_v = int.from_bytes(frame[4:6], "little", signed=True)
        current_w = int.from_bytes(frame[6:8], "little", signed=True)
        samples.append((current_u, current_v, current_w))

        # 关键：下一帧从 start+1 重新搜同步头，而不是直接跳到 start+8。
        # 因为一旦中途丢过字节，真实帧头的位置会比 start+8 更靠前，
        # 直接跳过去就会把下一帧的头也吞掉，导致连续丢好几帧才恢复。
        # 从 start+1 搜的代价只是每帧多一次 bytes.find，换来"丢 1 字节只丢 1 帧"。
        pos = start + 1


# ---------------------------------------------------------------------------
# 二、数据源
# ---------------------------------------------------------------------------
class JLinkSource(object):
    """通过 pylink 从 J-Link 的 RTT 上行通道读取二进制样本。"""

    def __init__(self, device, channel, jlink_dir, serial_no=None):
        try:
            import pylink   # 延迟导入：--demo / --selftest 不需要 pylink
        except ImportError:
            raise SystemExit(
                "[ERROR] 缺少 pylink-square，请先执行：\n"
                "        python -m pip install pylink-square")

        self.pylink = pylink
        self.channel = channel
        self.leftover = b""
        self.device_name = "?"

        # pylink 需要找到 JLinkARM.dll，把安装目录加进 DLL 搜索路径
        import os
        if jlink_dir and hasattr(os, "add_dll_directory") and os.path.isdir(jlink_dir):
            os.add_dll_directory(jlink_dir)

        self.jlink = pylink.JLink()
        if serial_no is None:
            self.jlink.open()
        else:
            self.jlink.open(serial_no=serial_no)

        self.jlink.set_tif(pylink.enums.JLinkInterfaces.SWD)

        # 器件名不认识就退回通用 Cortex-M4，RTT 仍可用
        try:
            self.jlink.connect(device)
            self.device_name = device
        except Exception:
            self.jlink.connect("Cortex-M4")
            self.device_name = "Cortex-M4"

        self.jlink.rtt_start()

        # ⚠ 关键：J-Link 的 connect() 会把 CPU 停住（halt），
        #    不停在跑的目标不会往 RTT 缓冲里写新数据，必须让它继续运行。
        #    restart() 内部是 JLINKARM_GoEx，只做"继续执行"，不会复位芯片。
        if self.jlink.halted():
            self.jlink.restart()
            print("[JLINK] CPU 原为 halt，已恢复运行（未复位）")

    def read(self):
        """读一次 RTT，返回本次解析出的样本列表。"""
        try:
            raw = bytes(self.jlink.rtt_read(self.channel, 4096))
        except Exception:
            return []

        if not raw:
            return []

        samples, self.leftover = parse_frames(raw, self.leftover)
        return samples

    def close(self):
        for action in ("rtt_stop", "close"):
            try:
                getattr(self.jlink, action)()
            except Exception:
                pass


class DemoSource(object):
    """合成三相正弦（8Hz 电频率 + 噪声），用于在没有硬件时验证脚本。"""

    def __init__(self, electrical_hz=8.0, sample_hz=10000.0, amplitude_ma=600.0):
        import math
        import random

        self.math = math
        self.random = random
        self.electrical_hz = electrical_hz
        self.sample_hz = sample_hz
        self.amplitude_ma = amplitude_ma
        self.phase_index = 0
        self.last_time = time.time()
        # 小数累加器：把"不足一个样本"的零头留到下次，
        # 避免每轮 int() 截断造成长期速率偏差（实测会偏快约 9%）
        self.sample_accumulator = 0.0
        self.channel = "demo"
        self.device_name = "demo"

    def read(self):
        """按真实经过时间生成对应数量的样本，模拟 10kHz 数据流。"""
        now = time.time()
        elapsed = now - self.last_time
        self.last_time = now

        self.sample_accumulator += elapsed * self.sample_hz
        count = int(self.sample_accumulator)
        self.sample_accumulator -= count
        count = min(max(count, 0), 20000)   # 防止卡顿后一次补太多
        samples = []
        two_pi = 2.0 * self.math.pi

        for _ in range(count):
            self.phase_index += 1
            theta = two_pi * self.electrical_hz * self.phase_index / self.sample_hz
            base = self.amplitude_ma * self.math.sin(theta)
            noise = self.random.gauss(0.0, 6.0)
            samples.append((
                int(base + noise),
                int(self.amplitude_ma * self.math.sin(theta - two_pi / 3.0) + noise),
                int(self.amplitude_ma * self.math.sin(theta + two_pi / 3.0) + noise),
            ))

        return samples

    def close(self):
        pass


# ---------------------------------------------------------------------------
# 三、TCP 服务：监听本机端口，等 VOFA+ 连上来后持续推文本
# ---------------------------------------------------------------------------
class TextStreamServer(object):
    """单客户端 TCP 服务器。客户端断开后自动等待下一次连接，不退出。"""

    def __init__(self, port):
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(("127.0.0.1", port))    # 只监听本机，不暴露到局域网
        self.server.listen(1)
        self.server.settimeout(1.0)
        self.client = None
        self.port = port

    def accept_if_needed(self):
        """没有客户端时尝试接受一个连接；避免阻塞主循环。"""
        if self.client is not None:
            return
        try:
            connection, address = self.server.accept()
            connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)   # 关掉 Nagle，降低延迟
            self.client = connection
            print("[TCP] VOFA+ 已连接 %s:%d" % address)
        except socket.timeout:
            pass
        except OSError:
            pass

    def send_line(self, text_bytes):
        """发送一行数据。返回 True 表示确实发出去了（有客户端且未断线）。"""
        if self.client is None:
            return False
        try:
            self.client.sendall(text_bytes)
            return True
        except OSError:
            print("[TCP] 客户端断开，等待重连")
            self.close_client()
            return False

    def close_client(self):
        if self.client is not None:
            try:
                self.client.close()
            except OSError:
                pass
            self.client = None

    def close(self):
        self.close_client()
        try:
            self.server.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# 四、主循环：RTT → 抽点 → 文本 → TCP
# ---------------------------------------------------------------------------
def run(source, port, decimate, csv_path=None, duration=None):
    server = TextStreamServer(port)
    print("[INFO] 监听 127.0.0.1:%d，请在 VOFA+ 里用 TCP 客户端连接" % port)
    print("[INFO] 抽点比 1:%d，端点 %s" % (decimate, getattr(source, "device_name", "?")))

    csv_file = None
    if csv_path:
        csv_file = open(csv_path, "w", encoding="utf-8")
        csv_file.write("t_s,iu_ma,iv_ma,iw_ma\n")

    sample_index = 0          # 累计收到的样本数（用于抽点取模）
    sent_count = 0            # 累计发出的行数
    stat_start = time.time()
    started_at = time.time()

    try:
        while True:
            if duration and (time.time() - started_at) > duration:
                break

            server.accept_if_needed()
            samples = source.read()

            if not samples:
                time.sleep(0.001)     # 没数据就让出 CPU
                continue

            lines = []

            for sample in samples:
                sample_index += 1

                if csv_file is not None:
                    csv_file.write("%.6f,%d,%d,%d\n"
                                   % (time.time(), sample[0], sample[1], sample[2]))

                # 抽点：每 decimate 个样本才发一个
                if (sample_index % decimate) != 0:
                    continue

                # FireWater 文本：逗号分通道，换行结束一帧
                # 固件发的是 mA 整数，这里换算成安培并保留 3 位小数（1mA 分辨率），
                # VOFA+ 直接显示浮点电压/电流波形，不用再在界面上做单位换算。
                lines.append("%.3f,%.3f,%.3f\n"
                             % (sample[0] * MA_TO_A,
                                sample[1] * MA_TO_A,
                                sample[2] * MA_TO_A))

            if lines:
                payload = "".join(lines).encode("ascii")
                # 只有真正发出去才算上送量，没客户端时不计入统计
                if server.send_line(payload):
                    sent_count += len(lines)

            # 每秒打印一次实际速率，方便判断瓶颈在哪
            now = time.time()
            if now - stat_start >= 1.0:
                print("[STAT] 输入 %.0f Hz  上送 %.0f Hz  客户端 %s"
                      % (sample_index / (now - stat_start),
                         sent_count / (now - stat_start),
                         "已连接" if server.client is not None else "无"))
                sample_index = 0
                sent_count = 0
                stat_start = now

    except KeyboardInterrupt:
        print("\n[INFO] 收到 Ctrl+C，退出")
    finally:
        if csv_file is not None:
            csv_file.flush()
            csv_file.close()
        server.close()
        source.close()


# ---------------------------------------------------------------------------
# 五、自检
# ---------------------------------------------------------------------------
def run_selftest():
    """验证帧解析的边界：整帧、半帧、跨包拼接、以及"丢字节后能否自愈"。"""
    failures = 0

    def build(values):
        """按固件格式打包：AA 55 + 3 个 int16 小端"""
        out = bytearray()
        for u, v, w in values:
            out += SYNC_BYTES
            for item in (u, v, w):
                out += int(item).to_bytes(2, "little", signed=True)
        return bytes(out)

    raw = build([(100, -200, 300), (-1, 0, 1)])

    samples, rest = parse_frames(raw, b"")
    if samples != [(100, -200, 300), (-1, 0, 1)] or rest != b"":
        print("用例1 失败:", samples, rest)
        failures += 1
    else:
        print("用例1 通过：两整帧，负数解析正确")

    samples, rest = parse_frames(raw[:10], b"")
    if len(samples) != 1 or len(rest) != 2:
        print("用例2 失败:", samples, len(rest))
        failures += 1
    else:
        print("用例2 通过：1 整帧 + 2 字节残余（下一帧的同步头）")

    samples, rest = parse_frames(raw[10:], rest)
    if samples != [(-1, 0, 1)] or rest != b"":
        print("用例3 失败:", samples, rest)
        failures += 1
    else:
        print("用例3 通过：跨包拼接正确")

    # 关键用例：模拟 RTT 丢字节，验证能否靠同步头重新对齐
    broken = raw[:5] + raw[6:]
    samples, rest = parse_frames(broken, b"")
    if (-1, 0, 1) not in samples:
        print("用例4 失败：丢字节后未能重新对齐", samples)
        failures += 1
    else:
        print("用例4 通过：丢 1 字节后，后续帧仍能重新对齐")

    extreme = build([(32767, -32768, 0)])
    samples, _ = parse_frames(extreme, b"")
    if samples != [(32767, -32768, 0)]:
        print("用例5 失败:", samples)
        failures += 1
    else:
        print("用例5 通过：int16 极值正确")

    print("自检结果：", "全部通过" if failures == 0 else "%d 项失败" % failures)
    return 0 if failures == 0 else 1


# ---------------------------------------------------------------------------
# 六、入口
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="RTT 二进制电流 → TCP 文本流（给 VOFA+ 用）")
    parser.add_argument("--demo", action="store_true", help="用合成数据验证脚本，不需要硬件")
    parser.add_argument("--selftest", action="store_true", help="只跑帧解析自检")
    parser.add_argument("--port", type=int, default=3333, help="TCP 监听端口（与 VOFA+ 一致）")
    parser.add_argument("--decimate", type=int, default=10, help="抽点比：每 N 个样本发 1 个")
    parser.add_argument("--channel", type=int, default=1, help="RTT 上行通道号（与固件一致）")
    parser.add_argument("--device", default="AT32F435RGT7", help="J-Link 器件名")
    parser.add_argument("--serial", type=int, default=None, help="J-Link 序列号（多探针时指定）")
    parser.add_argument("--jlink-dir", default=r"C:\Program Files\SEGGER\JLink_V810g",
                        help="J-Link 安装目录（用于定位 JLinkARM.dll）")
    parser.add_argument("--csv", default=None, help="同时把样本写入 CSV")
    parser.add_argument("--duration", type=float, default=None, help="运行多少秒后自动退出（测试用）")
    args = parser.parse_args()

    if args.selftest:
        return run_selftest()

    if args.decimate < 1:
        print("[ERROR] --decimate 必须 >= 1")
        return 2

    if args.demo:
        source = DemoSource()
    else:
        try:
            source = JLinkSource(args.device, args.channel, args.jlink_dir, args.serial)
        except SystemExit:
            raise
        except Exception as exc:
            print("[ERROR] 连接 J-Link 失败：%s" % exc)
            print("        逐项检查：")
            print("          1) USB 是否插好、J-Link 指示灯是否亮")
            print("          2) 调试器是否已被占用（关掉 RTT Viewer / Keil 调试会话）")
            print("          3) --jlink-dir 是否指向实际安装目录")
            print("          4) 多探针时用 --serial 指定序列号")
            print("        只想验证脚本可用：python script/rtt_to_tcp.py --demo")
            return 2

    run(source, args.port, args.decimate, args.csv, args.duration)
    return 0


if __name__ == "__main__":
    sys.exit(main())
