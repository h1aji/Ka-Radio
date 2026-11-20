#!/usr/bin/env python3
"""Minify + gzip web assets and convert them to C arrays for ESP8266 flash.

For every asset it writes a file (style,style1, script, index, logo, favicon) 
containing a C array placed in flash via ICACHE_STORE_ATTR / ICACHE_RODATA_ATTR. 
The source files are never modified.

Requirements:
    pip install rcssmin rjsmin
"""

import argparse
import gzip
import re
import sys
from pathlib import Path

import rcssmin
import rjsmin

BYTES_PER_LINE = 12


def minify_css(data: bytes) -> bytes:
    return rcssmin.cssmin(data.decode("utf-8")).encode("utf-8")


def minify_js(data: bytes) -> bytes:
    return rjsmin.jsmin(data.decode("utf-8")).encode("utf-8")


# ---------------------------------------------------------------------------
# HTML minifier, inlined from the "HTML Minify" section of css-html-js-minify
# (https://github.com/juancarlospaco/css-html-js-minify, GPLv3+/LGPLv3+) so
# generate.py no longer depends on that package being installed.
# ---------------------------------------------------------------------------

def condense_html_whitespace(html):
    """Condense HTML, but be safe first if it have textareas or pre tags."""
    # first space between tags, then empty new lines and in-between.
    is_ok = "<textarea" not in html.lower() and "<pre" not in html.lower()
    html = re.sub(r'>\s+<', '> <', html) if is_ok else html
    return re.sub(r'\s{2,}|[\r\n]', ' ', html) if is_ok else html.strip()


def condense_style(html):
    """Condense style html tags."""
    return html.replace('<style type="text/css">', '<style>').replace(
        "<style type='text/css'>", '<style>').replace(
            "<style type=text/css>", '<style>')


def condense_script(html):
    """Condense script html tags."""
    return html.replace('<script type="text/javascript">', '<script>').replace(
        "<style type='text/javascript'>", '<script>').replace(
            "<style type=text/javascript>", '<script>')


def clean_unneeded_html_tags(html):
    """Clean unneeded optional html tags."""
    for tag_to_remove in ("""</area> </base> <body> </body> </br> </col>
        </colgroup> </dd> </dt> <head> </head> </hr> <html> </html> </img>
        </input> </li> </link> </meta> </option> </param>
        </td> </tfoot> </th> </thead> </tr> </basefont> </isindex> </param>
        """.split()):
            html = html.replace(tag_to_remove, '')
    return html


def remove_html_comments(html):
    """Remove all HTML comments, Keep all for Grunt, Grymt and IE."""
    # Grunt uses comments to as build arguments, bad practice but still.
    return re.compile('<!-- [^(build|endbuild)].*? -->', re.I).sub('', html)


def unquote_html_attributes(html):
    """Remove all HTML quotes on attibutes if possible."""
    # data-foo=0> might cause errors on IE, we leave 1 space data-foo=0 >
    # cache all regular expressions on variables before we enter the for loop.
    any_tag = re.compile(r"<\w.*?>", re.I | re.MULTILINE | re.DOTALL)
    space = re.compile(r' \s+|\s +', re.MULTILINE)
    space1 = re.compile(r'\w\s+\w', re.MULTILINE)
    space2 = re.compile(r'"\s+>', re.MULTILINE)
    space3 = re.compile(r"'\s+>", re.MULTILINE)
    space4 = re.compile(r'"\s\s+\w+="|\'\s\s+\w+=\'|"\s\s+\w+=|\'\s\s+\w+=', re.MULTILINE)
    space6 = re.compile(r"\d\s+>", re.MULTILINE)
    quotes_in_tag = re.compile(r'([a-zA-Z]+)="([a-zA-Z0-9-_\.]+)"')
    # iterate on a for loop cleaning stuff up on the html markup.
    for tag in iter(any_tag.findall(html)):
        # exceptions of comments and closing tags
        if tag.startswith('<!') or tag.find('</') > -1:
            continue
        original = tag
        # remove white space inside the tag itself
        tag = space2.sub('" >', tag)  # preserve 1 white space is safer
        tag = space3.sub("' >", tag)
        for each in space1.findall(tag) + space6.findall(tag):
            tag = tag.replace(each, space.sub(' ', each))
        for each in space4.findall(tag):
            tag = tag.replace(each, each[0] + ' ' + each[1:].lstrip())
        # remove quotes on some attributes
        tag = quotes_in_tag.sub(r'\1=\2', tag)
        if original != tag:  # has the tag been improved ?
            html = html.replace(original, tag)
    return html.strip()


def html_minify_text(html, comments=False):
    """Minify HTML main function."""
    html = remove_html_comments(html) if not comments else html
    html = condense_style(html)
    html = condense_script(html)
    html = clean_unneeded_html_tags(html)
    html = condense_html_whitespace(html)
    html = unquote_html_attributes(html)
    return html.strip()


def minify_html(data: bytes) -> bytes:
    return html_minify_text(data.decode("utf-8")).encode("utf-8")


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
