"""Pinned ANGLE dependency for the standalone iOS rendering probe."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

COMMIT = '97d33bc6e1356dcb2e63ea4ca7e6ebd2bc81a39d'
TAG = 'angle-97d33bc'
ARCHIVE_SHA256 = 'dca3d8520a0dc5b2a334441d861c01d2d7524c7547c3a54d4286cccf0b57de99'
LICENSE_SHA256 = 'bf4da21bd20bcfb5b60b7ecc67fa864a79be049e21d6178076887f178dd6c71a'


def download_verified(path, url, digest):
    if not path.exists():
        temporary = path.with_suffix('.download')
        with urllib.request.urlopen(url, timeout=60) as response:
            temporary.write_bytes(response.read())
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != digest:
            temporary.unlink()
            raise RuntimeError('Unexpected SHA-256 for ' + url)
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Cached dependency checksum mismatch: ' + str(path))


def prepare(root: Path):
    cache = root / 'build-ios-deps' / TAG
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / 'angle-ios-universal.zip'
    download_verified(archive,
        f'https://github.com/jeremyfa/build-angle/releases/download/{TAG}/{archive.name}',
        ARCHIVE_SHA256)
    license_file = cache / 'LICENSE'
    download_verified(license_file,
        f'https://raw.githubusercontent.com/google/angle/{COMMIT}/LICENSE', LICENSE_SHA256)
    destination = cache / 'extracted'
    # Re-extract the verified archive so local extracted edits cannot silently
    # replace the pinned binaries or headers on the next build.
    with zipfile.ZipFile(archive) as bundle:
        for entry in bundle.infolist():
            if not (destination / entry.filename).resolve().is_relative_to(destination.resolve()):
                raise RuntimeError('Invalid archive path')
        bundle.extractall(destination)
    if (destination / 'commit.txt').read_text().strip() != COMMIT:
        raise RuntimeError('Unexpected ANGLE source revision')
    return destination, license_file
