"""Narrow normalization for one IDF-generated, disabled codec Kconfig comment.

The 2026-09-20 reconfigure adds this 63-byte CRLF block. Removing exactly
that block reproduces the preconfigure SHA256; no option value, unrelated
comment, whitespace, line ending or other configuration change is ignored.
"""
SECTION = b"# Audio Codec Device Configuration\r\n#\r\n"
DISABLED = b"# default:\r\n# CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE is not set\r\n"
KEY = b"CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE"

def historical_sdkconfig_bytes(data: bytes) -> bytes:
    anchor = SECTION + DISABLED
    if data.count(KEY) == 1 and data.count(anchor) == 1:
        return data.replace(anchor, SECTION, 1)
    return data
