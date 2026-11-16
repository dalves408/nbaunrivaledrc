"""Extract this project's contiguous STFS package, verifying every data block."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/XBLA-Extract'))
import stfs_extract as stfs


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    archive = ROOT / 'NBA Unrivaled (World) (XBLA).zip'
    package_root = ROOT / 'private/package'
    package_root.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            target = (package_root / entry.filename).resolve()
            if not target.is_relative_to(package_root.resolve()):
                raise ValueError('Unsafe archive path')
            if not target.exists():
                source.extract(entry, package_root)
    packages = [p for p in package_root.rglob('*') if p.is_file()]
    if len(packages) != 1:
        raise ValueError('Expected exactly one STFS package')
    package = packages[0]
    raw = package.read_bytes()
    if raw[0x360:0x364].hex() != '584109d4':
        raise ValueError('Unexpected title ID')
    _, base, spacing, _ = stfs._parse_stfs_header(package)
    entries = stfs.list_live_pirs(package)
    output = ROOT / 'private/game'
    output.mkdir(parents=True, exist_ok=True)
    manifest = []
    verified = set()

    def block_offset(number):
        return base + number * 4096 + stfs.get_cluster(number, spacing)

    for entry in entries:
        target = (output / entry['path']).resolve()
        if not target.is_relative_to(output.resolve()):
            raise ValueError('Unsafe STFS path')
        if entry['is_dir']:
            target.mkdir(parents=True, exist_ok=True)
            continue
        if not entry['is_contiguous']:
            raise ValueError('Fragmented STFS files require a chain-aware extractor')
        data = bytearray()
        count = (entry['size'] + 4095) // 4096
        for number in range(entry['startclust'], entry['startclust'] + count):
            offset = block_offset(number)
            block = raw[offset:offset + 4096]
            hash_offset = block_offset(number // 170 * 170) - spacing + number % 170 * 24
            if len(block) != 4096 or hashlib.sha1(block).digest() != raw[hash_offset:hash_offset + 20]:
                raise ValueError(f'STFS block hash mismatch at {number}')
            verified.add(number)
            data.extend(block)
        data = data[:entry['size']]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        manifest.append({'path': entry['path'], 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    xex = output / 'default.xex'
    if xex.read_bytes()[:4] != b'XEX2':
        raise ValueError('Missing valid XEX2 executable')
    report = {'title_id': '584109D4', 'archive_sha256': sha256(archive),
              'package_sha256': sha256(package), 'verified_data_blocks': len(verified),
              'files': manifest}
    (ROOT / 'private/input-manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Extracted {len(manifest)} files; verified {len(verified)} STFS data blocks')
    print(f'default.xex SHA256: {sha256(xex)}')


if __name__ == '__main__':
    main()
