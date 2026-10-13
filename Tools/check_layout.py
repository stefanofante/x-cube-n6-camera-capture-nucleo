#!/usr/bin/env python3
"""Check the linked Nucleo SRAM budget, boot addresses and runtime dependencies."""
import argparse
import pathlib
import re
import subprocess
import struct


def sections(path, prefix):
    output = subprocess.check_output([prefix + "readelf", "-W", "-S", str(path)], text=True)
    result = {}
    pattern = re.compile(r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+([A-Z]+)")
    for match in pattern.finditer(output):
        name, kind, address, _, size, flags = match.groups()
        result[name] = (kind, int(address, 16), int(size, 16), flags)
    if ".isr_vector" not in result:
        raise ValueError(f"No vector section found in {path}")
    return result


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check(app, fsbl, prefix):
    app_sections = sections(app, prefix)
    fsbl_sections = sections(fsbl, prefix)
    require(app_sections[".isr_vector"][1] == 0x34000400, "Application vectors do not match FSBL destination/header")
    require(fsbl_sections[".isr_vector"][1] == 0x34180400, "FSBL vectors do not match the ROM load address")
    limits = {".dma_bss": (0x34070000, 0x341F0000), ".uncached_bss": (0x341F0000, 0x34200000)}
    for name, (kind, address, size, flags) in app_sections.items():
        if "A" not in flags or not size:
            continue
        lower, upper = limits.get(name, (0x34000400, 0x34070000))
        require(lower <= address and address + size <= upper, f"Application {name} exceeds its internal SRAM region")
        if name in limits:
            require(kind == "NOBITS", f"DMA section {name} would be copied over the running FSBL")
    for name, (_, address, size, flags) in fsbl_sections.items():
        if "A" in flags and size:
            require(0x34180400 <= address and address + size <= 0x34200000, f"FSBL {name} exceeds SRAM2")
    binary = app.with_suffix(".bin").read_bytes()
    payload = len(binary)
    stack, reset = struct.unpack_from("<II", binary)
    require(stack == 0x34070000, "Application stack does not match the boot validator")
    _, text_begin, text_size, _ = app_sections[".text"]
    require(reset & 1 and text_begin <= (reset & ~1) < text_begin + text_size,
            "Application reset vector does not point to loaded Thumb code")
    require(payload + 0x400 <= 448 * 1024, "Signed application would exceed the bounded boot copy area")
    for path in (app, fsbl):
        symbols = subprocess.check_output([prefix + "nm", "-C", str(path)], text=True)
        require("HAL_BSEC_OTP_Program" not in symbols, f"Automatic OTP programming remains linked in {path}")
        if path == app:
            require(not re.search(r"operator (new|delete)|__cxa_|_GLOBAL__sub_I", symbols),
                    "Unexpected C++ heap/runtime or global initializer dependency")
    dma = app_sections[".dma_bss"][2]
    usb = app_sections[".uncached_bss"][2]
    print(f"Application payload: {payload} B; bounded signed capacity: {448 * 1024} B")
    print(f"Internal DMA: {dma} / {1536 * 1024} B; remaining: {1536 * 1024 - dma} B")
    print(f"Internal USB: {usb} / {64 * 1024} B; remaining: {64 * 1024 - usb} B")
    print("Boot/vector layout, NOLOAD buffers, OTP exclusion and C++ runtime checks passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=pathlib.Path, default=pathlib.Path("build/Debug/x-cube-n6-camera-capture-nucleo.elf"))
    parser.add_argument("--fsbl", type=pathlib.Path, default=pathlib.Path("Tools/FSBL/build/FSBL.elf"))
    parser.add_argument("--prefix", default="arm-none-eabi-")
    arguments = parser.parse_args()
    check(arguments.app, arguments.fsbl, arguments.prefix)
