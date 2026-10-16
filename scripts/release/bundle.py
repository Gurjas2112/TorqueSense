"""Stage a self-contained Windows release: every executable plus every DLL it needs, found by
reading PE import tables — not by trusting whatever happens to be on PATH on the build machine.

    python scripts/release/bundle.py --out dist/bin \
        --exe build/windows-release/control-app/torquesense.exe build/windows-release/softplc/softplc.exe \
        --also C:/dev/tools/onnxruntime/lib/onnxruntime_providers_shared.dll \
               C:/dev/tools/onnxruntime/lib/onnxruntime_providers_cuda.dll \
               C:/dev/tools/onnxruntime/lib/onnxruntime_providers_tensorrt.dll \
        --search C:/msys64/ucrt64/bin C:/dev/tools/onnxruntime/lib \
        --system-dir C:/Windows/System32

Each DLL is classified:
  bundled       copied next to the executables (MinGW runtime, ONNX Runtime, open62541, …)
  gpu-runtime   NVIDIA's CUDA, cuDNN and TensorRT DLLs: installed on the station, never shipped;
                the supervisor puts their folders on the engine's PATH
  msvc-runtime  the Visual C++ runtime ONNX Runtime was built with: the VC++ redistributable
  system        part of Windows
Anything that resolves to none of these fails the bundle: a release that would stop at start-up
with "DLL not found" is not built at all. bundle.json records every file with its SHA-256.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path

GPU_RUNTIME = re.compile(r"^(nvcuda|cudart64_\d+|cublas(lt)?64_\d+|cudnn\w*64_\d+|cufft64_\d+|curand64_\d+|"
                         r"nvrtc\w*|nvjitlink\w*|nvinfer\w*|nvonnxparser\w*|nvml)\.dll$", re.I)
MSVC_RUNTIME = re.compile(r"^(msvcp140\w*|vcruntime140\w*|concrt140)\.dll$", re.I)
API_SET = re.compile(r"^(api|ext)-ms-win-", re.I)
# Used only when --system-dir is not given (tests, Linux): core DLLs every Windows install has.
WINDOWS_CORE = {n.lower() for n in """kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll
oleaut32.dll ws2_32.dll msvcrt.dll ntdll.dll bcrypt.dll crypt32.dll secur32.dll iphlpapi.dll winmm.dll
dbghelp.dll shlwapi.dll version.dll setupapi.dll dxgi.dll d3d11.dll d3d12.dll mf.dll mfplat.dll
mfreadwrite.dll mfuuid.dll comdlg32.dll comctl32.dll userenv.dll powrprof.dll psapi.dll rpcrt4.dll
wldap32.dll normaliz.dll dwmapi.dll uxtheme.dll imm32.dll winhttp.dll wininet.dll ucrtbase.dll
cfgmgr32.dll ncrypt.dll avrt.dll ksuser.dll""".split()}


def imports(path: Path) -> list[str]:
    """Names of the DLLs a PE file imports, including delay-loaded ones."""
    b = path.read_bytes()
    if b[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE file")
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    if b[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path}: bad PE signature")
    n_sections, = struct.unpack_from("<H", b, pe + 6)
    opt_size, = struct.unpack_from("<H", b, pe + 20)
    opt = pe + 24
    magic, = struct.unpack_from("<H", b, opt)
    dirs = opt + (112 if magic == 0x20B else 96)
    sections = []
    for i in range(n_sections):
        s = opt + opt_size + 40 * i
        vsize, vaddr, rsize, raddr = struct.unpack_from("<IIII", b, s + 8)
        sections.append((vaddr, max(vsize, rsize), raddr))

    def off(rva: int) -> int:
        for vaddr, size, raddr in sections:
            if vaddr <= rva < vaddr + size:
                return rva - vaddr + raddr
        raise ValueError(f"{path}: RVA {rva:#x} outside every section")

    def cstr(o: int) -> str:
        return b[o:b.index(b"\0", o)].decode("ascii")

    names: list[str] = []
    imp_rva, _ = struct.unpack_from("<II", b, dirs + 8 * 1)
    if imp_rva:
        o = off(imp_rva)
        while True:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", b, o)
            if not (oft or name_rva or ft):
                break
            names.append(cstr(off(name_rva)))
            o += 20
    dly_rva, _ = struct.unpack_from("<II", b, dirs + 8 * 13)
    if dly_rva:
        o = off(dly_rva)
        while True:
            attrs, name_rva = struct.unpack_from("<II", b, o)
            if not name_rva:
                break
            names.append(cstr(off(name_rva)))
            o += 32
    return names


def classify(name: str, search: list[Path], system_dir: Path | None) -> tuple[str, Path | None]:
    if GPU_RUNTIME.match(name):
        return "gpu-runtime", None
    if MSVC_RUNTIME.match(name):
        return "msvc-runtime", None
    if API_SET.match(name):
        return "system", None
    if system_dir is not None and (system_dir / name).is_file():
        return "system", None
    if system_dir is None and name.lower() in WINDOWS_CORE:
        return "system", None
    for d in search:  # case-insensitive, as Windows is
        for p in d.iterdir():
            if p.name.lower() == name.lower():
                return "bundled", p
    return "unresolved", None


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def bundle(exes: list[Path], also: list[Path], search: list[Path], system_dir: Path | None, out: Path) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    files: dict[str, dict] = {}
    external: dict[str, set[str]] = {"gpu-runtime": set(), "msvc-runtime": set()}
    unresolved: list[str] = []
    queue: list[tuple[Path, str]] = [(p, "root") for p in exes + also]
    while queue:
        src, needed_by = queue.pop()
        key = src.name.lower()
        if key in files:
            continue
        shutil.copy2(src, out / src.name)
        files[key] = {"file": src.name, "sha256": sha256(src), "from": str(src), "needed_by": needed_by}
        for dep in imports(src):
            kind, path = classify(dep, search, system_dir)
            if kind == "bundled":
                queue.append((path, src.name))
            elif kind in external:
                external[kind].add(dep)
            elif kind == "unresolved":
                unresolved.append(f"{dep} (needed by {src.name})")
    manifest = {"files": sorted(files.values(), key=lambda f: f["file"].lower()),
                "requires": {k: sorted(v, key=str.lower) for k, v in external.items()},
                "unresolved": sorted(set(unresolved))}
    (out / "bundle.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--exe", type=Path, nargs="+", required=True)
    ap.add_argument("--also", type=Path, nargs="*", default=[], help="DLLs loaded at run time with LoadLibrary")
    ap.add_argument("--search", type=Path, nargs="+", required=True)
    ap.add_argument("--system-dir", type=Path)
    a = ap.parse_args(argv)
    m = bundle(a.exe, a.also, a.search, a.system_dir, a.out)
    print(f"bundled {len(m['files'])} files into {a.out}")
    for k, v in m["requires"].items():
        print(f"  requires ({k}): {', '.join(v) or '—'}")
    if m["unresolved"]:
        print("UNRESOLVED — the release would not start:\n  " + "\n  ".join(m["unresolved"]), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
