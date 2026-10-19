"""Bounded AMD64 PE import metadata; never load or execute audited images.

Format: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
This extracts DLL names, not exported-symbol/API or dynamic-loading compatibility.
"""
import hashlib
from pathlib import Path
import re
import stat
import struct

MAX_PE_BYTES = 128 * 1024 * 1024
MAX_IMPORTS = 2048


def parse_imports(data):
    if not isinstance(data, bytes) or len(data) > MAX_PE_BYTES:
        raise ValueError('PE input exceeds its byte limit or is not immutable bytes')

    def read(offset, length):
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ValueError('Truncated PE metadata')
        return data[offset:offset + length]

    def number(offset, code='I'):
        return struct.unpack('<' + code, read(offset, struct.calcsize('<' + code)))[0]

    if read(0, 2) != b'MZ':
        raise ValueError('Missing DOS image signature')
    pe = number(0x3c)
    if read(pe, 4) != b'PE\0\0' or number(pe + 4, 'H') != 0x8664:
        raise ValueError('An AMD64 PE image is required')
    count = number(pe + 6, 'H')
    optional_size = number(pe + 20, 'H')
    optional = pe + 24
    read(optional, optional_size)
    if not 1 <= count <= 96 or optional_size < 112 or number(optional, 'H') != 0x20b:
        raise ValueError('Invalid PE32+ header or section count')
    directories = number(optional + 108)
    if directories > 16 or 112 + directories * 8 > optional_size:
        raise ValueError('Invalid PE data-directory bounds')
    section_table = optional + optional_size
    headers = number(optional + 60)
    if not section_table + count * 40 <= headers <= len(data):
        raise ValueError('Invalid PE header byte range')
    sections = []
    for index in range(count):
        entry = section_table + index * 40
        virtual, rva, raw_size, raw = struct.unpack('<4I', read(entry + 8, 16))
        span = max(virtual, raw_size)
        if rva + span > 2**32 or (span and rva < headers):
            raise ValueError('Invalid PE section virtual range')
        if raw_size and (raw < headers or raw + raw_size > len(data)):
            raise ValueError('Invalid PE section file range')
        for other_rva, other_span, other_raw, other_size in sections:
            if (span and other_span and rva < other_rva + other_span and other_rva < rva + span):
                raise ValueError('Overlapping PE section virtual ranges')
            if (raw_size and other_size and raw < other_raw + other_size and other_raw < raw + raw_size):
                raise ValueError('Overlapping PE section file ranges')
        sections.append((rva, span, raw, raw_size))

    def offset(rva, length):
        if not 0 <= rva < 2**32 or length <= 0 or rva + length > 2**32:
            raise ValueError('Invalid PE relative address')
        if rva + length <= headers:
            return rva
        for start, span, raw, raw_size in sections:
            if start <= rva and rva + length <= start + span:
                relative = rva - start
                if relative + length > raw_size:
                    raise ValueError('PE relative address has no backing file bytes')
                return raw + relative
        raise ValueError('PE relative address is outside mapped file bytes')

    def dll_name(rva):
        if not rva:
            raise ValueError('Missing imported DLL name')
        name = bytearray()
        for index in range(260):
            value = data[offset(rva + index, 1)]
            if value == 0:
                try:
                    decoded = name.decode('ascii')
                except UnicodeError:
                    raise ValueError('Imported DLL name is not ASCII') from None
                if not re.fullmatch(r'[a-zA-Z0-9_+.-]+\.dll', decoded, re.IGNORECASE):
                    raise ValueError('Imported DLL name is not a safe basename')
                return decoded.lower()
            name.append(value)
        raise ValueError('Imported DLL name exceeds its byte limit')

    def imports(directory, stride, name_index):
        if directories <= directory:
            return []
        rva, size = struct.unpack('<2I', read(optional + 112 + directory * 8, 8))
        if rva == size == 0:
            return []
        if not rva or size < stride:
            raise ValueError('Invalid import directory range')
        base = offset(rva, size)
        names = set()
        for index in range(min(size // stride, MAX_IMPORTS + 1)):
            descriptor = struct.unpack('<' + 'I' * (stride // 4), read(base + index * stride, stride))
            if not any(descriptor):
                return sorted(names)
            if index >= MAX_IMPORTS:
                raise ValueError('Import descriptor count exceeds its limit')
            if directory == 13 and descriptor[0] != 1:
                raise ValueError('Only RVA-based delay imports are supported')
            names.add(dll_name(descriptor[name_index]))
        raise ValueError('Import directory has no bounded terminator')

    return {'machine': 'amd64', 'imports': imports(1, 20, 3),
            'delayImports': imports(13, 32, 1)}


def inspect_pe(path):
    path = Path(path)
    if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
        raise ValueError('PE input must not be linked')
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_PE_BYTES:
        raise ValueError('PE input is not a bounded ordinary file')
    with path.open('rb') as stream:
        data = stream.read(MAX_PE_BYTES + 1)
    parsed = parse_imports(data)
    return {**parsed, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
