# SPDX-License-Identifier: GPL-2.0-only
#!/usr/bin/env python3
"""Pack native PanVK and the MaliMetal bundle into a UniXNU GPU addon."""
import argparse
import plistlib
import struct
import zlib
from pathlib import Path


def pad(data):
    return data + bytes(-len(data) % 8)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dylib', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    if not args.dylib.is_file():
        raise SystemExit(f'missing dylib: {args.dylib}')
    bundle = 'addons/NativeMaliMetal.bundle'
    icd = {
        'file_format_version': '1.0.0',
        'ICD': {
            'library_path': 'libvulkan_panfrost.dylib',
            'api_version': '1.4.0',
        },
    }
    info = {
        'CFBundleIdentifier': 'org.dracen.NativeMaliMetal',
        'CFBundleName': 'NativeMaliMetal',
        'CFBundlePackageType': 'BNDL',
        'CFBundleVersion': '0.1.0',
    }
    files = [
        (f'{bundle}/libvulkan_panfrost.dylib', 0o755, args.dylib.read_bytes()),
        (f'{bundle}/panfrost_icd.json', 0o644,
         (plistlib.dumps(icd, fmt=plistlib.FMT_JSON)).decode().encode()),
        (f'{bundle}/Info.plist', 0o644, plistlib.dumps(info)),
    ]
    if args.publish:
        files.append(('addons/NativeMaliMetal.publish', 0o644, b'1\n'))
    body = b''
    for name, mode, data in files:
        encoded = name.encode() + b'\0'
        if len(encoded) > 160 or '..' in name.split('/') or name.startswith('/'):
            raise SystemExit(f'bad addon path: {name}')
        packed = zlib.compress(data, 9)
        body += struct.pack('<IIQQ', len(encoded), mode, len(data), len(packed)) + pad(encoded) + pad(packed)
    blob = b'NGPUADD2' + struct.pack('<IIQ', len(files), 0, 24 + len(body)) + body
    args.output.write_bytes(blob)
    print(f'{args.output}: {len(files)} files, {len(blob)} bytes')


if __name__ == '__main__':
    main()
