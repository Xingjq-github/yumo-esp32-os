"""Yumo USB display wire format. Lossless RGB565, bounded complete frames."""
from array import array
from dataclasses import dataclass
import struct
import sys
import zlib

WIDTH = HEIGHT = 240
PIXELS = WIDTH * HEIGHT
FRAME_BYTES = PIXELS * 2
MAGIC = b'YUMOCAST'
HEADER = struct.Struct('<8sBBBBIIHHIII')
FRAME, STATUS, END = 1, 2, 3
HELLO, READY, ACK, STOP = 16, 17, 18, 19
RAW, RLE = 0, 1


@dataclass(frozen=True)
class Packet:
    kind: int
    session: int = 0
    sequence: int = 0
    format: int = RAW
    flags: int = 1
    payload: bytes = b''


def pack(packet):
    width, height = (WIDTH, HEIGHT) if packet.kind == FRAME else (0, 0)
    head = HEADER.pack(MAGIC, 1, packet.kind, packet.format, packet.flags,
                       packet.session, packet.sequence, width, height,
                       len(packet.payload), zlib.crc32(packet.payload), 0)
    return head[:-4] + struct.pack('<I', zlib.crc32(head[:-4])) + packet.payload


class Parser:
    def __init__(self):
        self.buffer = bytearray()
        self.rejected = 0

    def feed(self, data):
        # Serial reads are bounded. Discard pathological callers, never grow forever.
        if len(data) > FRAME_BYTES * 2:
            data = data[-FRAME_BYTES * 2:]
        self.buffer.extend(data)
        result = []
        while True:
            start = self.buffer.find(MAGIC)
            if start < 0:
                del self.buffer[:-7]
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < HEADER.size:
                break
            fields = HEADER.unpack_from(self.buffer)
            _, version, kind, fmt, flags, sid, seq, w, h, length, crc, header_crc = fields
            valid = version == 1 and flags <= 1 and header_crc == zlib.crc32(self.buffer[:32])
            if kind == FRAME:
                valid &= (sid != 0 and seq != 0 and flags == 1 and (w, h) == (WIDTH, HEIGHT)
                          and fmt in (RAW, RLE) and 0 < length <= FRAME_BYTES
                          and (fmt != RAW or length == FRAME_BYTES))
            else:
                valid &= (kind in (STATUS, END, HELLO, READY, ACK, STOP)
                          and fmt == 0 and w == h == length == crc == 0)
            if not valid:
                self.rejected += 1
                del self.buffer[0]
                continue
            end = HEADER.size + length
            if len(self.buffer) < end:
                break
            payload = bytes(self.buffer[HEADER.size:end])
            if zlib.crc32(payload) != crc:
                self.rejected += 1
                del self.buffer[0]
                continue
            result.append(Packet(kind, sid, seq, fmt, flags, payload))
            del self.buffer[:end]
        return result


def decode(packet):
    if packet.kind != FRAME:
        raise ValueError('不是画面数据')
    if packet.format == RAW:
        if len(packet.payload) != FRAME_BYTES:
            raise ValueError('画面长度无效')
        return packet.payload
    if packet.format != RLE:
        raise ValueError('不支持的画面格式')
    result = bytearray()
    data, position = packet.payload, 0
    while position < len(data):
        tag = data[position]
        position += 1
        count = (tag & 127) + 1
        size = 2 if tag & 128 else count * 2
        if position + size > len(data) or len(result) + count * 2 > FRAME_BYTES:
            raise ValueError('压缩画面损坏')
        part = data[position:position + size]
        result.extend(part * count if tag & 128 else part)
        position += size
    if len(result) != FRAME_BYTES:
        raise ValueError('画面不完整')
    return bytes(result)


_rgb = None


def ppm(pixels):
    """Scale channel bits without quantization banding or RGB/BGR reversal."""
    global _rgb
    if len(pixels) != FRAME_BYTES:
        raise ValueError('RGB565长度无效')
    if _rgb is None:
        _rgb = [bytes((((v >> 11) << 3) | ((v >> 11) >> 2),
                       (((v >> 5) & 63) << 2) | (((v >> 5) & 63) >> 4),
                       ((v & 31) << 3) | ((v & 31) >> 2))) for v in range(65536)]
    values = array('H', pixels)
    if sys.byteorder != 'little':
        values.byteswap()
    return b'P6\n240 240\n255\n' + b''.join(_rgb[v] for v in values)
