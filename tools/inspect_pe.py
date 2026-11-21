"""Inspect the release PE without executing it or needing third-party modules."""

import hashlib
import json
from pathlib import Path
import struct
import sys


def inspect(path: Path) -> dict:
    data = path.read_bytes()

    def u16(offset):
        return struct.unpack_from("<H", data, offset)[0]

    def u32(offset):
        return struct.unpack_from("<I", data, offset)[0]

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    require(data[:2] == b"MZ", "Missing DOS header")
    pe = u32(0x3C)
    require(data[pe:pe + 4] == b"PE\0\0", "Missing PE signature")
    require(u16(pe + 4) == 0x8664, "Expected AMD64 machine")
    optional = pe + 24
    require(u16(optional) == 0x20B, "Expected PE32+ optional header")
    require(u16(optional + 68) == 2, "Expected Windows GUI subsystem")
    require(u16(pe + 22) & 0x0002, "Image is not executable")
    require(not u16(pe + 22) & 0x2000, "Expected an EXE, not a DLL")
    flags = u16(optional + 70)
    require(flags & 0x20 and flags & 0x40 and flags & 0x100,
            "Expected high-entropy ASLR, dynamic base, and DEP")

    sections = []
    section_header = optional + u16(pe + 20)
    for i in range(u16(pe + 6)):
        offset = section_header + i * 40
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode("ascii")
        virtual_size, rva, raw_size, raw = struct.unpack_from("<IIII", data, offset + 8)
        sections.append((name, rva, max(virtual_size, raw_size), raw))

    def file_offset(rva):
        for _, start, size, raw in sections:
            if start <= rva < start + size:
                return raw + rva - start
        raise ValueError(f"RVA outside sections: {rva:#x}")

    imports = []
    functions = set()
    import_rva = u32(optional + 112 + 8)
    require(import_rva != 0, "Missing import table")
    cursor = file_offset(import_rva)
    while any(data[cursor:cursor + 20]):
        name_offset = file_offset(u32(cursor + 12))
        end = data.index(b"\0", name_offset)
        imports.append(data[name_offset:end].decode("ascii"))
        thunk = file_offset(u32(cursor) or u32(cursor + 16))
        while (entry := struct.unpack_from("<Q", data, thunk)[0]):
            if not entry & (1 << 63):
                start = file_offset(entry) + 2
                functions.add(data[start:data.index(b"\0", start)].decode("ascii"))
            thunk += 8
        cursor += 20
    allowed = {"ADVAPI32.dll", "KERNEL32.dll", "USER32.dll", "GDI32.dll", "SHELL32.dll", "dwmapi.dll", "msvcrt.dll"}
    require({name.lower() for name in imports} <= {name.lower() for name in allowed},
            f"Unexpected runtime dependency: {imports}")
    require({"user32.dll", "dwmapi.dll", "kernel32.dll"} <= {name.lower() for name in imports},
            "Missing expected Win32 imports")
    workspace_apis = {"EnumWindows", "EnumDisplayMonitors", "GetCursorPos", "MonitorFromPoint",
                      "MonitorFromWindow", "SetPropW", "GetPropW", "RemovePropW", "SetWindowPos",
                      "RegisterHotKey", "UnregisterHotKey", "DwmGetWindowAttribute",
                      "SetWindowsHookExW", "UnhookWindowsHookEx", "CallNextHookEx", "SendInput",
                      "Shell_NotifyIconW", "CreateIconIndirect", "DestroyIcon", "CreatePopupMenu",
                      "TrackPopupMenu", "RegisterWindowMessageW"}
    require(workspace_apis <= functions, "Missing expected workspace API imports")
    require(any(name == ".rsrc" for name, *_ in sections), "Missing resource section")
    require("PerMonitorV2".encode() in data, "Missing per-monitor DPI manifest")
    require("asInvoker".encode() in data, "Missing asInvoker manifest")
    certificate_offset = u32(optional + 112 + 8 * 4)
    certificate_size = u32(optional + 112 + 8 * 4 + 4)
    require(certificate_offset == 0 and certificate_size == 0, "Expected the documented unsigned build")
    return {
        "file": path.name,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "format": "PE32+ AMD64 Windows GUI",
        "aslr": True,
        "high_entropy_aslr": True,
        "dep": True,
        "signed": False,
        "dpi_awareness": "PerMonitorV2",
        "imports": sorted(imports, key=str.lower),
        "workspace_apis": sorted(workspace_apis),
    }


if __name__ == "__main__":
    print(json.dumps(inspect(Path(sys.argv[1])), indent=2))
