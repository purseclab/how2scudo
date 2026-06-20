#!/usr/bin/env python3
"""Find Android Scudo's static allocator symbol in an ELF libc.so.

Android bionic builds Scudo into libc and, when local symbols are present, the
file-static allocator object is usually named `_ZL9Allocator`.

For the Android Scudo builds this project targets, the heap cookie is the first
field in that allocator object, so:

    Cookie = _ZL9Allocator + 0x0

This script intentionally avoids pyelftools so it can run in a minimal Python
environment.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


SHT_SYMTAB = 2
SHT_DYNSYM = 11

DEFAULT_SYMBOL = "_ZL9Allocator"


@dataclass
class Section:
    name_offset: int
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    addralign: int
    entsize: int
    name: str = ""


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    info: int
    other: int
    shndx: int
    table: str


class ElfError(Exception):
    pass


def cstr(buf: bytes, offset: int) -> str:
    if offset >= len(buf):
        return ""
    end = buf.find(b"\x00", offset)
    if end == -1:
        end = len(buf)
    return buf[offset:end].decode("utf-8", "replace")


def read_exact(data: bytes, offset: int, size: int) -> bytes:
    chunk = data[offset : offset + size]
    if len(chunk) != size:
        raise ElfError(f"truncated ELF while reading {size} bytes at 0x{offset:x}")
    return chunk


def parse_elf(data: bytes) -> tuple[str, int, list[Section]]:
    if len(data) < 16 or data[:4] != b"\x7fELF":
        raise ElfError("not an ELF file")

    elf_class = data[4]
    elf_data = data[5]
    if elf_class not in (1, 2):
        raise ElfError(f"unsupported ELF class: {elf_class}")
    if elf_data == 1:
        endian = "<"
    elif elf_data == 2:
        endian = ">"
    else:
        raise ElfError(f"unsupported ELF data encoding: {elf_data}")

    is_64 = elf_class == 2

    if is_64:
        header_fmt = endian + "HHIQQQIHHHHHH"
        header_size = struct.calcsize(header_fmt)
        fields = struct.unpack(header_fmt, read_exact(data, 16, header_size))
        (
            _e_type,
            _e_machine,
            _e_version,
            _e_entry,
            _e_phoff,
            e_shoff,
            _e_flags,
            _e_ehsize,
            _e_phentsize,
            _e_phnum,
            e_shentsize,
            e_shnum,
            e_shstrndx,
        ) = fields
        sh_fmt = endian + "IIQQQQIIQQ"
    else:
        header_fmt = endian + "HHIIIIIHHHHHH"
        header_size = struct.calcsize(header_fmt)
        fields = struct.unpack(header_fmt, read_exact(data, 16, header_size))
        (
            _e_type,
            _e_machine,
            _e_version,
            _e_entry,
            _e_phoff,
            e_shoff,
            _e_flags,
            _e_ehsize,
            _e_phentsize,
            _e_phnum,
            e_shentsize,
            e_shnum,
            e_shstrndx,
        ) = fields
        sh_fmt = endian + "IIIIIIIIII"

    min_shentsize = struct.calcsize(sh_fmt)
    if e_shentsize < min_shentsize:
        raise ElfError("section header entry size is too small")
    if e_shoff == 0:
        raise ElfError("ELF has no section header table")

    sections: list[Section] = []
    for i in range(e_shnum):
        raw = read_exact(data, e_shoff + i * e_shentsize, min_shentsize)
        sections.append(Section(*struct.unpack(sh_fmt, raw)))

    if e_shnum == 0 and sections:
        e_shnum = sections[0].size
    if e_shstrndx == 0xFFFF and sections:
        e_shstrndx = sections[0].link

    if e_shstrndx >= len(sections):
        raise ElfError("invalid section-name string table index")

    shstr = sections[e_shstrndx]
    shstr_data = read_exact(data, shstr.offset, shstr.size)
    for section in sections:
        section.name = cstr(shstr_data, section.name_offset)

    return endian, elf_class, sections


def parse_symbols(data: bytes, endian: str, elf_class: int, sections: list[Section]) -> list[Symbol]:
    is_64 = elf_class == 2
    if is_64:
        sym_fmt = endian + "IBBHQQ"
    else:
        sym_fmt = endian + "IIIBBH"

    sym_size = struct.calcsize(sym_fmt)
    symbols: list[Symbol] = []

    for section in sections:
        if section.type not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        if section.link >= len(sections):
            continue

        strtab = sections[section.link]
        strtab_data = read_exact(data, strtab.offset, strtab.size)
        entsize = section.entsize or sym_size
        if entsize < sym_size:
            continue

        count = section.size // entsize
        for i in range(count):
            raw = read_exact(data, section.offset + i * entsize, sym_size)
            if is_64:
                st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack(
                    sym_fmt, raw
                )
            else:
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack(
                    sym_fmt, raw
                )

            name = cstr(strtab_data, st_name)
            if name:
                symbols.append(
                    Symbol(
                        name=name,
                        value=st_value,
                        size=st_size,
                        info=st_info,
                        other=st_other,
                        shndx=st_shndx,
                        table=section.name,
                    )
                )

    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Find Scudo's _ZL9Allocator symbol in an Android libc.so"
    )
    parser.add_argument("libc", type=Path, help="path to libc.so")
    parser.add_argument(
        "--symbol",
        default=DEFAULT_SYMBOL,
        help=f"symbol to find, default: {DEFAULT_SYMBOL}",
    )
    args = parser.parse_args()

    data = args.libc.read_bytes()
    endian, elf_class, sections = parse_elf(data)
    symbols = parse_symbols(data, endian, elf_class, sections)
    matches = [sym for sym in symbols if sym.name == args.symbol]

    if not matches:
        print(f"{args.symbol}: not found")
        print("Note: release libc.so builds may strip local symbols like _ZL9Allocator.")
        return 1

    for sym in matches:
        print(f"symbol:      {sym.name}")
        print(f"table:       {sym.table}")
        print(f"value:       0x{sym.value:x}")
        print(f"size:        0x{sym.size:x} ({sym.size} bytes)")
        print(f"cookie expr: libc_base + 0x{sym.value:x}")
        print("cookie note: Cookie is at _ZL9Allocator + 0x0 for Android Scudo")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ElfError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
