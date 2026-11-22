#!/usr/bin/env python3
"""Verify exact edge revisions before a manually authorized stable promotion."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import urllib.request


class ChannelNotReady(RuntimeError):
    """A channel pointer has not reached the requested revision yet."""


def verify(archive, download, expected, arch):
    with archive.open("rb") as stream:
        if hashlib.file_digest(stream, "sha3_384").hexdigest() != download["sha3-384"]:
            raise ValueError(f"{arch}: Store archive checksum mismatch")
    metadata = subprocess.check_output(
        ["unsquashfs", "-cat", str(archive), "usr/share/doc/wordforge/build.txt"], timeout=60
    ).decode()
    for line in (f"Source commit: {expected}", f"Architecture: {arch}", "Uncommitted changes: False"):
        if line not in metadata.splitlines():
            raise ValueError(f"{arch}: unexpected build identity: {line}")
    digest = hashlib.sha256()
    with subprocess.Popen(["unsquashfs", "-cat", str(archive), "usr/bin/wordforge"],
                          stdout=subprocess.PIPE) as process:
        while chunk := process.stdout.read(1024 * 1024):
            digest.update(chunk)
        if process.wait(timeout=60) != 0:
            raise ValueError(f"{arch}: cannot read packaged executable")
    if f"Executable SHA256: {digest.hexdigest()}" not in metadata.splitlines():
        raise ValueError(f"{arch}: executable checksum mismatch")
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--amd64", required=True, type=int)
    parser.add_argument("--arm64", required=True, type=int)
    parser.add_argument("--channel", choices=["edge", "stable"], default="edge")
    parser.add_argument("--output", type=Path, default=Path("out/snap-store-verification"))
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.source) or min(args.amd64, args.arm64) <= 0:
        parser.error("Require a full source SHA and positive revisions")
    request = urllib.request.Request("https://api.snapcraft.io/v2/snaps/info/wordforge",
                                     headers={"Snap-Device-Series": "16"})
    with urllib.request.urlopen(request, timeout=30) as response:
        metadata = json.load(response)
    entries = {entry["channel"]["architecture"]: entry for entry in metadata["channel-map"]
               if entry["channel"]["name"] == args.channel}
    expected = {"amd64": args.amd64, "arm64": args.arm64}
    # Validate both channel pointers before downloading or authorizing either promotion.
    for arch, revision in expected.items():
        if arch not in entries or entries[arch]["revision"] != revision:
            raise ChannelNotReady(f"{arch}: {args.channel} does not yet point to revision {revision}")
    args.output.mkdir(parents=True, exist_ok=True)
    verified = {}
    for arch, revision in expected.items():
        entry = entries[arch]
        archive = args.output / f"wordforge-{arch}-{revision}.snap"
        if not archive.exists():
            with urllib.request.urlopen(entry["download"]["url"], timeout=90) as response, archive.open("wb") as stream:
                while chunk := response.read(1024 * 1024):
                    stream.write(chunk)
        executable = verify(archive, entry["download"], args.source, arch)
        verified[arch] = {"revision": revision, "source": args.source, "channel": args.channel,
                          "version": entry["version"], "executable_sha256": executable}
        print(f"Verified {arch} revision {revision}, source {args.source}", flush=True)
    (args.output / "verified.json").write_text(json.dumps(verified, indent=2) + "\n")


if __name__ == "__main__":
    try:
        main()
    except ChannelNotReady as error:
        print(error, file=sys.stderr)
        sys.exit(3)
