#!/usr/bin/env python3
"""
extract_xid.py - Extract XID_Start and XID_Continue from Unicode DerivedCoreProperties.txt,
                 merge contiguous codepoint ranges, and generate C header array definitions.
Usage:
    python3 extract_xid.py
Output:
    xid_property.h header file
"""

import urllib.request
import re
import sys
from collections import defaultdict

URL = "https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt"

def download_file(url):
    """Download file content and return decoded UTF-8 string"""
    with urllib.request.urlopen(url) as response:
        return response.read().decode('utf-8')

def parse_properties(text):
    """
    Parse raw text data, return two range lists
        start_points: list of tuples (codepoint_start, codepoint_end)
        continue_points: same structure as above
    """
    start_points = []
    continue_points = []
    # Match data lines, such as:
    # 005F          ; XID_Continue # Pc       LOW LINE
    # 0030..0039    ; XID_Continue # Nd  [10] DIGIT ZERO..DIGIT NINE
    line_re = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*(XID_Start|XID_Continue)\b')
    for line in text.splitlines():
        line = line.strip()
        # Skip blank lines and comment lines
        if not line or line.startswith('#'):
            continue
        m = line_re.match(line)
        if not m:
            continue
        start_hex = m.group(1)
        end_hex = m.group(2)
        prop = m.group(3)
        start = int(start_hex, 16)
        end = int(end_hex, 16) if end_hex else start
        if prop == 'XID_Start':
            start_points.append((start, end))
        else:
            continue_points.append((start, end))
    return start_points, continue_points

def merge_intervals(points):
    """
    Merge overlapping or adjacent codepoint intervals
    Input: Unsorted list of (start, end) closed intervals
    Output: Sorted, deduplicated merged interval list
    """
    if not points:
        return []
    # Sort intervals by starting codepoint
    sorted_points = sorted(points)
    merged = []
    cur_start, cur_end = sorted_points[0]
    for start, end in sorted_points[1:]:
        # Merge when ranges overlap or connect directly
        if start <= cur_end + 1:
            if end > cur_end:
                cur_end = end
        else:
            merged.append((cur_start, cur_end))
            cur_start, cur_end = start, end
    merged.append((cur_start, cur_end))
    return merged

def format_c_array(name, intervals, indent=4):
    """Format merged interval list into standard C array source code"""
    lines = []
    lines.append(f"static uint32_t {name}[] = {{")
    elements = []
    for start, end in intervals:
        elements.append(f"0x{start:04X}")
        elements.append(f"0x{end:04X}")
    # Wrap 8 values per line
    per_line = 8
    for i in range(0, len(elements), per_line):
        chunk = elements[i:i+per_line]
        lines.append(" " * indent + ", ".join(chunk) + ",")
    lines.append("};")
    lines.append(f"// Total intervals: {len(intervals)}, total codepoints: {sum(end-start+1 for start,end in intervals)}")
    return "\n".join(lines)

def main():
    try:
        text = download_file(URL)
    except Exception as e:
        print(f"Failed to download source file: {e}", file=sys.stderr)
        sys.exit(1)

    start_points, continue_points = parse_properties(text)
    start_merged = merge_intervals(start_points)
    continue_merged = merge_intervals(continue_points)

    # Write generated header
    with open('xid_property.h', 'w') as f:
        f.write("// Auto-generated file, do NOT modify manually\n")
        f.write(f"// Data source: {URL}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define XID_START_LEN (sizeof(xid_start) / sizeof(uint32_t))\n")
        f.write("#define XID_CONTINUE_LEN (sizeof(xid_continue) / sizeof(uint32_t))\n\n")
        f.write(format_c_array("xid_start", start_merged))
        f.write('\n\n')
        f.write(format_c_array("xid_continue", continue_merged))
        f.write('\n')
    
    print("Generated xid_property.h successfully.")

if __name__ == "__main__":
    main()
