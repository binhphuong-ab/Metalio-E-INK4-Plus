# Shim: esp_mmap_assets always imports 'qoi-conv.qoi', but this project builds with
# support_qoi=False, so the QOI encoder is never called. The registry package omits
# the qoi-conv submodule; this stub satisfies the import.
import os

def replace_extension(path, ext):
    return os.path.splitext(path)[0] + ext

class Qoi:
    def __init__(self, *a, **k):
        raise RuntimeError("QOI conversion is disabled in this build (support_qoi=False)")
