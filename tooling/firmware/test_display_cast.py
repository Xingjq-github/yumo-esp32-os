"""Execute the firmware codec and the PC receiver against synthetic USB data.

Never opens hardware, stores screenshots, or modifies firmware data partitions.
"""
from pathlib import Path
import argparse
import ctypes
import json
import random
import struct
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch
import zlib

ROOT = (Path(__file__).resolve().parents[2]/'firmware')
sys.path.insert(0, str(ROOT / '../desktop/pc-display'))
import protocol as wire
import receiver

codec = None


class CPacket(ctypes.Structure):
    _fields_ = [('kind', ctypes.c_uint8), ('format', ctypes.c_uint8), ('flags', ctypes.c_uint8),
                ('session', ctypes.c_uint32), ('sequence', ctypes.c_uint32),
                ('length', ctypes.c_uint32), ('crc', ctypes.c_uint32)]


class CodecTests(unittest.TestCase):
    def encode(self, source):
        buffer = ctypes.create_string_buffer(wire.FRAME_BYTES)
        count = codec.ymcast_encode(source, len(source) // 2, buffer, len(buffer))
        return buffer.raw[:count]

    def test_rgb565_palette_and_ppm(self):
        palette = b'\x00\xf8\xe0\x07\x1f\x00\xff\xff\x00\x00'
        pixels = (palette * ((wire.FRAME_BYTES // len(palette)) + 1))[:wire.FRAME_BYTES]
        ppm = wire.ppm(pixels)
        self.assertEqual(ppm.split(b'\n', 3)[3][:15],
                         b'\xff\x00\x00\x00\xff\x00\x00\x00\xff\xff\xff\xff\x00\x00\x00')

    def test_native_roundtrip_cross_language(self):
        rng = random.Random(23)
        for mode in range(35):
            pixels = bytearray()
            while len(pixels) < wire.FRAME_BYTES:
                run = rng.randrange(1, 257) if mode else 57600
                color = rng.randrange(65536)
                pixels += struct.pack('<H', color) * run
            raw = bytes(pixels[:wire.FRAME_BYTES])
            data = self.encode(raw)
            self.assertGreater(len(data), 0)
            decoded = wire.decode(wire.Packet(wire.FRAME, 5, 1, wire.RLE, 1, data))
            self.assertEqual(decoded, raw)
            target = ctypes.create_string_buffer(wire.FRAME_BYTES)
            self.assertEqual(codec.ymcast_decode(data, len(data), target, wire.PIXELS), 1)
            self.assertEqual(target.raw, raw)

    def test_random_noise_uses_raw(self):
        source = random.Random(99).randbytes(wire.FRAME_BYTES)
        self.assertEqual(self.encode(source), b'')
        self.assertEqual(wire.decode(wire.Packet(wire.FRAME, 5, 1, wire.RAW, 1, source)), source)

    def test_encoder_never_overflows_short_buffer(self):
        for count in (0, 1, 2, 3, 127, 128, 129, 256, 1000):
            for capacity in (0, 1, 2, 3, 17, 128, 400):
                target = ctypes.create_string_buffer(b'\x5a' * 512, 512)
                length = codec.ymcast_encode(b'\x34\x12' * count, count, target, capacity)
                self.assertLessEqual(length, capacity)
                self.assertEqual(target.raw[capacity:], b'\x5a' * (512 - capacity))

    def test_malformed_rle(self):
        for data in (b'', b'\x80', b'\x7f\x00', b'\xff\x00\x00' * 451,
                     b'\xff\x00\x00' * 450 + b'\x80\x00\x00'):
            with self.assertRaises(ValueError):
                wire.decode(wire.Packet(wire.FRAME, 5, 1, wire.RLE, 1, data))
            target = ctypes.create_string_buffer(wire.FRAME_BYTES)
            self.assertEqual(codec.ymcast_decode(data, len(data), target, wire.PIXELS), 0)

    def test_partial_render_stride_and_edges(self):
        screen = ctypes.create_string_buffer(wire.FRAME_BYTES)
        source = b'\x00\xf8' * 3 + b'pad123' + b'\xe0\x07' * 3
        self.assertEqual(codec.ymcast_copy_rect(screen, 237, 238, 239, 239, source, 12, len(source)), 1)
        self.assertEqual(screen.raw[(238 * 240 + 237) * 2:239 * 240 * 2], b'\x00\xf8' * 3)
        self.assertEqual(screen.raw[-6:], b'\xe0\x07' * 3)
        before = screen.raw
        for args in ((-1, 0, 2, 1, 12, 18), (0, 0, 240, 1, 12, 18),
                     (0, 0, 2, 1, 5, 18), (0, 0, 2, 1, 12, 17)):
            self.assertEqual(codec.ymcast_copy_rect(screen, *args[:4], source, *args[4:]), 0)
            self.assertEqual(screen.raw, before)

    def test_headers_crc_fragmentation_and_noise(self):
        raw = b'\x00\xf8' * wire.PIXELS
        encoded = self.encode(raw)
        packet = wire.Packet(wire.FRAME, 0x12345678, 999, wire.RLE, 1, encoded)
        head = ctypes.create_string_buffer(wire.HEADER.size)
        native = CPacket(packet.kind, packet.format, packet.flags, packet.session,
                         packet.sequence, len(encoded), zlib.crc32(encoded))
        codec.ymcast_header(head, ctypes.byref(native))
        data = wire.pack(packet)
        self.assertEqual(head.raw, data[:wire.HEADER.size])
        self.assertEqual(codec.ymcast_crc32(b'123456789', 9), 0xcbf43926)
        for chunk in (1, 7, 35, 36, 63, 64, 255, 8192):
            parser = wire.Parser()
            results = []
            stream = b'ESP-ROM log\r\n' + data + wire.pack(wire.Packet(wire.STATUS, packet.session))
            for pos in range(0, len(stream), chunk):
                results += parser.feed(stream[pos:pos + chunk])
            self.assertEqual(results[0], packet)
            self.assertEqual(len(results), 2)
        for index in range(36):
            damaged = bytearray(data[:36]); damaged[index] ^= 1
            self.assertEqual(codec.ymcast_parse(bytes(damaged), ctypes.byref(native)), 0)
        damaged = bytearray(data);damaged[-1] ^= 1
        parser = wire.Parser()
        self.assertEqual(parser.feed(bytes(damaged) + data), [packet])
        self.assertGreater(parser.rejected, 0)
        for _ in range(100):
            parser.feed(b'z' * 8192)
            self.assertLessEqual(len(parser.buffer), 7)


class FakeSerial:
    def __init__(self, *_, **kwargs):
        self.port = None
        self.dtr = self.rts = True
        self.is_open = False
        self.incoming = bytearray()
        self.controls = []
        self.guard = threading.Lock()
        self.ready = threading.Event()
        self.closed = threading.Event()
        self.init_args = kwargs
        self.silent = False

    def open(self):
        assert self.dtr is False and self.rts is False
        self.is_open = True

    @property
    def in_waiting(self):
        with self.guard:
            return len(self.incoming)

    def reset_input_buffer(self):
        with self.guard:
            self.incoming.clear()

    def append(self, packet):
        with self.guard:
            self.incoming += wire.pack(packet)

    def write(self, data):
        p = wire.Parser().feed(data)[0]
        self.controls.append(p)
        if p.kind == wire.HELLO:
            self.append(wire.Packet(wire.STATUS, 123))
        if p.kind == wire.READY:
            payload = b'\xff\x00\xf8' * 450
            self.append(wire.Packet(wire.FRAME, 123, 1, wire.RLE, 1, payload))
            self.ready.set()
        if p.kind == wire.ACK and not self.silent:
            self.append(wire.Packet(wire.STATUS, 123))
        return len(data)

    def read(self, length):
        with self.guard:
            data = bytes(self.incoming[:length])
            del self.incoming[:length]
        if not data:
            time.sleep(0.005)
        return data

    def close(self):
        self.is_open = False
        self.closed.set()


class TransportTests(unittest.TestCase):
    def test_ack_after_paint_and_disconnect(self):
        usb = FakeSerial()
        with patch.object(receiver.serial, 'Serial', return_value=usb):
            c = receiver.Connection('SYNTHETIC')
            c.thread.start()
            self.assertTrue(usb.ready.wait(2))
            sid, seq, image, _ = c.frames.get(timeout=2)
            self.assertTrue(image.startswith(b'P6'))
            self.assertFalse(any(p.kind == wire.ACK and p.sequence == 1 for p in usb.controls))
            c.acknowledge(sid, seq)
            deadline = time.monotonic() + 2
            while not any(p.kind == wire.ACK and p.sequence == 1 for p in usb.controls):
                self.assertLess(time.monotonic(), deadline)
                time.sleep(0.005)
            c.stop.set();c.thread.join(2)
            self.assertFalse(c.thread.is_alive())
            self.assertTrue(usb.closed.is_set())
            self.assertEqual(usb.controls[-1].kind, wire.STOP)

    def test_device_end_clears_session(self):
        usb = FakeSerial()
        with patch.object(receiver.serial, 'Serial', return_value=usb):
            c = receiver.Connection('SYNTHETIC');c.thread.start()
            self.assertTrue(usb.ready.wait(2))
            usb.append(wire.Packet(wire.END, 123, flags=0))
            c.thread.join(2)
            self.assertFalse(c.thread.is_alive())
            self.assertTrue(usb.closed.is_set())

    def test_stalled_usb_releases_port(self):
        usb = FakeSerial();usb.silent = True
        with patch.object(receiver.serial, 'Serial', return_value=usb):
            c = receiver.Connection('SYNTHETIC');c.thread.start()
            self.assertTrue(usb.ready.wait(2))
            c.thread.join(4)
            self.assertFalse(c.thread.is_alive())
            self.assertTrue(usb.closed.is_set())


def main():
    global codec
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--run-dir', type=Path, required=True)
    args = parser.parse_args()
    run = args.run_dir.resolve();run.mkdir(parents=True, exist_ok=False)
    dll = run / 'display-codec.dll'
    exports = run / 'exports.c'
    exports.write_text('''#include "display_cast_codec.h"
__declspec(dllexport) uint32_t ymcast_crc32(const uint8_t *,size_t);
__declspec(dllexport) void ymcast_header(uint8_t *,const ymcast_packet *);
__declspec(dllexport) int ymcast_parse(const uint8_t *,ymcast_packet *);
__declspec(dllexport) size_t ymcast_encode(const uint8_t *,size_t,uint8_t *,size_t);
__declspec(dllexport) int ymcast_decode(const uint8_t *,size_t,uint8_t *,size_t);
__declspec(dllexport) int ymcast_copy_rect(uint8_t *,int,int,int,int,const uint8_t *,size_t,size_t);
#include "display_cast_codec.c"
''', encoding='utf-8')
    command = [str(args.compiler), '-shared', '-Imain/core', '-o', str(dll), str(exports)]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=60)
    (run / 'compile.stdout.log').write_bytes(result.stdout)
    (run / 'compile.stderr.log').write_bytes(result.stderr)
    result.check_returncode()
    codec = ctypes.CDLL(str(dll))
    codec.ymcast_crc32.restype = ctypes.c_uint32
    codec.ymcast_crc32.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    codec.ymcast_encode.restype = ctypes.c_size_t
    codec.ymcast_encode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    codec.ymcast_decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    codec.ymcast_copy_rect.argtypes = [ctypes.c_void_p] + [ctypes.c_int] * 4 + [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t]
    codec.ymcast_header.argtypes = [ctypes.c_void_p, ctypes.POINTER(CPacket)]
    codec.ymcast_parse.argtypes = [ctypes.c_void_p, ctypes.POINTER(CPacket)]
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__]))
    report = dict(passed=result.wasSuccessful(), tests=result.testsRun,
                  serial_hardware_opened=False, method='production C codec + simulated serial PC receiver')
    (run / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    raise SystemExit(main())
