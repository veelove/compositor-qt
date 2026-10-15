#!/usr/bin/env python3
"""
vision_tool.py - Compositor-Qt 视觉智能 Python 桥

协议:
  - 命令行参数: x y  (点击坐标,原图像素空间)
  - stdin: 原图像 PNG 字节流
  - stdout: mask PNG 字节流 (Grayscale8, 与原图同尺寸)
  - 退出码: 0=成功,非 0=失败(同时把错误写到 stderr)

真实集成优先尝试 ONNX Runtime + U^2-Net(u2netp 轻量) 做显著性检测,
然后把显著性按点击点连通域 + 阈值化为前景 mask。
若未安装 onnxruntime/onnx/模型权重,降级到简单 OpenCV GrabCut 或纯色阈值,
确保 Qt 端总能拿到 mask。
"""
from __future__ import annotations

import sys
import io
import os
import struct
import traceback


def log(msg: str) -> None:
    sys.stderr.write(str(msg) + "\n")
    sys.stderr.flush()


def read_png_from_stdin() -> bytes:
    data = sys.stdin.buffer.read()
    if not data:
        raise RuntimeError("stdin 为空:未收到 PNG 字节流")
    return data


def decode_png(png_bytes: bytes):
    """尽量用 PIL; 退回 OpenCV; 退化 到纯 Python PNG 解码器。"""
    try:
        from PIL import Image  # type: ignore
        img = Image.open(io.BytesIO(png_bytes)).convert("RGB")
        return img, "pil"
    except Exception:
        pass
    try:
        import numpy as np  # type: ignore
        import cv2  # type: ignore
        arr = np.frombuffer(png_bytes, dtype=np.uint8)
        bgr = cv2.imdecode(arr, cv2.IMREAD_COLOR)
        if bgr is None:
            raise RuntimeError("cv2.imdecode 失败")
        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        # 包装为极简 Image-like: 提供 size/crop/load/convert/tobytes 等
        return _NumpyImage(rgb), "cv2"
    except Exception:
        pass
    # 终极退化:zlib + 自实现 PNG 解码 (仅支持 8bit RGB/RGBA)
    return _decode_png_minimal(png_bytes), "minimal"


class _NumpyImage:
    """最小 Image 协议,适配 cv2 numpy 数组,够本脚本内部使用。"""

    def __init__(self, arr):
        import numpy as np  # type: ignore
        self._arr = arr
        self._np = np
        self._w = arr.shape[1]
        self._h = arr.shape[0]

    @property
    def size(self):
        return (self._w, self._h)

    def convert(self, mode: str):
        if mode == "RGB":
            return self
        if mode == "L":
            arr = self._arr
            np = self._np
            # Rec.601 luma
            gray = (0.299 * arr[..., 0] + 0.587 * arr[..., 1] + 0.114 * arr[..., 2]).astype(np.uint8)
            return _NumpyImage(gray[..., None].repeat(3, axis=2))
        raise ValueError("unsupported convert mode: " + mode)

    def crop(self, box):
        import numpy as np  # type: ignore
        l, t, r, b = box
        return _NumpyImage(self._arr[t:b, l:r].copy())

    def save(self, buf, format: str = "PNG"):
        import numpy as np  # type: ignore
        if format.upper() != "PNG":
            raise ValueError("only PNG supported")
        arr = self._arr
        h, w = arr.shape[:2]
        if arr.ndim == 3:
            ch = arr.shape[2]
            color = 2 if ch == 3 else 6  # RGB=2, RGBA=6
            raw = arr.astype(np.uint8).tobytes()
        else:
            color = 0  # grayscale
            raw = arr.astype(np.uint8).tobytes()
        _write_png(buf, w, h, color, raw)


