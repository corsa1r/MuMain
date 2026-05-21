"""
Generate an empty (no NPC markers) Minimap_WorldN_Eng.bmd file.

File format:
  100 records * 116 bytes (BuxConvert-encrypted MINI_MAP_FILE structs)
  45 bytes of zeros (footer, not encrypted)
  4 bytes DWORD checksum (little-endian)
  = 11649 bytes total

MINI_MAP_FILE (MSVC default packing, sizeof=116):
  BYTE Kind          1 byte  + 3 bytes padding
  int  Location[2]   8 bytes
  int  Rotation      4 bytes
  char Name[100]     100 bytes

BuxConvert: XOR each byte with {0xFC, 0xCF, 0xAB}[i % 3], i resets per record.
GenerateCheckSum2: see ZzzInfomation.h
"""

import struct
import sys
import os

BUX_CODE = bytes([0xFC, 0xCF, 0xAB])
KEY = 0x2BC1
RECORD_SIZE = 116
MAX_RECORDS = 100
FOOTER_SIZE = 45
EXPECTED_DATA_SIZE = RECORD_SIZE * MAX_RECORDS + FOOTER_SIZE  # 11645
EXPECTED_FILE_SIZE = EXPECTED_DATA_SIZE + 4                   # 11649


def bux_convert(data: bytes) -> bytes:
    result = bytearray(data)
    for i in range(len(result)):
        result[i] ^= BUX_CODE[i % 3]
    return bytes(result)


def generate_checksum2(buf: bytes) -> int:
    key = KEY
    result = (key << 9) & 0xFFFFFFFF
    size = len(buf)
    checked = 0
    while checked <= size - 4:
        temp = struct.unpack_from('<I', buf, checked)[0]
        if (checked // 4 + key) % 2 == 0:
            result ^= temp
        else:
            result = (result + temp) & 0xFFFFFFFF
        if (checked % 16) == 0:
            shift = (checked // 4) % 8 + 1
            result ^= ((key + result) >> shift) & 0xFFFFFFFF
        result &= 0xFFFFFFFF
        checked += 4
    return result


def generate_empty_bmd(output_path: str) -> None:
    data = bytearray()

    # 100 empty records (Kind=0 means no marker), encrypted with BuxConvert
    empty_record = bytes(RECORD_SIZE)
    encrypted_record = bux_convert(empty_record)
    for _ in range(MAX_RECORDS):
        data.extend(encrypted_record)

    # 45-byte footer (zeros, not encrypted)
    data.extend(bytes(FOOTER_SIZE))

    assert len(data) == EXPECTED_DATA_SIZE, f"Data size mismatch: {len(data)}"

    checksum = generate_checksum2(bytes(data))

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(bytes(data))
        f.write(struct.pack('<I', checksum))

    print(f"Written {EXPECTED_FILE_SIZE} bytes to: {output_path}")
    print(f"Checksum: 0x{checksum:08X}")


def verify_bmd(path: str) -> bool:
    """Verify an existing BMD file's checksum."""
    with open(path, 'rb') as f:
        content = f.read()
    if len(content) != EXPECTED_FILE_SIZE:
        print(f"FAIL: expected {EXPECTED_FILE_SIZE} bytes, got {len(content)}")
        return False
    data = content[:EXPECTED_DATA_SIZE]
    stored_checksum = struct.unpack_from('<I', content, EXPECTED_DATA_SIZE)[0]
    computed_checksum = generate_checksum2(data)
    if stored_checksum == computed_checksum:
        print(f"OK: checksum 0x{computed_checksum:08X} matches")
        return True
    else:
        print(f"FAIL: stored 0x{stored_checksum:08X}, computed 0x{computed_checksum:08X}")
        return False


if __name__ == '__main__':
    base = r"c:\Users\vladi\Documents\BloodlustMU Project\MuMain\out\build\windows-x64\src\Release\Data\Local"

    # Verify World1 to confirm the algorithm is correct
    world1 = os.path.join(base, "Eng", "Minimap", "Minimap_World1_eng.bmd")
    print(f"Verifying World1: {world1}")
    if not verify_bmd(world1):
        print("Algorithm verification failed — aborting.")
        sys.exit(1)
    print()

    # Generate empty BMD for all language variants
    targets = [
        (os.path.join(base, "Eng", "Minimap", "Minimap_World57_Eng.bmd"), "World57 Eng"),
        (os.path.join(base, "Por", "Minimap", "Minimap_World57_Por.bmd"), "World57 Por"),
        (os.path.join(base, "Spn", "Minimap", "Minimap_World57_Spn.bmd"), "World57 Spn"),
    ]
    for path, label in targets:
        print(f"Generating {label}:")
        generate_empty_bmd(path)
        verify_bmd(path)
        print()
