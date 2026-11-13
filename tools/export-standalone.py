#!/usr/bin/env python3
"""Export wordforge/ as a new standalone repository, staged for one initial commit.

The export carries no history from the enclosing repository, so earlier commits
(and anything they once contained) are not published. It does not create or
push to any remote; do that yourself after reviewing the output.
"""
import argparse
import io
from pathlib import Path
import subprocess
import tarfile


def git(*args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True).stdout


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True, help="new, empty directory")
parser.add_argument("--allow-dirty", action="store_true")
args = parser.parse_args()

project = Path(__file__).resolve().parents[1]
root = Path(git("rev-parse", "--show-toplevel", cwd=project).decode().strip())
prefix = project.relative_to(root).as_posix()
if git("status", "--porcelain", "--", ".", cwd=project) and not args.allow_dirty:
    parser.error("wordforge/ has uncommitted changes; commit them or pass --allow-dirty")
if args.output.exists() and any(args.output.iterdir()):
    parser.error(f"{args.output} is not empty")
args.output.mkdir(parents=True, exist_ok=True)

source = git("rev-parse", "HEAD", cwd=root).decode().strip()
# Committed files only: build output and ignored files never leave the checkout.
archive = git("archive", "--format=tar", f"HEAD:{prefix}", cwd=root)
with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
    tar.extractall(args.output, filter="data")

git("init", "--quiet", "--initial-branch=main", cwd=args.output)
git("add", "--all", cwd=args.output)
count = len(git("ls-files", cwd=args.output).splitlines())
# The first commit is left to you: its author name and email become public.
print(f"Staged {count} files in {args.output.resolve()} (source {source[:12]}).\n"
      "Review them, set the public identity you want with git config user.name /\n"
      "user.email inside that repository, then commit, for example:\n"
      f'  git commit -m "Wordforge 0.6 developer preview" -m "Exported from {prefix}/ at {source[:12]}."')
