"""Open a stock texture export (.dds B8G8R8A8 raw, or any image Pillow reads) as an RGBA image."""
import struct

from PIL import Image


def load(path):
    data = path.read_bytes()
    if data[84:88] == b"DX10" and struct.unpack_from("<I", data, 128)[0] in (87, 91):   # B8G8R8A8 (raw)
        height, width = struct.unpack_from("<II", data, 12)
        raw = data[148:148 + width * height * 4]
        return Image.frombuffer("RGBA", (width, height), raw, "raw", "BGRA", 0, 1).copy()
    return Image.open(path).convert("RGBA")
