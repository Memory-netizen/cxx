#!/usr/bin/env python3
"""
extract_xid.py - Extract XID_Start and XID_Continue from Unicode DerivedCoreProperties.txt,
                 merge contiguous codepoint ranges, and generate C header array definitions.
Usage:
    python3 extract_xid.py
Output:
    xid_property.h (written to stdout)
"""

import urllib.request
import re
import sys

URL = "https://www.unicode.org/Public/UCD/latest/ucd/DerivedCoreProperties.txt"

def download_file(url):
    """Download file content and return decoded UTF-8 string"""
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        print(f"Failed to download {url}: {e}", file=sys.stderr)
        sys.exit(1)

def parse_properties(text):
    """
    Parse raw text data, return two range lists
        start_points: list of tuples (codepoint_start, codepoint_end)
        continue_points: same structure as above
    """
    start_points = []
    continue_points = []
    line_re = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*(XID_Start|XID_Continue)\b')
    for line in text.splitlines():
        line = line.strip()
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
    sorted_points = sorted(points)
    merged = []
    cur_start, cur_end = sorted_points[0]
    for start, end in sorted_points[1:]:
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
    per_line = 8
    for i in range(0, len(elements), per_line):
        chunk = elements[i:i+per_line]
        lines.append(" " * indent + ", ".join(chunk) + ",")
    lines.append("};")
    total = sum(end - start + 1 for start, end in intervals)
    lines.append(f"// Total intervals: {len(intervals)}, total codepoints: {total}")
    return "\n".join(lines)

def main():
    text = download_file(URL)
    start_points, continue_points = parse_properties(text)
    start_merged = merge_intervals(start_points)
    continue_merged = merge_intervals(continue_points)

    # Build output lines
    output_lines = []
    output_lines.append("// Auto-generated file, do NOT modify manually")
    output_lines.append(f"// Data source: {URL}")
    output_lines.append("")
    output_lines.append("#include <stdint.h>")
    output_lines.append("")
    output_lines.append("#define XID_START_LEN (sizeof(xid_start) / sizeof(uint32_t))")
    output_lines.append("#define XID_CONTINUE_LEN (sizeof(xid_continue) / sizeof(uint32_t))")
    output_lines.append("")
    output_lines.append(format_c_array("xid_start", start_merged))
    output_lines.append("")
    output_lines.append(format_c_array("xid_continue", continue_merged))

    # Write to stdout
    sys.stdout.write("\n".join(output_lines))

    # Diagnostic message to stderr
    print("Generated xid_property.h successfully.", file=sys.stderr)

if __name__ == "__main__":
    main()
