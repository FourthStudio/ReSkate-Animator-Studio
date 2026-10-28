"""Writes launcher.json for an Animator Studio release, so the launcher updates from our releases.

    python contrib/make_launcher_json.py --launcher build/.../ReSkateLauncher.exe --dll build/.../ReSkate.dll
        --version 1.0.11 --official v2.1.2 --output launcher.json

Attach launcher.json, ReSkateLauncher.exe and ReSkate.dll to the GitHub release. The game and
DepotDownloader pins are copied from the official ReSkate release the build was made from
(--official), so both choices in the launcher ask for the same skate. build. The dedicated server
is left out: servers keep updating from official ReSkate.
"""

import argparse
import hashlib
import json
import urllib.request
from pathlib import Path

OFFICIAL_REPO = "Dingo-Shenanigans/ReSkate"


def pinned(path, version, asset):
    data = Path(path).read_bytes()
    return {"version": version, "url": f"asset:{asset}", "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


def official_config(tag, local):
    if local:
        return json.loads(Path(local).read_text(encoding="utf-8-sig"))
    url = f"https://github.com/{OFFICIAL_REPO}/releases/download/{tag}/launcher.json"
    with urllib.request.urlopen(url, timeout=30) as answer:
        return json.loads(answer.read().decode("utf-8-sig"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--launcher", required=True)
    parser.add_argument("--dll", required=True)
    parser.add_argument("--version", required=True, help="the Animator Studio version, as 1.0.11")
    parser.add_argument("--official", required=True, help="the official ReSkate release tag the build was made from, as v2.1.2")
    parser.add_argument("--official-config", help="that release's launcher.json on disk, instead of downloading it")
    parser.add_argument("--output", default="launcher.json")
    args = parser.parse_args()

    base = official_config(args.official, args.official_config)
    official_version = base["runtime"]["version"]
    version = f"{args.version}+reskate.{official_version}"
    config = {
        "schema": 1,
        "launcher": pinned(args.launcher, version, "ReSkateLauncher.exe"),
        "runtime": pinned(args.dll, version, "ReSkate.dll"),
        "game": base["game"],
        "depot_downloader": base["depot_downloader"],
    }
    Path(args.output).write_text(json.dumps(config, indent=4) + "\n", encoding="utf-8")
    print(f"{args.output}: Animator Studio {version}, game build {base['game'].get('build_id', '?')}")


if __name__ == "__main__":
    main()
