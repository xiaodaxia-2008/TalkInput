#!/usr/bin/env python3
"""
OpenVINO vs ONNX Runtime benchmark for PP-OCRv6 small.

Uses the det/rec ONNX already downloaded to:
  C:/Users/xiaoz/AppData/Local/Temp/opencode/ppocrv6_small/{det,rec}.onnx
or falls back to third_parties/rapid-ocr/models (PP-OCRv3) if v6 not present.

Measures pure model inference (no pre/post) on 640x640 dummy and on the
paseo screenshot resized to 640x640, like arXiv Fig.6 (square inputs).
"""

import time
import pathlib
import numpy as np

try:
    import openvino as ov
except ImportError:
    ov = None

try:
    import onnxruntime as ort
except ImportError:
    ort = None

try:
    import cv2
except ImportError:
    cv2 = None

DET = pathlib.Path("C:/Users/xiaoz/AppData/Local/Temp/opencode/ppocrv6_small/det.onnx")
REC = pathlib.Path("C:/Users/xiaoz/AppData/Local/Temp/opencode/ppocrv6_small/rec.onnx")
IMG = pathlib.Path("C:/AppSource/TalkInput/tests/test_data/paseo_sreenshot.png")

# fallback to PP-OCRv3 if v6 not present
if not DET.exists():
    DET = pathlib.Path("C:/AppSource/TalkInput/third_parties/rapid-ocr/models/ch_PP-OCRv3_det_infer.onnx")
if not REC.exists():
    REC = pathlib.Path("C:/AppSource/TalkInput/third_parties/rapid-ocr/models/ch_PP-OCRv3_rec_infer.onnx")

def bench_ov(model_path, input_shape, iters=30, warmup=5):
    if ov is None:
        return None, "openvino not installed"
    core = ov.Core()
    model = core.read_model(str(model_path))
    compiled = core.compile_model(model, "CPU")
    req = compiled.create_infer_request()
    dummy = np.random.rand(*input_shape).astype(np.float32)
    for _ in range(warmup):
        req.infer({0: dummy})
    t0 = time.perf_counter()
    for _ in range(iters):
        req.infer({0: dummy})
    t1 = time.perf_counter()
    return (t1 - t0) / iters * 1000, None

def bench_ort(model_path, input_shape, iters=30, warmup=5):
    if ort is None:
        return None, "onnxruntime not installed"
    sess = ort.InferenceSession(str(model_path), providers=["CPUExecutionProvider"])
    inp_name = sess.get_inputs()[0].name
    dummy = np.random.rand(*input_shape).astype(np.float32)
    for _ in range(warmup):
        sess.run(None, {inp_name: dummy})
    t0 = time.perf_counter()
    for _ in range(iters):
        sess.run(None, {inp_name: dummy})
    t1 = time.perf_counter()
    return (t1 - t0) / iters * 1000, None

def load_image_resized(shape):
    if cv2 is None or not IMG.exists():
        return np.random.rand(*shape).astype(np.float32)
    img = cv2.imread(str(IMG))
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    h, w = shape[2], shape[3]
    img = cv2.resize(img, (w, h))
    img = img.astype(np.float32) / 255.0
    img = np.transpose(img, (2, 0, 1))[None, ...]  # NCHW
    if shape[1] == 3:
        return img
    # for rec: 3x48x320
    return img

def main():
    print(f"DET: {DET} ({DET.stat().st_size/1024/1024:.1f} MB)" if DET.exists() else f"DET missing: {DET}")
    print(f"REC: {REC} ({REC.stat().st_size/1024/1024:.1f} MB)" if REC.exists() else f"REC missing: {REC}")
    print(f"IMG: {IMG} ({IMG.stat().st_size/1024:.0f} KB)" if IMG.exists() else f"IMG missing: {IMG}")
    print(f"openvino: {ov.__version__ if ov else 'not installed'}")
    print(f"onnxruntime: {ort.__version__ if ort else 'not installed'}")
    print()

    # Shapes: det is typically 1x3x640x640 or 1x3x736x736; rec is 1x3x48x320
    for name, path, shape in [
        ("det 640", DET, (1, 3, 640, 640)),
        ("det 1024", DET, (1, 3, 1024, 1024)),
        ("rec 48x320", REC, (1, 3, 48, 320)),
    ]:
        if not path.exists():
            continue
        print(f"--- {name} {path.name} {shape} ---")
        ov_ms, ov_err = bench_ov(path, shape)
        ort_ms, ort_err = bench_ort(path, shape)
        if ov_err:
            print(f"  OpenVINO: {ov_err}")
        else:
            print(f"  OpenVINO: {ov_ms:.1f} ms/iter")
        if ort_err:
            print(f"  ONNX RT : {ort_err}")
        else:
            print(f"  ONNX RT : {ort_ms:.1f} ms/iter")
        if ov_ms and ort_ms:
            print(f"  speedup: {ort_ms/ov_ms:.2f}x (OpenVINO faster)" if ov_ms < ort_ms else f"  speedup: {ov_ms/ort_ms:.2f}x (ONNX faster)")

if __name__ == "__main__":
    main()
