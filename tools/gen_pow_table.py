#!/usr/bin/env python3
"""
gen_pow_table.py - Generate C lookup tables for powers of 10 and powers of 2.

This script generates a C header file (pow_table.h) containing two static
double-precision arrays:
  - pow10_table[] : 10^e for e = -324, -323, ..., 308 (633 entries)
  - pow2_table[]  : 2^p  for p = -1074, -1073, ..., 1023 (2098 entries)

These tables are intended for fast and accurate decimal‑to‑binary floating‑point
conversions (e.g., in number parsers or formatters).  Each table entry is
accompanied by its exact hexadecimal representation (produced via struct.pack)
to facilitate debugging and verification against the IEEE‑754 specification.

Usage:
    python3 gen_pow_table.py

Output:
    pow_table.h
"""

import struct

def double_to_hex(f):
    """Return the hex representation of the double-precision value f as a C literal."""
    return '0x' + struct.pack('d', f).hex()

with open('pow_table.h', 'w') as f:
    f.write('static const double pow10_table[] = {\n')
    f.write(f' // 1e-324, \t\t   {double_to_hex(10.0 ** -324)}\n')
    for e in range(-323, 309):
        val = 10.0 ** e
        f.write(f'    1e{e}, \t\t// {double_to_hex(val)}\n')
    f.write('};\n')
    
    f.write('\n')
    f.write('static const double pow2_table[] = {\n')
    f.write(f' // 0x1.0p-1075, \t\t   {double_to_hex(2.0 ** -1075)}\n')
    for p in range(-1074, 1024):
        val = 2.0 ** p
        f.write(f'    0x1.0p{p}, \t\t// {double_to_hex(val)}\n')
    f.write('};\n')
    f.write('\n')

    print("Generated pow_table.h successfully.")
