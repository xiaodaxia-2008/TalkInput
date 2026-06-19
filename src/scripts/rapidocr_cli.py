# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "rapidocr",
#     "onnxruntime",
# ]
# ///
"""RapidOCR CLI — reads an image path from argv and prints recognized text."""

import sys

from rapidocr import RapidOCR


def main() -> None:
    if len(sys.argv) < 2:
        sys.exit(1)

    img_path = sys.argv[1]
    engine = RapidOCR()
    result = engine(img_path)
    if result and result.txts:
        for text in result.txts:
            print(str(text))


if __name__ == "__main__":
    main()
