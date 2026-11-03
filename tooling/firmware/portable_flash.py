"""Portable, factory-only updater for an already provisioned Yumo ESP32-S3.

The manifest is an integrity record, not a publisher signature. Only use a bundle
from a trusted source. --dry-run never imports esptool or opens a serial port.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile


ESPTOOL_VERSION = "5.4.0"
FLASH_SIZE = 0x1000000
APP_OFFSET = 0x20000
APP_SIZE = 0x950000
TABLE_OFFSET = 0x8000
TABLE_SIZE = 0x1000
EXPECTED_LAYOUT = (
    ("nvs", 1, 2, 0x9000, 0x4000, 0),
    ("otadata", 1, 0, 0xD000, 0x2000, 0),
    ("phy_init", 1, 1, 0xF000, 0x1000, 0),
    ("factory", 0, 0, APP_OFFSET, APP_SIZE, 0),
    ("assets", 1, 0x82, 0x970000, 0x550000, 0),
    ("vault", 1, 0x40, 0xEC0000, 0x100000, 0),
)


class FlashError(RuntimeError):
    """Stop before an unsafe or unverifiable operation."""


@dataclass(frozen=True)
class Bundle:
    root: Path
    image: bytes
    table: bytes
    app_sha256: str
    table_sha256: str

    @property
    def erase_end(self):
        return APP_OFFSET + ((len(self.image) + 4095) & ~4095)

    @property
    def protected_ranges(self):
        # Includes bootloader/table/NVS/otadata/PHY, all unmodified app tail,
        # assets/vault and the final reserved area. Hashes never leave memory.
        return ((0, APP_OFFSET), (self.erase_end, FLASH_SIZE - self.erase_end))


def require(condition, message):
    if not condition:
        raise FlashError(message)


def _int(value, expected, label):
    require(type(value) is int and value == expected, f"清单中的{label}不匹配，已停止。")


def _inside_file(root, name, expected):
    require(name == expected, f"清单必须使用 {expected}，不能替换烧录目标。")
    path = (root / name).resolve()
    require(path.parent == root and path.is_file(), f"文件缺失或位于包目录之外：{expected}")
    return path


def pinned_config(root):
    root = Path(root).resolve()
    cfg = _inside_file(root, "esptool.cfg", "esptool.cfg")
    try:
        require(cfg.read_text(encoding="utf-8-sig").strip() == "[esptool]",
                "esptool.cfg必须是升级包附带的默认配置，不能加入自定义指令。")
    except OSError as exc:
        raise FlashError("无法读取包内esptool.cfg。") from exc
    return cfg


def parse_partition_table(raw):
    require(len(raw) in (0xC00, TABLE_SIZE), "分区表长度不正确。")
    layout = []
    for at in range(0, len(raw), 32):
        chunk = raw[at:at + 32]
        magic = int.from_bytes(chunk[:2], "little")
        if magic == 0xEBEB:
            require(chunk[2:16] == b"\xff" * 14, "分区表MD5记录格式不正确。")
            require(chunk[16:] == hashlib.md5(raw[:at]).digest(), "分区表MD5校验失败。")
            require(raw[at + 32:] == b"\xff" * (len(raw) - at - 32), "分区表尾部不是空闲空间。")
            require(tuple(layout) == EXPECTED_LAYOUT, "设备分区布局不兼容；本工具不会修改分区表。")
            return tuple(layout)
        require(magic == 0x50AA, "分区表为空或损坏；本工具仅用于已有系统的设备。")
        _, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", chunk)
        try:
            label = label.rstrip(b"\0").decode("ascii")
        except UnicodeDecodeError as exc:
            raise FlashError("分区名称不正确。") from exc
        layout.append((label, kind, subtype, offset, size, flags))
    raise FlashError("分区表缺少MD5校验记录。")


def validate_image(raw):
    require(32 <= len(raw) <= APP_SIZE, "固件大小超出factory分区。")
    require(raw[0] == 0xE9 and 1 <= raw[1] <= 16, "不是受支持的ESP应用镜像。")
    require(int.from_bytes(raw[12:14], "little") == 9, "固件不是ESP32-S3镜像。")
    require(raw[23] == 1, "镜像必须带有SHA256摘要。")
    require(raw[2] == 2 and raw[3] == 0x4F, "镜像Flash配置不是本设备的DIO/80MHz/16MB。")
    checksum = 0xEF
    pos = 24
    first = None
    for _ in range(raw[1]):
        require(pos + 8 <= len(raw), "镜像段头不完整。")
        _, length = struct.unpack_from("<II", raw, pos)
        pos += 8
        require(length % 4 == 0 and length <= len(raw) - pos, "镜像段内容不完整。")
        segment = raw[pos:pos + length]
        if first is None:
            first = segment
        for value in segment:
            checksum ^= value
        pos += length
    require(first is not None and len(first) >= 256, "镜像缺少应用描述信息。")
    require(first[:4] == struct.pack("<I", 0xABCD5432), "不能把bootloader或合并镜像当作应用烧录。")
    require(first[48:80].split(b"\0", 1)[0] == b"01_factory", "镜像项目名称与本设备系统不一致。")
    footer = (pos + 15) & ~15
    if footer == pos:
        footer += 16
    require(footer + 32 == len(raw), "镜像尾部长度或摘要格式不正确。")
    require(raw[footer - 1] == checksum, "镜像段校验和不正确。")
    require(raw[footer:] == hashlib.sha256(raw[:footer]).digest(), "镜像内置SHA256校验失败。")


def load_bundle(root):
    root = Path(root).resolve()
    try:
        manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8-sig"))
        require(isinstance(manifest, dict), "清单内容格式不正确。")
        _int(manifest.get("schema"), 1, "版本")
        require(manifest.get("chip") == "esp32s3", "清单芯片必须是esp32s3。")
        require(manifest.get("esptool_version") == ESPTOOL_VERSION, "清单esptool版本不匹配。")
        _int(manifest.get("flash_size"), FLASH_SIZE, "Flash容量")
        app = manifest["app"]
        table = manifest["partition_table"]
        require(isinstance(app, dict) and isinstance(table, dict), "镜像清单格式不正确。")
        _int(app.get("offset"), APP_OFFSET, "系统分区地址")
        _int(app.get("partition_size"), APP_SIZE, "系统分区容量")
        _int(table.get("offset"), TABLE_OFFSET, "分区表地址")
        _int(table.get("read_size"), TABLE_SIZE, "分区表读取长度")
        _int(table.get("bytes"), 0xC00, "分区表文件长度")
        require(type(app.get("bytes")) is int and 32 <= app["bytes"] <= APP_SIZE, "固件文件长度不正确。")
        image_path = _inside_file(root, app.get("file"), "01_factory.bin")
        table_path = _inside_file(root, table.get("file"), "partition-table.bin")
        require(image_path.stat().st_size == app["bytes"], "固件文件长度与清单不符。")
        require(table_path.stat().st_size == table["bytes"], "分区表文件长度与清单不符。")
        image, table_bytes = image_path.read_bytes(), table_path.read_bytes()
        for item, raw in ((app, image), (table, table_bytes)):
            digest = item.get("sha256")
            require(isinstance(digest, str) and re.fullmatch(r"[0-9a-f]{64}", digest), "清单SHA256格式不正确。")
            require(hashlib.sha256(raw).hexdigest() == digest, "包内文件SHA256不匹配，请重新解压完整安装包。")
        parse_partition_table(table_bytes)
        validate_image(image)
        pinned_config(root)
        bundle = Bundle(root, image, table_bytes, app["sha256"], table["sha256"])
        require(bundle.erase_end <= APP_OFFSET + APP_SIZE, "固件擦写范围超出了系统分区。")
        return bundle
    except (OSError, KeyError, TypeError, ValueError) as exc:
        raise FlashError(f"无法读取或验证完整烧录包：{exc}") from exc


def enumerate_ports():
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise FlashError("缺少pyserial，请使用完整包内的Flash.cmd。") from exc
    # list_ports only asks the OS for metadata; it does not open/reset devices.
    return sorted(list(list_ports.comports()), key=lambda item: item.device.casefold())


def port_label(port):
    description = str(getattr(port, "description", "") or "USB串口")
    return f"{port.device}  {description}"


def is_target_port(port):
    return getattr(port, "vid", None) == 0x303A and getattr(port, "pid", None) == 0x1001


def choose_port(ports, requested=None, *, interactive=False, input_fn=input, emit=print):
    ports = list(ports)
    if requested:
        matches = [p for p in ports if p.device.casefold() == requested.casefold()]
        require(len(matches) == 1, "指定串口不在本机端口列表中；请先使用 --list-ports 查看。")
        # Explicit choice permits USB/UART bridges, but never arbitrary URLs or
        # an unenumerated endpoint; chip/table checks are still mandatory.
        return matches[0]
    candidates = [p for p in ports if is_target_port(p)]
    require(candidates, "没有找到ESP32-S3 USB串口；检查数据线、驱动和设备USB模式，必要时按住BOOT后复位。可用 --list-ports 查看端口。")
    if len(candidates) == 1:
        return candidates[0]
    emit("检测到多个ESP32 USB端口，请明确选择设备：")
    for number, port in enumerate(candidates, 1):
        emit(f"  {number}. {port_label(port)}")
    require(interactive, "存在多个候选串口，已停止。请使用 --port COM号 明确指定。")
    try:
        choice = input_fn("输入对应序号（回车取消）：").strip()
    except EOFError as exc:
        raise FlashError("没有选择串口，已取消。") from exc
    require(choice.isascii() and choice.isdecimal() and 1 <= int(choice) <= len(candidates), "没有选择有效串口，已取消。")
    return candidates[int(choice) - 1]


class EspDevice:
    """One pinned esptool API connection; no intermediate USB reset/reopen."""

    def __init__(self, bundle_root=None):
        # esptool reads environment/CWD/home configuration during its first
        # import. -I alone does not prevent that. Pin and validate the packaged
        # empty configuration before importing, then verify the resolved parser.
        cfg = pinned_config(bundle_root or Path(__file__).resolve().parent)
        os.environ["ESPTOOL_CFGFILE"] = str(cfg)
        try:
            import esptool
            from esptool import cmds
            from esptool.targets.esp32s3 import ESP32S3ROM
            from esptool.config import load_config_file
        except ImportError as exc:
            raise FlashError("缺少esptool依赖，请使用完整包内的Flash.cmd。") from exc
        require(esptool.__version__ == ESPTOOL_VERSION, f"需要esptool {ESPTOOL_VERSION}，请使用包内运行环境。")
        parsed, loaded_path = load_config_file()
        require(loaded_path is not None and Path(loaded_path).resolve() == cfg,
                "esptool已加载外部配置，请关闭此进程后使用Flash.cmd重新启动。")
        require(parsed.sections() == ["esptool"] and not parsed.defaults() and not list(parsed.items("esptool")),
                "esptool配置不是升级包内的默认值，已停止。")
        self.cmds = cmds
        self.rom_class = ESP32S3ROM
        self.esp = None

    def connect(self, port, baud):
        self.esp = self.rom_class(port, baud=115200)
        self.esp.connect(mode="default-reset", attempts=7)
        require(self.esp.get_chip_id() == 9 and self.esp.CHIP_NAME == "ESP32-S3", "连接的芯片不是ESP32-S3。")
        require(not self.esp.secure_download_mode, "安全下载模式设备不适用于本升级包。")
        require(not self.esp.get_flash_encryption_enabled(), "设备启用了Flash加密，本升级包无法安全更新。")
        require(not self.esp.get_secure_boot_enabled(), "设备启用了安全启动，本升级包无法安全更新。")
        self.esp = self.cmds.run_stub(self.esp)
        require(self.esp.IS_STUB, "无法启动本设备所需的烧录助手。")
        # An esptool write retry would reopen the same COM name without our
        # chip/table/data checks. Disconnection must end this update attempt.
        self.esp.WRITE_FLASH_ATTEMPTS = 1
        self.cmds.attach_flash(self.esp)
        require(self.cmds.detect_flash_size(self.esp) == "16MB", "设备Flash不是16MB，已停止。")
        self.esp.flash_set_parameters(FLASH_SIZE)
        if baud != 115200:
            self.esp.change_baud(baud)

    def read_table(self):
        return bytes(self.cmds.read_flash(self.esp, TABLE_OFFSET, TABLE_SIZE, flash_size="keep", no_progress=True))

    def digest(self, offset, size):
        result = self.esp.flash_md5sum(offset, size)
        if isinstance(result, bytes):
            result = result.decode("ascii")
        require(isinstance(result, str) and re.fullmatch(r"[0-9a-fA-F]{32}", result), "设备数据校验返回异常。")
        return result.lower()

    def write_app(self, image):
        self.esp.WRITE_FLASH_ATTEMPTS = 1
        self.cmds.write_flash(self.esp, [(APP_OFFSET, image)], flash_freq="keep", flash_mode="keep", flash_size="keep", compress=True)

    def reset(self):
        self.esp.hard_reset()

    def close(self):
        if self.esp is not None:
            self.esp._port.close()


def flash_bundle(bundle, port, baud, state, *, device_factory=None, emit=print):
    device = None
    state.update(write_started=False, flashed=False, app_verified=False,
                 protected_data_unchanged=None, reset_completed=False)
    try:
        state["stage"] = "connect"
        device = device_factory() if device_factory else EspDevice(bundle.root)
        device.connect(port.device, baud)
        state["stage"] = "partition-check"
        raw = device.read_table()
        require(len(raw) == TABLE_SIZE, "读取设备分区表不完整。")
        parse_partition_table(raw)
        require(raw == bundle.table.ljust(TABLE_SIZE, b"\xff"), "设备分区表与此包不一致；未写入任何分区。")
        state["partition_table_matches"] = True
        state["stage"] = "data-precheck"
        emit("正在核对非系统区域，密码与其他数据只在设备端计算摘要。")
        before = [device.digest(offset, size) for offset, size in bundle.protected_ranges]
        state["stage"] = "write-app"
        state["write_started"] = True
        emit(f"仅更新系统factory分区：{len(bundle.image):,}字节。请保持连接。")
        device.write_app(bundle.image)
        state["flashed"] = True
        state["stage"] = "verify-app"
        require(device.digest(APP_OFFSET, len(bundle.image)) == hashlib.md5(bundle.image).hexdigest(), "系统写后校验失败；请保持设备连接并重新运行升级包。")
        state["app_verified"] = True
        state["stage"] = "data-postcheck"
        after = [device.digest(offset, size) for offset, size in bundle.protected_ranges]
        state["protected_data_unchanged"] = before == after
        require(before == after, "非系统区域校验不一致，已停止自动重启，请保留日志排查。")
        state["stage"] = "reset"
        device.reset()
        state["reset_completed"] = True
        state["stage"] = "complete"
    finally:
        if device is not None:
            device.close()


def save_receipt(root, state):
    # No raw flash dumps, MAC addresses, USB serial numbers or protected hashes.
    try:
        directory = Path(root) / "logs"
        directory.mkdir(parents=True, exist_ok=True)
        target = directory / ("flash-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".json")
        target.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return target
    except OSError:
        with tempfile.NamedTemporaryFile(mode="w", prefix="yumo-flash-", suffix=".json", encoding="utf-8", delete=False) as stream:
            json.dump(state, stream, ensure_ascii=False, indent=2)
            return Path(stream.name)


def main(argv=None):
    parser = argparse.ArgumentParser(description="雨墨ESP32-S3便携系统升级；保留其他分区和SD卡。", allow_abbrev=False)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true", help="仅校验本地文件，不枚举或连接设备")
    mode.add_argument("--list-ports", action="store_true", help="仅列出本机串口，不打开设备")
    parser.add_argument("--port", help="明确选择串口，例如COM8")
    parser.add_argument("--baud", type=int, choices=(115200, 230400, 460800, 921600), default=460800)
    parser.add_argument("--bundle", type=Path, default=Path(__file__).resolve().parent, help="升级包目录，默认脚本所在目录")
    args = parser.parse_args(argv)
    state = {"schema": 1, "started": datetime.now().astimezone().isoformat(), "status": "running", "stage": "local-preflight", "flashed": False}
    operation_started = False
    try:
        if args.list_ports:
            ports = enumerate_ports()
            for port in ports:
                print(port_label(port) + ("  [ESP32 USB]" if is_target_port(port) else ""))
            if not ports:
                print("未发现串口。")
            return 0
        bundle = load_bundle(args.bundle)
        state.update(firmware_sha256=bundle.app_sha256, firmware_bytes=len(bundle.image),
                     write_offset=APP_OFFSET, write_end=APP_OFFSET + len(bundle.image),
                     erase_end=bundle.erase_end, esptool_version=ESPTOOL_VERSION)
        if args.dry_run:
            print(f"本地校验通过：ESP32-S3，系统镜像 {len(bundle.image):,}字节。")
            print(f"写入地址 0x{APP_OFFSET:X}，扇区擦写终点 0x{bundle.erase_end:X}（不含终点）。")
            print("此操作没有枚举或连接设备，未执行烧录。")
            return 0
        port = choose_port(enumerate_ports(), args.port, interactive=sys.stdin.isatty())
        state["port"] = port.device
        print(f"已选择 {port_label(port)}；校验设备后开始系统更新。")
        operation_started = True
        flash_bundle(bundle, port, args.baud, state)
        state["status"] = "passed"
        print("系统更新及校验完成，其他Flash区域未变，设备已重启；SD卡未访问。")
        return 0
    except KeyboardInterrupt:
        state.update(status="cancelled", error="用户取消")
        print("操作已取消。若已开始写入，请保持连接并重新运行升级包。", file=sys.stderr)
        return 130
    except Exception as exc:
        state.update(status="failed", error=str(exc))
        print(f"停止：{exc}", file=sys.stderr)
        if state.get("write_started"):
            print("已进入系统写入阶段，可能尚未写完；其他分区没有写入命令。请保留日志后重试。", file=sys.stderr)
        return 1
    finally:
        if operation_started:
            state["finished"] = datetime.now().astimezone().isoformat()
            try:
                print(f"记录：{save_receipt(args.bundle, state)}")
            except OSError:
                print("无法保存运行记录，请保留以上终端输出。", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
