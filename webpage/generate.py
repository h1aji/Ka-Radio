#!/usr/bin/env python3
"""Minify + gzip web assets and convert them to C arrays for ESP8266 flash.

Python 3 port of generate.sh. For every asset it writes a file (style,
style1, script, index, logo, favicon) containing a C array placed in flash
via ICACHE_STORE_ATTR / ICACHE_RODATA_ATTR. The source files are never
modified.

Requirements:
    pip install rcssmin rjsmin css-html-js-minify
"""

import argparse
import gzip
import re
import sys
from pathlib import Path

import rcssmin
import rjsmin
from css_html_js_minify import html_minify

BYTES_PER_LINE = 12  # same as `xxd -i`


def minify_css(data: bytes) -> bytes:
    return rcssmin.cssmin(data.decode("utf-8")).encode("utf-8")


def minify_js(data: bytes) -> bytes:
    return rjsmin.jsmin(data.decode("utf-8")).encode("utf-8")


def minify_html(data: bytes) -> bytes:
    return html_minify(data.decode("utf-8")).encode("utf-8")


def keep(data: bytes) -> bytes:
    return data


# (source file, minifier). Output file name = source name without extension.
ASSETS = [
    ("style.css", minify_css),
    ("style1.css", minify_css),
    ("script.js", minify_js),
    ("index.html", minify_html),
    ("logo.png", keep),
    ("favicon.png", keep),
]


def to_c_array(source_name: str, data: bytes) -> str:
    """Equivalent of `xxd -i` plus the sed edits from the shell script."""
    var = re.sub(r"\W", "_", source_name)  # style.css -> style_css
    lines = []
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = data[i:i + BYTES_PER_LINE]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk))
    body = ",\n".join(lines)
    return (
        f"const char {var}[] ICACHE_STORE_ATTR ICACHE_RODATA_ATTR = {{\n"
        f"{body}\n"
        f"}};\n"
        f"const int {var}_len = {len(data)};\n"
    )


def process(directory: Path, name: str, minifier) -> None:
    src = directory / name
    raw = src.read_bytes()
    minified = minifier(raw)
    # mtime=0 makes the output reproducible (no timestamp in the gzip header)
    packed = gzip.compress(minified, compresslevel=9, mtime=0)
    out = directory / src.stem
    out.write_text(to_c_array(name, packed), encoding="ascii")
    print(f"{name}: {len(raw)} -> {len(minified)} (minified) -> {len(packed)} (gzip)  => {out.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", nargs="?", default=".",
                        help="folder containing the web files (default: current dir)")
    args = parser.parse_args()
    directory = Path(args.directory)

    failed = False
    for name, minifier in ASSETS:
        try:
            process(directory, name, minifier)
        except FileNotFoundError:
            print(f"{name}: not found, skipped", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
