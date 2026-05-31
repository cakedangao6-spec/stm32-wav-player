#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Receive W25Q64 readback data from STM32 USART1 and compare it with 666.wav.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
import zlib
from pathlib import Path


DEFAULT_SIZE = 301_810
DEFAULT_BAUD = 115_200
WORKSPACE_ROOT = Path(__file__).resolve().parents[2]
AUDIO_DIR = WORKSPACE_ROOT / "音频素材"
ARTIFACT_DIR = WORKSPACE_ROOT / "验证产物"
DEFAULT_SOURCE = AUDIO_DIR / "666.wav"
DEFAULT_OUTPUT = ARTIFACT_DIR / "readback.wav"


def require_pyserial():
    try:
        import serial  # type: ignore
        from serial.tools import list_ports  # type: ignore
    except ImportError as exc:
        raise RuntimeError(
            "缺少 pyserial，请先运行：python -m pip install pyserial"
        ) from exc
    return serial, list_ports


def list_serial_ports() -> list:
    _, list_ports = require_pyserial()
    return list(list_ports.comports())


def choose_serial_port(port_arg: str | None) -> str:
    if port_arg:
        return port_arg

    ports = list_serial_ports()
    if not ports:
        raise RuntimeError("没有发现串口。请确认 USB-TTL 或带 VCP 的 ST-LINK 已连接。")

    if len(ports) == 1:
        return ports[0].device

    keywords = (
        "stlink",
        "st-link",
        "stmicroelectronics",
        "usb serial",
        "usb-serial",
        "ch340",
        "wch",
        "cp210",
        "silicon labs",
        "ftdi",
    )

    ranked = []
    for info in ports:
        text = f"{info.device} {info.description} {info.manufacturer or ''}".lower()
        score = sum(1 for key in keywords if key in text)
        if score:
            ranked.append((score, info))

    ranked.sort(key=lambda item: item[0], reverse=True)
    if len(ranked) == 1 or (ranked and ranked[0][0] > ranked[1][0]):
        return ranked[0][1].device

    lines = ["发现多个串口，无法安全自动选择，请用 --port 指定："]
    for info in ports:
        lines.append(f"  {info.device}: {info.description}")
    raise RuntimeError("\n".join(lines))


def read_line(ser, timeout_s: float) -> str | None:
    deadline = time.monotonic() + timeout_s
    data = bytearray()

    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        data += b
        if b == b"\n":
            return data.decode("ascii", errors="replace").strip()

    if data:
        return data.decode("ascii", errors="replace").strip()
    return None


def wait_ready(ser, expected_size: int, timeout_s: float) -> int:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        line = read_line(ser, 0.5)
        if not line:
            continue

        print(f"STM32: {line}")
        if line.startswith("ERR:"):
            raise RuntimeError(f"STM32 返回错误：{line}")

        if line.startswith("READY:"):
            size_text = line.split(":", 1)[1]
            ready_size = int(size_text, 10)
            if ready_size != expected_size:
                raise RuntimeError(f"STM32 READY 大小不匹配：{ready_size} != {expected_size}")
            return ready_size

    raise RuntimeError("等待 STM32 READY 超时。请确认固件已烧录、串口接线正确、波特率一致。")


def synchronize_device(ser, timeout_s: float) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        ser.write(b"PING\n")
        ser.flush()

        inner_deadline = min(deadline, time.monotonic() + 0.6)
        while time.monotonic() < inner_deadline:
            line = read_line(ser, 0.2)
            if not line:
                continue
            print(f"STM32: {line}")
            if line.startswith(("VERIFY_FW_READY", "DONE:", "ERR:")):
                continue
            if line == "PONG":
                return True
    return False


def receive_readback(port: str, baud: int, size: int, output: Path, boot_wait: float, sync_timeout: float, ready_timeout: float, stall_timeout: float):
    serial, _ = require_pyserial()

    print(f"打开串口: {port}, {baud} baud")
    with serial.Serial(port=port, baudrate=baud, timeout=0.2, write_timeout=2) as ser:
        time.sleep(boot_wait)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        if sync_timeout > 0:
            print(f"与 STM32 握手，最长 {sync_timeout:.1f} 秒...")
            if not synchronize_device(ser, sync_timeout):
                raise RuntimeError("STM32 PING 握手超时。请确认已烧录最新验证固件。")

        command = f"READBACK:{size}\n".encode("ascii")
        print(f"发送命令: {command.decode('ascii').strip()}")
        ser.write(command)
        ser.flush()

        wait_ready(ser, size, ready_timeout)

        print(f"开始接收 {size} 字节...")
        output.parent.mkdir(parents=True, exist_ok=True)
        received = 0
        last_data_time = time.monotonic()
        last_report_time = 0.0

        with output.open("wb") as fp:
            while received < size:
                need = min(4096, size - received)
                chunk = ser.read(need)
                now = time.monotonic()

                if chunk:
                    fp.write(chunk)
                    received += len(chunk)
                    last_data_time = now

                    if now - last_report_time >= 0.5 or received == size:
                        percent = received * 100.0 / size
                        print(f"接收进度: {received}/{size} ({percent:.1f}%)")
                        last_report_time = now
                    continue

                if now - last_data_time > stall_timeout:
                    raise RuntimeError(f"串口接收停滞超时，已接收 {received}/{size} 字节")

        done_line = None
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            line = read_line(ser, 0.5)
            if not line:
                continue
            print(f"STM32: {line}")
            if line.startswith("DONE:") or line.startswith("ERR:"):
                done_line = line
                break

        if done_line is None:
            print("警告: 未收到 STM32 DONE 行，但已接收指定长度数据，继续文件比对。")
        elif done_line.startswith("ERR:"):
            raise RuntimeError(f"STM32 在发送后返回错误：{done_line}")

    print(f"已保存: {output}")


