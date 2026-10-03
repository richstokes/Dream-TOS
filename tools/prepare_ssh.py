#!/usr/bin/env python3
"""Create a private SSH seed file for Dream TOS on a mounted SD card or staging folder."""
import argparse
import hashlib
import os
from pathlib import Path
import secrets


def create_seed(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    record = b'DCSSH001' + secrets.token_bytes(64)
    record += hashlib.sha256(record).digest()
    path = directory / 'SEED.BIN'
    # Never overwrite a live seed: restoring old state can repeat session keys.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as output:
        output.write(record)
        output.flush()
        os.fsync(output.fileno())
    return path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='e.g. /Volumes/SD/SSH')
    args = parser.parse_args()
    print(f'Created {create_seed(args.directory)}. Keep it private and do not reuse copies.')
