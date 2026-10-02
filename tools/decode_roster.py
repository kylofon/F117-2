#!/usr/bin/env python3
"""Decoder for F-117A ROSTER.FIL"""
import struct
import sys

def parse_pilot_record(data):
    """Parse 80-byte pilot record. Return dict of fields."""
    if len(data) != 80:
        raise ValueError(f"Record size {len(data)} != 80")

    # Offset 0x00-0x01: Unknown word (always 2 in all checked records except one with 1 or 0)
    # Offset 0x02-0x03: Pilot index (0-9 ascending, with duplicates like 08 08 at end)
    index_lo = struct.unpack('<H', data[0x00:0x02])[0]
    index_hi = struct.unpack('<H', data[0x02:0x04])[0]

    # Offset 0x04-0x1D: Pilot name (30 bytes, null-terminated)
    name_bytes = data[0x04:0x1E]
    name = name_bytes.split(b'\x00')[0].decode('ascii', errors='replace')

    # Offset 0x1E-0x4F: Stats (50 bytes)
    # The structure is unclear without source, but appears to be:
    # - Bytes related to rank, skill, experience
    # - Score/kill counts
    # - Mission statistics
    stats = data[0x1E:0x50]

    return {
        'unknown_word': index_lo,
        'index': index_hi,
        'name': name,
        'stats_raw': stats,
        'stats_words': [struct.unpack('<H', stats[i:i+2])[0] for i in range(0, len(stats), 2)],
        'stats_bytes': list(stats),
    }

def dump_record(rec, num):
    """Print a parsed pilot record."""
    print(f"\nPilot {num}:")
    print(f"  Name: {rec['name']}")
    print(f"  Index words: {rec['unknown_word']:04X} {rec['index']:04X}")
    print(f"  Stats (25 words):")
    for i, w in enumerate(rec['stats_words']):
        if i % 5 == 0:
            print(f"    +{(i*2):02X}:", end='')
        print(f" {w:04X}", end='')
        if (i + 1) % 5 == 0:
            print()
    if len(rec['stats_words']) % 5 != 0:
        print()

def main():
    path = 'C:\\Coding\\F117-2\\Game\\ROSTER.FIL'

    with open(path, 'rb') as f:
        data = f.read()

    print(f"ROSTER.FIL: {len(data)} bytes")
    print(f"Expected: 10 records × 80 bytes + 2-byte footer = 802 bytes")

    # Parse header
    footer = struct.unpack('<H', data[800:802])[0]
    print(f"\nFooter: {footer:04X} ({footer})")

    # Parse pilot records
    records = []
    for i in range(10):
        offset = i * 80
        rec_data = data[offset:offset+80]
        rec = parse_pilot_record(rec_data)
        records.append(rec)
        dump_record(rec, i)

    print("\n\n=== Summary ===")
    for i, rec in enumerate(records):
        print(f"{i}: {rec['name']:<30} idx={rec['index']:2d} unknown={rec['unknown_word']}")

if __name__ == '__main__':
    main()