def digest_file(path: Path) -> tuple[str, int]:
    md5 = hashlib.md5()
    crc = 0
    with path.open("rb") as fp:
        while True:
            chunk = fp.read(1024 * 1024)
            if not chunk:
                break
            md5.update(chunk)
            crc = zlib.crc32(chunk, crc)
    return md5.hexdigest(), crc & 0xFFFFFFFF


def compare_files(source: Path, readback: Path) -> bool:
    if not source.exists():
        raise FileNotFoundError(f"原始文件不存在: {source}")
    if not readback.exists():
        raise FileNotFoundError(f"回读文件不存在: {readback}")

    source_size = source.stat().st_size
    readback_size = readback.stat().st_size
    source_md5, source_crc32 = digest_file(source)
    readback_md5, readback_crc32 = digest_file(readback)

    first_error = None
    diff_count = 0
    offset = 0

    with source.open("rb") as fa, readback.open("rb") as fb:
        while True:
            a = fa.read(64 * 1024)
            b = fb.read(64 * 1024)
            if not a and not b:
                break

            common_len = min(len(a), len(b))
            if a[:common_len] != b[:common_len]:
                for i in range(common_len):
                    if a[i] != b[i]:
                        if first_error is None:
                            first_error = offset + i
                        diff_count += 1

            if len(a) != len(b):
                if first_error is None:
                    first_error = offset + common_len
                diff_count += abs(len(a) - len(b))

            offset += max(len(a), len(b))

    size_same = source_size == readback_size
    md5_same = source_md5 == readback_md5
    crc_same = source_crc32 == readback_crc32
    byte_same = diff_count == 0 and size_same
    passed = size_same and md5_same and crc_same and byte_same

    print()
    print("========== 比对结果 ==========")
    print(f"原始文件: {source}")
    print(f"回读文件: {readback}")
    print(f"文件大小是否一致: {'是' if size_same else '否'} (原始={source_size}, 回读={readback_size})")
    print(f"MD5是否一致: {'是' if md5_same else '否'}")
    print(f"  原始 MD5: {source_md5}")
    print(f"  回读 MD5: {readback_md5}")
    print(f"CRC32是否一致: {'是' if crc_same else '否'}")
    print(f"  原始 CRC32: 0x{source_crc32:08X}")
    print(f"  回读 CRC32: 0x{readback_crc32:08X}")
    print(f"逐字节是否一致: {'是' if byte_same else '否'}")
    print(f"不同字节数量: {diff_count}")
    print(f"第一个错误位置: {'无' if first_error is None else first_error}")

    if passed:
        print()
        print("PASS")
        print("W25Q64内容完全正确")
    else:
        print()
        print("FAIL")
        print("W25Q64内容与原始文件不一致")

    return passed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Receive STM32 W25Q64 readback and compare with original WAV.")
    parser.add_argument("--port", help="串口号，例如 COM3。未指定时尝试自动识别。")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"串口波特率，默认 {DEFAULT_BAUD}")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, help=f"读取字节数，默认 {DEFAULT_SIZE}")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help="原始文件路径")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT, help="保存回读文件路径")
    parser.add_argument("--compare-only", action="store_true", help="不打开串口，只比较 source 和 output")
    parser.add_argument("--list-ports", action="store_true", help="列出串口后退出")
    parser.add_argument("--boot-wait", type=float, default=0.5, help="打开串口后等待设备启动的秒数")
    parser.add_argument("--sync-timeout", type=float, default=8.0, help="发送命令前与 STM32 做 PING 握手的秒数，0 表示不等待")
    parser.add_argument("--ready-timeout", type=float, default=8.0, help="等待 READY 行的秒数")
    parser.add_argument("--stall-timeout", type=float, default=5.0, help="接收过程中无数据超时秒数")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    try:
        if args.list_ports:
            ports = list_serial_ports()
            if not ports:
                print("未发现串口")
                return 1
            for info in ports:
                print(f"{info.device}: {info.description}")
            return 0

        if not args.compare_only:
            port = choose_serial_port(args.port)
            receive_readback(
                port=port,
                baud=args.baud,
                size=args.size,
                output=args.output,
                boot_wait=args.boot_wait,
                sync_timeout=args.sync_timeout,
                ready_timeout=args.ready_timeout,
                stall_timeout=args.stall_timeout,
            )

        return 0 if compare_files(args.source, args.output) else 1

    except Exception as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
