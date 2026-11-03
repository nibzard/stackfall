#!/usr/bin/env python3
"""Print the packet image ids for one review round, in manifest order.

Usage:
  review_ids.py --packet PACKET_DIR [--only id1,id2,...] [--max N]

Exits nonzero with a message when a selected id is unknown or the round
would exceed the image bound.
"""

import argparse
import json
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packet", required=True, type=Path)
    parser.add_argument("--only", default="")
    parser.add_argument("--max", type=int, default=12)
    args = parser.parse_args()

    manifest = json.loads((args.packet / "manifest.json").read_text())
    ids = [entry["id"] for entry in manifest["images"]]
    if args.only:
        wanted = [part for part in args.only.split(",") if part]
        unknown = [image_id for image_id in wanted if image_id not in ids]
        if unknown:
            print(f"review_ids: unknown image ids: {', '.join(unknown)}",
                  file=sys.stderr)
            return 1
        ids = wanted
    if len(ids) > args.max:
        print(f"review_ids: {len(ids)} images exceed the round bound of "
              f"{args.max}", file=sys.stderr)
        return 1
    for image_id in ids:
        print(image_id)
    return 0


if __name__ == "__main__":
    sys.exit(main())
