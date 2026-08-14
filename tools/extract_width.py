#!/usr/bin/env python3
"""
extract_width.py - Generate C arrays for zero-width and double-width Unicode codepoints,
                   with East Asian context (W, F, A → double-width) and Emoji fix
                   (Emoji_Presentation → double-width).

Usage:
    python3 extract_width.py

Output:
    width_property.h (written to stdout)
"""

import urllib.request
import re
import sys
import locale
import os

# Data source URLs
EAST_ASIAN_WIDTH_URL = "https://www.unicode.org/Public/UCD/latest/ucd/EastAsianWidth.txt"
GENERAL_CATEGORY_URL = "https://www.unicode.org/Public/UCD/latest/ucd/extracted/DerivedGeneralCategory.txt"
EMOJI_DATA_URL = "https://www.unicode.org/Public/UCD/latest/emoji/emoji-sequences.txt"

def download_file(url):
    try:
        with urllib.request.urlopen(url) as response:
            return response.read().decode('utf-8')
    except Exception as e:
        print(f"Failed to download {url}: {e}", file=sys.stderr)
        sys.exit(1)

def is_east_asian_locale():
    try:
        locale.setlocale(locale.LC_CTYPE, '')
    except locale.Error:
        pass
    loc = locale.getlocale(locale.LC_CTYPE)
    if loc and loc[0]:
        lang = loc[0]
        if lang.startswith(('zh', 'ja', 'ko')):
            return True
    for var in ('LC_CTYPE', 'LANG'):
        lang = os.environ.get(var, '')
        if lang.startswith(('zh', 'ja', 'ko')):
            return True
    return False

def parse_east_asian_width(text, include_ambiguous):
    intervals = []
    line_re = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*([WFA])\b')
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = line_re.match(line)
        if not m:
            continue
        width = m.group(3)
        if width == 'A' and not include_ambiguous:
            continue
        start = int(m.group(1), 16)
        end = int(m.group(2), 16) if m.group(2) else start
        intervals.append((start, end))
    return intervals

def parse_general_category(text):
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
    east_asian = is_east_asian_locale()
    print(f"Locale detected: {'East Asian' if east_asian else 'Non-East Asian'}", file=sys.stderr)

    eaw_text = download_file(EAST_ASIAN_WIDTH_URL)
    cat_text = download_file(GENERAL_CATEGORY_URL)
    emoji_text = download_file(EMOJI_DATA_URL)

    zero_wide = parse_general_category(cat_text)
    double_wide_eaw = parse_east_asian_width(eaw_text, include_ambiguous=east_asian)
    double_wide_emoji = parse_emoji_presentation(emoji_text)

    double_merged = merge_intervals(double_wide_eaw + double_wide_emoji)
    zero_merged = merge_intervals(zero_wide)

    output_lines = []
    output_lines.append("// Auto-generated file, do NOT modify manually")
    output_lines.append("// Data sources:")
    output_lines.append(f"//   {EAST_ASIAN_WIDTH_URL}")
    output_lines.append(f"//   {GENERAL_CATEGORY_URL}")
    output_lines.append(f"//   {EMOJI_DATA_URL}")
    output_lines.append(f"// Generated for {'East Asian' if east_asian else 'Non-East Asian'} locale")
    output_lines.append("")
    output_lines.append("#include <stdint.h>")
    output_lines.append("")
    output_lines.append("#define ZERO_WIDTH_LEN (sizeof(zero_width) / sizeof(uint32_t))")
    output_lines.append("#define DOUBLE_WIDTH_LEN (sizeof(double_width) / sizeof(uint32_t))")
    output_lines.append("")
    output_lines.append(format_c_array("zero_width", zero_merged))
    output_lines.append("")
    output_lines.append(format_c_array("double_width", double_merged))

    sys.stdout.write("\n".join(output_lines))
    print("Generated width_property.h successfully.", file=sys.stderr)

if __name__ == "__main__":
    main()