def _png_chunk(tag: bytes, data: bytes) -> bytes:
    import zlib
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def _write_png(buf, w: int, h: int, color: int, raw: bytes) -> None:
    import zlib
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, color, 0, 0, 0)
    # 加 filter byte 0 每行
    stride = len(raw) // h
    filtered = b"".join(b"\x00" + raw[i * stride:(i + 1) * stride] for i in range(h))
    idat = zlib.compress(filtered, 9)
    buf.write(sig)
    buf.write(_png_chunk(b"IHDR", ihdr))
    buf.write(_png_chunk(b"IDAT", idat))
    buf.write(_png_chunk(b"IEND", b""))


def _decode_png_minimal(png_bytes: bytes):
    """只支持 8bit RGB/RGBA/灰度 PNG,过滤类型 0/None。"""
    import zlib
    assert png_bytes[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    i = 8
    width = height = depth = color = 0
    idat = bytearray()
    while i < len(png_bytes):
        length = struct.unpack(">I", png_bytes[i:i + 4])[0]
        tag = png_bytes[i + 4:i + 8]
        data = png_bytes[i + 8:i + 8 + length]
        if tag == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", data[:10])
        elif tag == b"IDAT":
            idat += data
        elif tag == b"IEND":
            break
        i += 8 + length + 4
    if depth != 8:
        raise RuntimeError("minimal decoder: only 8-bit supported")
    raw = zlib.decompress(bytes(idat))
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    stride = width * bpp
    # 简化:假设全部是 filter type 0
    rows = [raw[r * (stride + 1) + 1:(r + 1) * (stride + 1)] for r in range(height)]
    arr_bytes = b"".join(rows)

    class _MinimalImage:
        def __init__(self, w, h, bpp, data):
            self._w, self._h, self._bpp, self._data = w, h, bpp, data

        @property
        def size(self):
            return (self._w, self._h)

        def convert(self, mode):
            if self._bpp == 3 and mode == "RGB":
                return self
            if self._bpp == 4 and mode == "RGB":
                # drop alpha
                return _MinimalImage(self._w, self._h, 3,
                                    b"".join(self._data[i:i + 3] for i in range(0, len(self._data), 4)))
            if mode == "L":
                if self._bpp == 1:
                    return self
                if self._bpp == 3:
                    out = bytearray(self._w * self._h)
                    for i in range(self._w * self._h):
                        r, g, b = self._data[i * 3], self._data[i * 3 + 1], self._data[i * 3 + 2]
                        out[i] = (0.299 * r + 0.587 * g + 0.114 * b)
                    return _MinimalImage(self._w, self._h, 1, bytes(out))
                if self._bpp == 4:
                    out = bytearray(self._w * self._h)
                    for i in range(self._w * self._h):
                        r, g, b = self._data[i * 4], self._data[i * 4 + 1], self._data[i * 4 + 2]
                        out[i] = (0.299 * r + 0.587 * g + 0.114 * b)
                    return _MinimalImage(self._w, self._h, 1, bytes(out))
            raise ValueError("unsupported convert: " + mode)

        def crop(self, box):
            l, t, r, b = box
            cw, ch = r - l, b - t
            out = bytearray(cw * ch * self._bpp)
            src = self._data
            src_stride = self._w * self._bpp
            for row in range(ch):
                src_off = (t + row) * src_stride + l * self._bpp
                dst_off = row * cw * self._bpp
                out[dst_off:dst_off + cw * self._bpp] = src[src_off:src_off + cw * self._bpp]
            return _MinimalImage(cw, ch, self._bpp, bytes(out))

        def save(self, buf, format: str = "PNG"):
            if format.upper() != "PNG":
                raise ValueError("only PNG")
            color_map = {1: 0, 3: 2, 4: 6}
            _write_png(buf, self._w, self._h, color_map[self._bpp], self._data)

    return _MinimalImage(width, height, bpp, arr_bytes)


def _try_u2net(rgb_image):
    """尝试用 ONNX Runtime + U^2-Net 做显著性检测。返回 0..1 显著性图 numpy(HxW)。"""
    try:
        import numpy as np  # type: ignore
        import onnxruntime as ort  # type: ignore
    except Exception:
        return None

    model_path = os.environ.get(
        "COMPOSITOR_U2NET_MODEL",
        os.path.join(os.path.dirname(__file__), "u2netp.onnx"),
    )
    if not os.path.isfile(model_path):
        log(f"[u2net] 模型权重不存在: {model_path}")
        return None

    try:
        w, h = rgb_image.size
        # u2net 期望 320x240 输入(轻量 u2netp);按长边等比缩放再 pad
        inp_w, inp_h = 320, 240
        scale = min(inp_w / w, inp_h / h)
        nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
        if hasattr(rgb_image, "resize"):
            resized = rgb_image.resize((nw, nh))
        else:
            resized = rgb_image
        arr = np.asarray(resized, dtype=np.float32) / 255.0
        # NHWC -> NCHW
        if arr.ndim == 2:
            arr = arr[..., None]
        arr = np.transpose(arr, (2, 0, 1))[None, ...]
        # mean/std normalize (ImageNet 反标准化;U^2-Net 训练用的)
        mean = np.array([0.485, 0.456, 0.406], dtype=np.float32).reshape(1, 3, 1, 1)
        std = np.array([0.229, 0.224, 0.225], dtype=np.float32).reshape(1, 3, 1, 1)
        arr = (arr - mean) / std

        sess = ort.InferenceSession(model_path, providers=["CPUExecutionProvider"])
        input_name = sess.get_inputs()[0].name
        out = sess.run(None, {input_name: arr.astype(np.float32)})[0]
        # out: (1,1,nh,nw) 0..1
        sal = out[0, 0]
        # 还原到原图尺寸
        try:
            import cv2  # type: ignore
            sal = cv2.resize(sal, (w, h), interpolation=cv2.INTER_LINEAR)
        except Exception:
            # numpy 双线性插值(简化)
            sal_y = np.linspace(0, nh - 1, h).astype(np.int32)
            sal_x = np.linspace(0, nw - 1, w).astype(np.int32)
            sal = sal[sal_y[:, None], sal_x[None, :]]
        sal = np.clip(sal, 0.0, 1.0).astype(np.float32)
        return sal
    except Exception as e:
        log("[u2net] 推理失败: " + repr(e))
        return None


def _try_grabcut(rgb_image):
    """用 OpenCV GrabCut 做粗分割,返回 0/1 mask。"""
    try:
        import numpy as np  # type: ignore
        import cv2  # type: ignore
    except Exception:
        return None
    try:
        arr = np.asarray(rgb_image, dtype=np.uint8)
        if arr.ndim == 2:
            arr = cv2.cvtColor(arr, cv2.COLOR_GRAY2BGR)
        else:
            arr = cv2.cvtColor(arr, cv2.COLOR_RGB2BGR)
        h, w = arr.shape[:2]
        # 围绕中心 60% 区域作为 init 前景
        margin_x, margin_y = int(w * 0.1), int(h * 0.1)
        rect = (margin_x, margin_y, w - 2 * margin_x, h - 2 * margin_y)
        mask = np.zeros((h, w), np.uint8)
        bgd = np.zeros((1, 65), np.float64)
        fgd = np.zeros((1, 65), np.float64)
        cv2.grabCut(arr, mask, rect, bgd, fgd, 3, cv2.GC_INIT_WITH_RECT)
        m = np.where((mask == cv2.GC_FGD) | (mask == cv2.GC_PR_FGD), 1, 0).astype(np.uint8)
        return m
    except Exception as e:
        log("[grabcut] 失败: " + repr(e))
        return None


def _color_threshold_fallback(rgb_image, x: int, y: int):
    """没模型/没 OpenCV 时,用点击点颜色做洪水漫灌连通域。"""
    w, h = rgb_image.size
    x = max(0, min(w - 1, x))
    y = max(0, min(h - 1, y))
    rgb = rgb_image.convert("RGB")
    crop_w, crop_h = min(64, w), min(64, h)
    l, t = max(0, x - crop_w // 2), max(0, y - crop_h // 2)
    r, btm = min(w, l + crop_w), min(h, t + crop_h)
    patch = rgb.crop((l, t, r, btm))
    try:
        from PIL import Image  # type: ignore
        pa = list(patch.getdata())
        size = patch.size
    except Exception:
        # 自实现 getdata
        pa = []
        size = patch.size
        # 用 crop 的 _data 顺序遍历 (RGB 3 byte/pixel)
        if hasattr(patch, "_data"):
            d = patch._data
            bpp = patch._bpp
            for i in range(size[0] * size[1]):
                if bpp >= 3:
                    pa.append((d[i * bpp], d[i * bpp + 1], d[i * bpp + 2]))
        patch_w, patch_h = size

    cx = x - l
    cy = y - t
    if not pa:
        return _empty_mask(w, h)
    seed = pa[cy * size[0] + cx]
    sr, sg, sb = seed
    tol = 32
    mask = bytearray(size[0] * size[1])
    stack = [(cx, cy)]
    while stack:
        ix, iy = stack.pop()
        idx = iy * size[0] + ix
        if mask[idx]:
            continue
        pr, pg, pb = pa[idx]
        if abs(pr - sr) > tol or abs(pg - sg) > tol or abs(pb - sb) > tol:
            continue
        mask[idx] = 1
        if ix > 0:
            stack.append((ix - 1, iy))
        if ix < size[0] - 1:
            stack.append((ix + 1, iy))
        if iy > 0:
            stack.append((ix, iy - 1))
        if iy < size[1] - 1:
            stack.append((ix, iy + 1))

    full = bytearray(w * h)
    for py_ in range(size[1]):
        for px_ in range(size[0]):
            if mask[py_ * size[0] + px_]:
                full[(t + py_) * w + (l + px_)] = 1
    return bytes(full)


def _empty_mask(w: int, h: int) -> bytes:
    return bytes(w * h)


def _mask_to_png_bytes(mask_bytes: bytes, w: int, h: int) -> bytes:
    buf = io.BytesIO()
    _write_png(buf, w, h, 0, mask_bytes)  # color=0 grayscale 8bit
    return buf.getvalue()


def main(argv):
    if len(argv) < 3:
        log("usage: vision_tool.py x y  (PNG on stdin)")
        return 2
    try:
        x = int(argv[1])
        y = int(argv[2])
    except Exception:
        log("x y 必须为整数")
        return 2

    try:
        png_bytes = read_png_from_stdin()
        rgb_image, backend = decode_png(png_bytes)
        log(f"[vision] 解码后端={backend}, size={rgb_image.size}")
    except Exception as e:
        log("读取/解码 PNG 失败: " + repr(e))
        traceback.print_exc(file=sys.stderr)
        return 3

    w, h = rgb_image.size

    # 路径 1:U^2-Net 显著性 + 阈值
    sal = _try_u2net(rgb_image)
    if sal is not None:
        try:
            import numpy as np  # type: ignore
            # 以点击点显著性为中心,Otsu 风格阈值
            center = sal[max(0, min(h - 1, y)), max(0, min(w - 1, x))]
            thr = max(0.2, float(center) * 0.6)
            mask = (sal >= thr).astype(np.uint8).tobytes()
            sys.stdout.buffer.write(_mask_to_png_bytes(mask, w, h))
            sys.stdout.buffer.flush()
            return 0
        except Exception as e:
            log("[u2net] 后处理失败: " + repr(e))

    # 路径 2:OpenCV GrabCut
    m = _try_grabcut(rgb_image)
    if m is not None:
        sys.stdout.buffer.write(_mask_to_png_bytes(bytes(m), w, h))
        sys.stdout.buffer.flush()
        return 0

    # 路径 3:纯色阈值洪水漫灌
    try:
        mask = _color_threshold_fallback(rgb_image, x, y)
        sys.stdout.buffer.write(_mask_to_png_bytes(mask, w, h))
        sys.stdout.buffer.flush()
        return 0
    except Exception as e:
        log("color threshold 失败: " + repr(e))
        traceback.print_exc(file=sys.stderr)
        return 4


if __name__ == "__main__":
    sys.exit(main(sys.argv))