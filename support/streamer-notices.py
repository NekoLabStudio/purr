#!/usr/bin/env python3
"""Writes the licence notices for the VR streamer: every crate compiled into it for one target,
with its licence and the licence and notice files it ships. Releases put the file beside the
streamer, so games that ship it carry its notices.

    support/streamer-notices.py <target triple> <output file>
"""

import json
import pathlib
import subprocess
import sys

MANIFEST = pathlib.Path(__file__).resolve().parent.parent / "modules/vr_streamer/streamer/Cargo.toml"
LICENSE_FILES = ("license", "licence", "copying", "notice", "unlicense", "copyright", "authors")


def licence_texts(manifest_path: str) -> list[tuple[str, str]]:
    texts = []
    for path in sorted(pathlib.Path(manifest_path).parent.iterdir()):
        if path.is_file() and path.name.lower().startswith(LICENSE_FILES):
            texts.append((path.name, path.read_text(encoding="utf-8", errors="replace").strip()))
    return texts


def spdx_links(expression: str) -> list[str]:
    """Where the licences a crate names are written out, for crates that don't include the text."""
    words = expression.replace("(", " ").replace(")", " ").replace("/", " OR ").split()
    return [f"https://spdx.org/licenses/{word}.html" for word in words if word not in ("OR", "AND", "WITH")]


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    target, output = sys.argv[1], pathlib.Path(sys.argv[2])
    metadata = json.loads(
        subprocess.run(
            ["cargo", "metadata", "--format-version", "1", "--locked", "--filter-platform", target, "--manifest-path", str(MANIFEST)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    )
    packages = {package["id"]: package for package in metadata["packages"]}
    nodes = {node["id"]: node for node in metadata["resolve"]["nodes"]}

    # What ends up in the binary: normal dependencies, not build scripts' or procedural macros'.
    shipped = set()
    stack = [metadata["resolve"]["root"]]
    while stack:
        package_id = stack.pop()
        if package_id in shipped:
            continue
        shipped.add(package_id)
        for dependency in nodes[package_id]["deps"]:
            normal = any(kind["kind"] is None for kind in dependency["dep_kinds"])
            macro = any("proc-macro" in target_["kind"] for target_ in packages[dependency["pkg"]]["targets"])
            if normal and not macro:
                stack.append(dependency["pkg"])
    shipped.discard(metadata["resolve"]["root"])

    lines = [
        "The VR streamer is built from these open source crates. Their licences and notices follow.",
        "",
    ]
    missing = []
    for package in sorted((packages[i] for i in shipped), key=lambda p: (p["name"], p["version"])):
        lines.append("=" * 78)
        lines.append(f"{package['name']} {package['version']}")
        lines.append(f"Licence: {package.get('license') or 'see below'}")
        if package.get("repository"):
            lines.append(f"Source: {package['repository']}")
        texts = licence_texts(package["manifest_path"])
        if not texts:
            missing.append(package["name"])
            if package.get("license"):
                lines.append("Licence text: " + ", ".join(spdx_links(package["license"])))
        for name, text in texts:
            lines += ["", f"--- {name} ---", "", text]
        lines.append("")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if missing:
        # Named above with a link to the text: these crates don't include the file.
        print(f"{len(missing)} of {len(shipped)} crates carry no licence file: {', '.join(sorted(set(missing)))}", file=sys.stderr)


if __name__ == "__main__":
    main()
