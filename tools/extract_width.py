#!/usr/bin/env python3
"""
extract_width.py - Generate C arrays for zero-width and double-width Unicode codepoints,
                   with East Asian context (W, F, A → double-width) and Emoji fix
                   (Emoji_Presentation → double-width).

Usage:
    python3 extract_width.py

Output:
    width_property.h
"""

import urllib.request
import re
import sys

# Data source URLs
EAST_ASIAN_WIDTH_URL = "https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt"
GENERAL_CATEGORY_URL = "https://www.unicode.org/Public/UCD/latest/ucd/extracted/DerivedGeneralCategory.txt"
EMOJI_DATA_URL = "https://www.unicode.org/Public/UCD/latest/emoji/emoji-sequences.txt"

def download_file(url):
    """Download file content and return decoded UTF-8 string."""
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        print(f"Failed to download {url}: {e}", file=sys.stderr)
        sys.exit(1)

def parse_east_asian_width(text):
    """Parse EastAsianWidth.txt, extract W, F, A → double-width."""
    intervals = []
    line_re = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*([WFA])\b')
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = line_re.match(line)
        if not m:
            continue
        start = int(m.group(1), 16)
        end = int(m.group(2), 16) if m.group(2) else start
        intervals.append((start, end))
    return intervals

def parse_general_category(text):
    """Parse DerivedGeneralCategory.txt, extract zero-width categories."""
    intervals = []
    target_cats = {'Mn', 'Mc', 'Me', 'Cc', 'Cf'}
    line_re = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*([A-Za-z]+)\b')
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = line_re.match(line)
        if not m:
            continue
        cat = m.group(3)
        if cat not in target_cats:
            continue
        start = int(m.group(1), 16)
        end = int(m.group(2), 16) if m.group(2) else start
        intervals.append((start, end))
    return intervals

def parse_emoji_presentation(text):
    """Parse emoji-data.txt, extract Emoji_Presentation=Yes."""
    intervals = []
    line_re = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*Basic_Emoji\b')
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = line_re.match(line)
        if not m:
            continue
        start = int(m.group(1), 16)
        end = int(m.group(2), 16) if m.group(2) else start
        intervals.append((start, end))
    return intervals

def merge_intervals(intervals):
    """Merge overlapping or adjacent intervals."""
    if not intervals:
        return []
    sorted_intervals = sorted(intervals)
    merged = []
    cur_start, cur_end = sorted_intervals[0]
    for start, end in sorted_intervals[1:]:
        if start <= cur_end + 1:
            if end > cur_end:
                cur_end = end
        else:
            merged.append((cur_start, cur_end))
            cur_start, cur_end = start, end
    merged.append((cur_start, cur_end))
    return merged

def format_c_array(name, intervals, indent=4):
    """Generate C array source from merged intervals."""
    elements = []
    for start, end in intervals:
        elements.append(f"0x{start:04X}")
        elements.append(f"0x{end:04X}")
    lines = []
    lines.append(f"static uint32_t {name}[] = {{")
    per_line = 8
    for i in range(0, len(elements), per_line):
        chunk = elements[i:i+per_line]
        lines.append(" " * indent + ", ".join(chunk) + ",")
    lines.append("};")
    total_codepoints = sum(end - start + 1 for start, end in intervals)
    lines.append(f"// Total intervals: {len(intervals)}, total codepoints: {total_codepoints}")
    return "\n".join(lines)

def main():
    # Download data
    eaw_text = download_file(EAST_ASIAN_WIDTH_URL)
    cat_text = download_file(GENERAL_CATEGORY_URL)
    emoji_text = download_file(EMOJI_DATA_URL)   # Now works

    # Parse
    double_wide_eaw = parse_east_asian_width(eaw_text)
    double_wide_emoji = parse_emoji_presentation(emoji_text)
    zero_wide = parse_general_category(cat_text)

    # Merge double-width intervals
    double_merged = merge_intervals(double_wide_eaw + double_wide_emoji)
    zero_merged = merge_intervals(zero_wide)

    # Write header
    with open('width_property.h', 'w') as f:
        f.write("// Auto-generated file, do NOT modify manually\n")
        f.write("// Data sources:\n")
        f.write(f"//   {EAST_ASIAN_WIDTH_URL}\n")
        f.write(f"//   {GENERAL_CATEGORY_URL}\n")
        f.write(f"//   {EMOJI_DATA_URL}\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define ZERO_WIDTH_LEN (sizeof(zero_width) / sizeof(uint32_t))\n")
        f.write("#define DOUBLE_WIDTH_LEN (sizeof(double_width) / sizeof(uint32_t))\n\n")
        f.write(format_c_array("zero_width", zero_merged))
        f.write("\n\n")
        f.write(format_c_array("double_width", double_merged))
        f.write("\n")

    print("Generated width_property.h successfully.")

if __name__ == "__main__":
    main()
