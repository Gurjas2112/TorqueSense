import json, struct, sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import bundle


def make_pe(path, imports=(), delay=()):
    """A minimal PE32+ file: headers and one section holding the import tables."""
    sect_rva, sect_raw = 0x1000, 0x200
    data = bytearray()
    names = {}
    for n in list(imports) + list(delay):
        names[n] = sect_rva + len(data)
        data += n.encode() + b"\0"
    while len(data) % 4:
        data += b"\0"
    imp_rva = sect_rva + len(data)
    for n in imports:
        data += struct.pack("<IIIII", 0, 0, 0, names[n], 0)
    data += b"\0" * 20
    dly_rva = sect_rva + len(data) if delay else 0
    for n in delay:
        data += struct.pack("<IIIIIIII", 1, names[n], 0, 0, 0, 0, 0, 0)
    if delay:
        data += b"\0" * 32
    opt_size = 112 + 16 * 8
    b = bytearray(sect_raw)
    b[0:2] = b"MZ"
    struct.pack_into("<I", b, 0x3C, 0x40)
    b[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HH", b, 0x44, 0x8664, 1)              # machine, one section
    struct.pack_into("<H", b, 0x44 + 16, opt_size)
    opt = 0x58
    struct.pack_into("<H", b, opt, 0x20B)
    struct.pack_into("<II", b, opt + 112 + 8, imp_rva if imports else 0, 0)
    struct.pack_into("<II", b, opt + 112 + 8 * 13, dly_rva, 0)
    s = opt + opt_size
    b[s:s + 8] = b".idata\0\0"
    struct.pack_into("<IIII", b, s + 8, len(data), sect_rva, len(data), sect_raw)
    path.write_bytes(bytes(b) + bytes(data))
    return path


def test_reads_normal_and_delay_imports(tmp_path):
    p = make_pe(tmp_path / "a.exe", ["KERNEL32.dll", "libstdc++-6.dll"], ["nvinfer_10.dll"])
    assert bundle.imports(p) == ["KERNEL32.dll", "libstdc++-6.dll", "nvinfer_10.dll"]


def test_bundle_classifies_and_recurses(tmp_path):
    src, out = tmp_path / "src", tmp_path / "out"
    src.mkdir()
    exe = make_pe(src / "torquesense.exe", ["KERNEL32.dll", "onnxruntime.dll", "libstdc++-6.dll"])
    make_pe(src / "onnxruntime.dll", ["MSVCP140.dll", "api-ms-win-crt-heap-l1-1-0.dll"])
    make_pe(src / "libstdc++-6.dll", ["libwinpthread-1.dll"])
    make_pe(src / "libwinpthread-1.dll", ["msvcrt.dll"])
    prov = make_pe(src / "onnxruntime_providers_tensorrt.dll", ["nvinfer_10.dll", "cudnn64_9.dll"],
                   ["onnxruntime_providers_shared.dll"])
    make_pe(src / "onnxruntime_providers_shared.dll", ["KERNEL32.dll"])
    m = bundle.bundle([exe], [prov], [src], None, out)
    got = {f["file"] for f in m["files"]}
    assert got == {"torquesense.exe", "onnxruntime.dll", "libstdc++-6.dll", "libwinpthread-1.dll",
                   "onnxruntime_providers_tensorrt.dll", "onnxruntime_providers_shared.dll"}
    assert m["requires"] == {"gpu-runtime": ["cudnn64_9.dll", "nvinfer_10.dll"], "msvc-runtime": ["MSVCP140.dll"]}
    assert m["unresolved"] == []
    assert (out / "libwinpthread-1.dll").is_file()
    assert json.loads((out / "bundle.json").read_text())["files"][0]["sha256"]


def test_missing_dll_fails_the_bundle(tmp_path):
    src = tmp_path / "src"
    src.mkdir()
    exe = make_pe(src / "softplc.exe", ["KERNEL32.dll", "libopen62541.dll"])
    rc = bundle.main(["--out", str(tmp_path / "out"), "--exe", str(exe), "--search", str(src)])
    assert rc == 1
    m = json.loads((tmp_path / "out" / "bundle.json").read_text())
    assert m["unresolved"] == ["libopen62541.dll (needed by softplc.exe)"]


def test_system_dir_decides_when_given(tmp_path):
    src, sysdir = tmp_path / "src", tmp_path / "System32"
    src.mkdir(); sysdir.mkdir()
    (sysdir / "mfplat.dll").write_bytes(b"")
    exe = make_pe(src / "a.exe", ["mfplat.dll", "dwmapi.dll"])
    m = bundle.bundle([exe], [], [src], sysdir, tmp_path / "out")
    assert m["unresolved"] == ["dwmapi.dll (needed by a.exe)"]   # not in this System32
