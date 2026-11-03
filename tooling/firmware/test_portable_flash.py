"""Host-only failure-path tests. Never opens or resets a serial port."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("yumo_portable_flash_tested", HERE / "portable_flash.py")
flash = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = flash
SPEC.loader.exec_module(flash)


def image_bytes(project=b"01_factory", chip=9, extra=b""):
    header = bytearray(24)
    header[0:4] = bytes((0xE9, 1, 2, 0x4F))
    header[12:14] = struct.pack("<H", chip)
    header[23] = 1
    segment = bytearray(256)
    segment[:4] = struct.pack("<I", 0xABCD5432)
    segment[48:48 + len(project)] = project
    segment.extend(extra)
    raw = header + struct.pack("<II", 0x3C000020, len(segment)) + segment
    checksum = 0xEF
    for value in segment:
        checksum ^= value
    raw.extend(b"\0" * ((15 - len(raw) % 16) % 16))
    raw.append(checksum)
    return bytes(raw) + hashlib.sha256(raw).digest()


def table_bytes(layout=flash.EXPECTED_LAYOUT, size=0xC00):
    raw = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, sub, off, length,
                              name.encode("ascii"), flags)
                   for name, kind, sub, off, length, flags in layout)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw.ljust(size, b"\xff")


def make_manifest(image, table):
    return {"schema": 1, "chip": "esp32s3", "esptool_version": "5.4.0",
            "flash_size": flash.FLASH_SIZE,
            "app": {"file": "01_factory.bin", "offset": flash.APP_OFFSET,
                    "partition_size": flash.APP_SIZE, "bytes": len(image),
                    "sha256": hashlib.sha256(image).hexdigest()},
            "partition_table": {"file": "partition-table.bin", "offset": flash.TABLE_OFFSET,
                                "bytes": len(table), "read_size": flash.TABLE_SIZE,
                                "sha256": hashlib.sha256(table).hexdigest()}}


def port(name="COM42", vid=0x303A, pid=0x1001):
    return SimpleNamespace(device=name, vid=vid, pid=pid, description="Test USB")


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="yumo portable 空格 ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.image, self.table = image_bytes(), table_bytes()
        self.manifest = make_manifest(self.image, self.table)
        (self.root / "01_factory.bin").write_bytes(self.image)
        (self.root / "partition-table.bin").write_bytes(self.table)
        (self.root / "esptool.cfg").write_text("[esptool]\n", encoding="utf-8")
        self.write_manifest()

    def write_manifest(self):
        (self.root / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_valid_bundle_and_sector_guard(self):
        bundle = flash.load_bundle(self.root)
        self.assertEqual(bundle.image, self.image)
        self.assertEqual(bundle.erase_end, 0x21000)
        self.assertEqual(bundle.protected_ranges, ((0, 0x20000), (0x21000, 0xFDF000)))

    def test_manifest_cannot_redirect_any_write_region(self):
        cases = [("app", "offset", 0), ("app", "partition_size", flash.FLASH_SIZE),
                 ("app", "file", "../bootloader.bin"),
                 ("partition_table", "file", str(self.root / "partition-table.bin")),
                 ("partition_table", "read_size", 32), ("partition_table", "bytes", 4096),
                 ("partition_table", "offset", 0)]
        original = copy.deepcopy(self.manifest)
        for group, key, value in cases:
            with self.subTest(group=group, key=key):
                self.manifest = copy.deepcopy(original)
                self.manifest[group][key] = value
                self.write_manifest()
                with self.assertRaises(flash.FlashError):
                    flash.load_bundle(self.root)

    def test_manifest_wrong_versions_size_types_rejected(self):
        for key, value in (("schema", True), ("chip", "esp32"), ("flash_size", 0x800000),
                           ("esptool_version", "5.0.0")):
            with self.subTest(key=key):
                self.manifest = make_manifest(self.image, self.table)
                self.manifest[key] = value
                self.write_manifest()
                with self.assertRaises(flash.FlashError):
                    flash.load_bundle(self.root)

    def test_modified_image_rejected_before_connect(self):
        corrupt = bytearray(self.image)
        corrupt[48] ^= 1
        (self.root / "01_factory.bin").write_bytes(corrupt)
        with patch.object(flash, "enumerate_ports", side_effect=AssertionError("hardware touched")):
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(flash.main(["--bundle", str(self.root)]), 1)

    def test_manifest_size_and_hash_must_both_match(self):
        for key, value in (("bytes", len(self.image) - 1), ("sha256", "0" * 64),
                           ("sha256", "BAD"), ("bytes", True)):
            with self.subTest(key=key, value=value):
                self.manifest = make_manifest(self.image, self.table)
                self.manifest["app"][key] = value
                self.write_manifest()
                with self.assertRaises(flash.FlashError):
                    flash.load_bundle(self.root)

    def test_dry_run_never_enumerates_or_connects(self):
        with patch.object(flash, "enumerate_ports", side_effect=AssertionError("enumerated")), \
             patch.object(flash, "EspDevice", side_effect=AssertionError("connected")), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(flash.main(["--dry-run", "--bundle", str(self.root)]), 0)
        self.assertFalse((self.root / "logs").exists())

    def test_list_ports_does_not_require_bundle_or_connect(self):
        with patch.object(flash, "enumerate_ports", return_value=[port()]), \
             patch.object(flash, "load_bundle", side_effect=AssertionError("loaded")), \
             patch.object(flash, "EspDevice", side_effect=AssertionError("connected")), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(flash.main(["--list-ports"]), 0)

    def test_missing_or_modified_esptool_configuration_rejected(self):
        cfg = self.root / "esptool.cfg"
        for content in ("", "[esptool]\ncustom_reset_sequence = D1", "[DEFAULT]\ntimeout=1\n[esptool]"):
            with self.subTest(content=content):
                cfg.write_text(content, encoding="utf-8")
                with self.assertRaises(flash.FlashError):
                    flash.load_bundle(self.root)
        cfg.unlink()
        with self.assertRaises(flash.FlashError):
            flash.load_bundle(self.root)

    def test_unsafe_options_and_abbreviations_not_exposed(self):
        for option in ("--force", "--erase-all", "--offset", "--encrypt", "--dry"):
            with self.subTest(option=option), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    flash.main([option])
                self.assertEqual(raised.exception.code, 2)


class ImageAndTableTests(unittest.TestCase):
    def test_good_checksums_with_different_alignment(self):
        for size in (0, 4, 8, 12, 16, 32):
            with self.subTest(size=size):
                flash.validate_image(image_bytes(extra=b"\0" * size))

    def test_chip_bootloader_and_foreign_app_rejected(self):
        cases = [image_bytes(chip=0), image_bytes(project=b"foreign_app")]
        raw = bytearray(image_bytes())
        raw[32:36] = b"\0" * 4
        cases.append(bytes(raw))
        for raw in cases:
            with self.assertRaises(flash.FlashError):
                flash.validate_image(raw)

    def test_segment_truncation_checksum_and_digest_failures(self):
        good = image_bytes()
        cases = [good[:20], good[:-1], good + b"\0", b"\0" * 300]
        for offset in (1, 2, 3, 23, 28, len(good) - 33, len(good) - 1):
            raw = bytearray(good)
            raw[offset] ^= 0x80
            cases.append(bytes(raw))
        for number, raw in enumerate(cases):
            with self.subTest(number=number), self.assertRaises(flash.FlashError):
                flash.validate_image(raw)

    def test_oversized_image_rejected(self):
        with self.assertRaises(flash.FlashError):
            flash.validate_image(b"\0" * (flash.APP_SIZE + 1))

    def test_partition_blank_missing_md5_and_padding_rejected(self):
        valid = table_bytes()
        cases = [b"\xff" * 4096, valid[:192].ljust(3072, b"\xff"), valid[:-1],
                 valid[:-1] + b"\0"]
        corrupt = bytearray(valid)
        corrupt[210] ^= 1
        cases.append(bytes(corrupt))
        for raw in cases:
            with self.assertRaises(flash.FlashError):
                flash.parse_partition_table(raw)

    def test_partition_legacy_flag_and_changed_data_layout_rejected(self):
        for index, field, value in ((3, 4, 0x550000), (0, 5, 1), (5, 3, 0xED0000),
                                     (3, 0, "ota_0"), (4, 2, 0x81)):
            with self.subTest(index=index, field=field):
                layout = [list(entry) for entry in flash.EXPECTED_LAYOUT]
                layout[index][field] = value
                with self.assertRaises(flash.FlashError):
                    flash.parse_partition_table(table_bytes(layout))


class PortTests(unittest.TestCase):
    def test_unique_matching_usb_chosen_while_others_ignored(self):
        target = port()
        self.assertIs(flash.choose_port([port("COM1", vid=1), target]), target)

    def test_no_target_fails_without_probing_other_ports(self):
        with self.assertRaises(flash.FlashError):
            flash.choose_port([port("COM1", vid=1)])

    def test_ambiguous_noninteractive_never_selects(self):
        with self.assertRaises(flash.FlashError):
            flash.choose_port([port(), port("COM43")], emit=lambda _: None)

    def test_interactive_selection_and_cancellation(self):
        targets = [port(), port("COM43")]
        selected = flash.choose_port(targets, interactive=True, input_fn=lambda _: "2", emit=lambda _: None)
        self.assertIs(selected, targets[1])
        for value in ("", "0", "3", "COM42", "２"):
            with self.subTest(value=value), self.assertRaises(flash.FlashError):
                flash.choose_port(targets, interactive=True, input_fn=lambda _: value, emit=lambda _: None)

    def test_explicit_enumerated_bridge_allowed_case_insensitive(self):
        bridge = port("COM5", vid=0x10C4)
        self.assertIs(flash.choose_port([bridge], "com5"), bridge)
        with self.assertRaises(flash.FlashError):
            flash.choose_port([bridge], "socket://example:443")


class FakeDevice:
    def __init__(self, bundle, fail=None):
        self.bundle, self.fail = bundle, fail
        self.calls = []
        self.written = False

    def connect(self, port_name, baud):
        self.calls.append(("connect", port_name, baud))
        if self.fail == "connect":
            raise flash.FlashError("Wrong chip or flash size")

    def read_table(self):
        self.calls.append(("read_table",))
        if self.fail == "table":
            return b"\xff" * 4096
        return self.bundle.table.ljust(4096, b"\xff")

    def digest(self, offset, size):
        self.calls.append(("digest", offset, size))
        if offset == flash.APP_OFFSET:
            return "f" * 32 if self.fail == "app_verify" else hashlib.md5(self.bundle.image).hexdigest()
        if self.fail == "pre_hash" and not self.written:
            raise flash.FlashError("read error")
        if self.fail == "protected" and self.written:
            return "d" * 32
        return "a" * 32

    def write_app(self, image):
        self.calls.append(("write_app", image))
        if self.fail == "interrupt":
            raise KeyboardInterrupt()
        if self.fail == "write":
            raise flash.FlashError("write interrupted")
        self.written = True

    def reset(self):
        self.calls.append(("reset",))
        if self.fail == "reset":
            raise flash.FlashError("reset failed")

    def close(self):
        self.calls.append(("close",))


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        image, table = image_bytes(), table_bytes()
        self.bundle = flash.Bundle(Path("not-used"), image, table,
                                   hashlib.sha256(image).hexdigest(), hashlib.sha256(table).hexdigest())

    def run_fake(self, fail=None):
        fake = FakeDevice(self.bundle, fail)
        state = {}
        error = None
        try:
            flash.flash_bundle(self.bundle, port(), 460800, state,
                               device_factory=lambda: fake, emit=lambda _: None)
        except BaseException as exc:
            error = exc
        return fake, state, error

    def test_single_write_and_all_protected_regions_before_after(self):
        fake, state, error = self.run_fake()
        self.assertIsNone(error)
        expected_hashes = [("digest", off, size) for off, size in self.bundle.protected_ranges]
        self.assertEqual(fake.calls, [("connect", "COM42", 460800), ("read_table",)] +
                         expected_hashes + [("write_app", self.bundle.image),
                         ("digest", flash.APP_OFFSET, len(self.bundle.image))] + expected_hashes +
                         [("reset",), ("close",)])
        self.assertTrue(state["app_verified"])
        self.assertTrue(state["protected_data_unchanged"])
        self.assertTrue(state["reset_completed"])
        self.assertNotIn("protected_hashes", state)

    def test_preflight_failures_never_write_or_reset(self):
        for reason in ("connect", "table", "pre_hash"):
            with self.subTest(reason=reason):
                fake, state, error = self.run_fake(reason)
                self.assertIsInstance(error, flash.FlashError)
                self.assertFalse(state["write_started"])
                self.assertFalse(any(c[0] in ("write_app", "reset") for c in fake.calls))
                self.assertEqual(fake.calls[-1], ("close",))

    def test_write_interruption_never_reports_verified_or_reboots(self):
        for reason in ("write", "interrupt"):
            with self.subTest(reason=reason):
                fake, state, error = self.run_fake(reason)
                self.assertIsNotNone(error)
                self.assertTrue(state["write_started"])
                self.assertFalse(state["flashed"])
                self.assertFalse(state["app_verified"])
                self.assertNotIn(("reset",), fake.calls)
                self.assertEqual(fake.calls[-1], ("close",))

    def test_bad_app_or_protected_hash_prevents_reset(self):
        for reason in ("app_verify", "protected"):
            with self.subTest(reason=reason):
                fake, state, error = self.run_fake(reason)
                self.assertIsInstance(error, flash.FlashError)
                self.assertTrue(state["flashed"])
                self.assertNotIn(("reset",), fake.calls)
                self.assertFalse(state["reset_completed"])
                self.assertEqual(fake.calls[-1], ("close",))

    def test_reset_failure_preserves_successful_verification_state(self):
        fake, state, error = self.run_fake("reset")
        self.assertIsInstance(error, flash.FlashError)
        self.assertTrue(state["flashed"])
        self.assertTrue(state["app_verified"])
        self.assertTrue(state["protected_data_unchanged"])
        self.assertFalse(state["reset_completed"])
        self.assertEqual(fake.calls[-1], ("close",))


class ApiAdapterTests(unittest.TestCase):
    """Exercise the production adapter with fake ROM objects, never a serial port."""

    def setUp(self):
        from unittest.mock import Mock
        self.rom = Mock()
        self.rom.get_chip_id.return_value = 9
        self.rom.CHIP_NAME = "ESP32-S3"
        self.rom.secure_download_mode = False
        self.rom.get_flash_encryption_enabled.return_value = False
        self.rom.get_secure_boot_enabled.return_value = False
        self.stub = Mock()
        self.stub.IS_STUB = True
        self.cmds = Mock()
        self.cmds.run_stub.return_value = self.stub
        self.cmds.detect_flash_size.return_value = "16MB"
        self.adapter = object.__new__(flash.EspDevice)
        self.adapter.cmds = self.cmds
        self.adapter.rom_class = Mock(return_value=self.rom)
        self.adapter.esp = None

    def test_success_connects_once_with_real_api_signature(self):
        self.adapter.connect("COM42", 460800)
        self.adapter.rom_class.assert_called_once_with("COM42", baud=115200)
        self.rom.connect.assert_called_once_with(mode="default-reset", attempts=7)
        self.cmds.run_stub.assert_called_once_with(self.rom)
        self.cmds.attach_flash.assert_called_once_with(self.stub)
        self.stub.flash_set_parameters.assert_called_once_with(0x1000000)
        self.stub.change_baud.assert_called_once_with(460800)
        self.assertEqual(self.stub.WRITE_FLASH_ATTEMPTS, 1)
        self.adapter.close()
        self.stub._port.close.assert_called_once()

    def test_chip_and_security_checks_happen_before_ram_stub(self):
        cases = (("chip", 0), ("name", "ESP32-C3"), ("download", True),
                 ("encryption", True), ("secure_boot", True))
        for name, value in cases:
            with self.subTest(name=name):
                self.setUp()
                if name == "chip":
                    self.rom.get_chip_id.return_value = value
                elif name == "name":
                    self.rom.CHIP_NAME = value
                elif name == "download":
                    self.rom.secure_download_mode = value
                elif name == "encryption":
                    self.rom.get_flash_encryption_enabled.return_value = value
                else:
                    self.rom.get_secure_boot_enabled.return_value = value
                with self.assertRaises(flash.FlashError):
                    self.adapter.connect("COM42", 460800)
                self.cmds.run_stub.assert_not_called()
                self.cmds.write_flash.assert_not_called()
                self.adapter.close()
                self.rom._port.close.assert_called_once()

    def test_missing_stub_or_wrong_flash_capacity_cannot_write(self):
        for value in (None, "8MB", "32MB"):
            with self.subTest(value=value):
                self.setUp()
                self.cmds.detect_flash_size.return_value = value
                with self.assertRaises(flash.FlashError):
                    self.adapter.connect("COM42", 460800)
                self.cmds.write_flash.assert_not_called()
        self.setUp()
        self.stub.IS_STUB = False
        with self.assertRaises(flash.FlashError):
            self.adapter.connect("COM42", 460800)
        self.cmds.attach_flash.assert_not_called()

    def test_write_api_has_one_fixed_range_no_security_bypass(self):
        self.adapter.esp = self.stub
        image = image_bytes()
        self.adapter.write_app(image)
        self.assertEqual(self.stub.WRITE_FLASH_ATTEMPTS, 1)
        self.cmds.write_flash.assert_called_once_with(
            self.stub, [(0x20000, image)], flash_freq="keep", flash_mode="keep",
            flash_size="keep", compress=True)

    def test_table_read_and_digest_errors_fail_closed(self):
        self.adapter.esp = self.stub
        self.cmds.read_flash.return_value = table_bytes(size=4096)
        self.assertEqual(len(self.adapter.read_table()), 4096)
        self.cmds.read_flash.assert_called_once_with(self.stub, 0x8000, 4096,
                                                    flash_size="keep", no_progress=True)
        for value in ("", "bad", "z" * 32, None):
            with self.subTest(value=value):
                self.stub.flash_md5sum.return_value = value
                with self.assertRaises(flash.FlashError):
                    self.adapter.digest(0, 0x20000)
        self.stub.flash_md5sum.return_value = b"A" * 32
        self.assertEqual(self.adapter.digest(0, 0x20000), "a" * 32)


class RealEsptoolIsolationTests(unittest.TestCase):
    """Runs real esptool code with injected transports, no actual hardware."""

    @classmethod
    def setUpClass(cls):
        if importlib.util.find_spec("esptool") is None:
            raise unittest.SkipTest("esptool5.4 runtime is required for adapter integration tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="yumo config isolation ")
        cls.root = Path(cls.temp.name)
        (cls.root / "esptool.cfg").write_text("[esptool]\n", encoding="utf-8")
        # Pin before first import in this test process. Constructors below never
        # call connect, so creating the production adapter cannot open a port.
        cls.adapter = flash.EspDevice(cls.root)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "temp"):
            cls.temp.cleanup()

    def test_real_write_serial_error_does_not_reopen_same_com(self):
        from unittest.mock import Mock
        from esptool import cmds
        from esptool.targets.esp32s3 import ESP32S3ROM
        from serial import SerialException
        esp = Mock()
        esp.CHIP_NAME = "ESP32-S3"
        esp.IMAGE_CHIP_ID = 9
        esp.IS_STUB = True
        esp.secure_download_mode = False
        esp.WRITE_FLASH_ATTEMPTS = 2  # upstream default; adapter must override
        esp.FLASH_SECTOR_SIZE = 4096
        esp.BOOTLOADER_FLASH_OFFSET = 0
        esp.get_secure_boot_v1_enabled.return_value = False
        esp.get_flash_encryption_enabled.return_value = False
        esp.get_encrypted_download_disabled.return_value = False
        esp.get_secure_boot_enabled.return_value = False
        esp.get_major_chip_version.return_value = 0
        esp.flash_id.return_value = 0x1840EF
        esp.flash_defl_begin.side_effect = SerialException("injected cable removal")
        adapter = object.__new__(flash.EspDevice)
        adapter.cmds, adapter.esp = cmds, esp
        with contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaises(SerialException):
                adapter.write_app(image_bytes())
        self.assertEqual(esp.WRITE_FLASH_ATTEMPTS, 1)
        esp.flash_defl_begin.assert_called_once()
        esp._port.open.assert_not_called()
        esp.connect.assert_not_called()
        esp.run_stub.assert_not_called()
        esp.erase_flash.assert_not_called()

    def test_external_env_and_cwd_config_do_not_change_defaults(self):
        evil = self.root / "external"
        evil.mkdir(exist_ok=True)
        hostile = evil / "esptool.cfg"
        hostile.write_text("[esptool]\ntimeout=999\ncustom_reset_sequence=D1\n", encoding="utf-8")
        # Fresh subprocess is necessary because configuration is read and cached
        # at import time. It only imports, then inspects constants.
        script = """
import importlib.util,json,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('portable_checked',sys.argv[1])
m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m)
adapter=m.EspDevice(Path(sys.argv[2]))
from esptool.config import load_config_file
from esptool.loader import DEFAULT_TIMEOUT
config,path=load_config_file()
assert DEFAULT_TIMEOUT != 999
assert not list(config.items('esptool'))
assert Path(path).resolve() == Path(sys.argv[2]).resolve()/'esptool.cfg'
assert adapter.esp is None
print('isolated; no port opened')
"""
        env = dict(os.environ, ESPTOOL_CFGFILE=str(hostile))
        result = subprocess.run([sys.executable, "-I", "-X", "utf8", "-B", "-c", script,
                                 str(HERE / "portable_flash.py"), str(self.root)],
                                cwd=evil, env=env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("isolated; no port opened", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
