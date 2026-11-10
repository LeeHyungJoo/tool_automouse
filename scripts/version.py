#!/usr/bin/env python3
"""Check or increment the checked-in Windows app metadata from VERSION."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
VERSION_PATTERN = re.compile(r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})")
ASSEMBLY_NAMESPACE = "{urn:schemas-microsoft-com:asm.v1}"
APP_NAME = "MfcAutoClicker.App"
MAX_COMPONENT = 65535


def parse_version(text: str) -> tuple[int, int, int]:
    match = VERSION_PATTERN.fullmatch(text)
    if not match:
        raise ValueError("version must be MAJOR.MINOR.PATCH without leading zeros or suffixes")
    parts = tuple(int(part) for part in match.groups())
    if any(part > MAX_COMPONENT for part in parts):
        raise ValueError("each version component must be between 0 and 65535")
    return parts


def version_string(parts: tuple[int, int, int]) -> str:
    return ".".join(str(part) for part in parts)


def read_version() -> tuple[int, int, int]:
    text = (ROOT / "VERSION").read_text(encoding="utf-8")
    # Accept the conventional single final newline, including Windows CRLF.
    if text.endswith("\n"):
        text = text[:-1]
    return parse_version(text)


def render_header(parts: tuple[int, int, int]) -> str:
    numeric = ",".join(str(part) for part in (*parts, 0))
    return (
        "#pragma once\n\n"
        "// Generated from VERSION by scripts/version.py. Do not edit by hand.\n"
        f"#define AUTOCLICKER_VERSION_COMMA {numeric}\n"
        f'#define AUTOCLICKER_VERSION_STRING "{version_string(parts)}"\n'
    )


def app_manifest_identity(text: str) -> ET.Element:
    root = ET.fromstring(text)
    if root.tag != ASSEMBLY_NAMESPACE + "assembly":
        raise ValueError("AutoClicker.manifest must contain a Windows assembly root")
    identities = root.findall(ASSEMBLY_NAMESPACE + "assemblyIdentity")
    if len(identities) != 1 or identities[0].get("name") != APP_NAME:
        raise ValueError(f"AutoClicker.manifest must have one root identity named {APP_NAME}")
    return identities[0]


def check_resource_linkage(text: str) -> None:
    if len(re.findall(r'^\s*#include\s+"Version\.h"\s*$', text, re.MULTILINE)) != 1:
        raise ValueError("AutoClicker.rc must include Version.h exactly once")
    directives = {
        "FILEVERSION": "AUTOCLICKER_VERSION_COMMA",
        "PRODUCTVERSION": "AUTOCLICKER_VERSION_COMMA",
        'VALUE "FileVersion",': "AUTOCLICKER_VERSION_STRING",
        'VALUE "ProductVersion",': "AUTOCLICKER_VERSION_STRING",
    }
    for directive, expected in directives.items():
        # Permit whitespace changes while rejecting literals and duplicates.
        pattern = r"^\s*" + r"\s*".join(re.escape(word) for word in directive.split())
        values = re.findall(pattern + r"\s+([^\r\n]+?)\s*$", text, re.MULTILINE)
        if values != [expected]:
            raise ValueError(f"AutoClicker.rc {directive} must use {expected}")


def check_metadata(parts: tuple[int, int, int]) -> str:
    app = ROOT / "AutoClicker"
    if (app / "Version.h").read_text(encoding="utf-8") != render_header(parts):
        raise ValueError("AutoClicker/Version.h differs from VERSION")
    manifest = (app / "AutoClicker.manifest").read_text(encoding="utf-8")
    expected = version_string(parts) + ".0"
    if app_manifest_identity(manifest).get("version") != expected:
        raise ValueError(f"AutoClicker.manifest app version must be {expected}")
    check_resource_linkage((app / "AutoClicker.rc").read_text(encoding="utf-8"))
    return manifest


def render_manifest(text: str, parts: tuple[int, int, int]) -> str:
    matches = [
        match for match in re.finditer(r"<(?:[A-Za-z_][\w.-]*:)?assemblyIdentity\b[^>]*>", text)
        if re.search(r'\bname\s*=\s*([\"\'])' + re.escape(APP_NAME) + r"\1", match.group())
    ]
    if len(matches) != 1:
        raise ValueError("cannot locate a unique application assemblyIdentity in manifest")
    match = matches[0]
    new_version = version_string(parts) + ".0"
    identity, count = re.subn(
        r'\bversion\s*=\s*([\"\'])[^\"\']*\1',
        lambda found: f"version={found.group(1)}{new_version}{found.group(1)}",
        match.group(),
    )
    if count != 1:
        raise ValueError("application assemblyIdentity must have exactly one version attribute")
    result = text[:match.start()] + identity + text[match.end():]
    if app_manifest_identity(result).get("version") != new_version:
        raise ValueError("failed to update application manifest version")
    return result


def bump_version(parts: tuple[int, int, int], component: str) -> tuple[int, int, int]:
    index = ("major", "minor", "patch").index(component)
    if parts[index] == MAX_COMPONENT:
        raise ValueError(f"cannot increment {component} beyond {MAX_COMPONENT}")
    updated = list(parts)
    updated[index] += 1
    for lower in range(index + 1, 3):
        updated[lower] = 0
    return tuple(updated)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true", help="validate VERSION and Windows metadata")
    action.add_argument("--bump", choices=("major", "minor", "patch"), help="increment one component and reset lower components")
    parser.add_argument("--tag", help="with --check, require the exact Git tag vMAJOR.MINOR.PATCH")
    args = parser.parse_args()
    if args.tag is not None and not args.check:
        parser.error("--tag is only valid with --check")

    try:
        parts = read_version()
        manifest = check_metadata(parts)
        if args.tag is not None:
            if not args.tag.startswith("v"):
                raise ValueError("tag must start with v, for example v1.0.0")
            tag_parts = parse_version(args.tag[1:])
            if tag_parts != parts:
                raise ValueError(f"tag {args.tag} does not match VERSION {version_string(parts)}")
        if args.bump:
            parts = bump_version(parts, args.bump)
            # Finish all validation and rendering before changing any file.
            generated = {
                ROOT / "VERSION": version_string(parts) + "\n",
                ROOT / "AutoClicker" / "Version.h": render_header(parts),
                ROOT / "AutoClicker" / "AutoClicker.manifest": render_manifest(manifest, parts),
            }
            for path, content in generated.items():
                with path.open("w", encoding="utf-8", newline="\n") as stream:
                    stream.write(content)
        print(version_string(parts))
        return 0
    except (OSError, ValueError, ET.ParseError) as error:
        print(f"version: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
