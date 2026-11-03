#!/usr/bin/env python3
"""Build the design-review packet from captured checkpoints.

Reads tools/design-review/packet.tsv, converts the listed checkpoint BMPs
to PNG under <run>/packet/images/, pairs every image with its structured
observation record, and writes <run>/packet/manifest.json plus a
human-readable manifest.md. The BMPs stay where the sessions wrote them;
the packet only adds the reviewed PNG copies (docs/DESIGN_REVIEW.md,
"Preserve the original screenshots").

Usage:
  make_packet.py --repo REPO --run-dir RUN_DIR [--round initial]
"""

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    print("make_packet: the Python Imaging Library is required", file=sys.stderr)
    raise

# Observation fields the manifest copies verbatim; board= and colors= are
# kept only in the packet's raw record copies, because their bulk hides the
# fields a design review reads.
RECORD_FIELDS = [
    "scene", "tick", "score", "lines", "level", "high", "record",
    "active", "ghost", "hold", "queue", "menu", "settled",
]


def parse_packet_table(path):
    """Return (sessions, images) from the pipe-separated packet table."""
    sessions = []
    images = []
    for line in path.read_text().splitlines():
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        parts = text.split("|")
        if parts[0] == "session" and len(parts) == 8:
            sessions.append({
                "name": parts[1],
                "seed": int(parts[2]),
                "video": parts[3],
                "window": parts[4],
                "script": parts[5],
                "fixtures": [] if parts[6] == "-" else parts[6].split(","),
                "label": parts[7],
            })
        elif parts[0] == "image" and len(parts) == 7:
            images.append({
                "session": parts[1],
                "checkpoint": parts[2],
                "id": parts[3],
                "scene": parts[4],
                "covers": parts[5],
                "expected": parts[6],
            })
        else:
            raise ValueError(f"bad packet table row: {text}")
    return sessions, images


def parse_records(path):
    """Map check name to its observation record fields."""
    records = {}
    if not path.exists():
        return records
    for line in path.read_text().splitlines():
        fields = {}
        for token in line.split():
            if "=" in token:
                key, value = token.split("=", 1)
                fields[key] = value
        if "check" in fields:
            records[fields["check"]] = {"line": line, "fields": fields}
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--run-dir", required=True, type=Path)
    parser.add_argument("--round", default="initial",
                        help="review round label recorded in the manifest")
    parser.add_argument("--commit", default="",
                        help="commit identifier of the reviewed sources")
    parser.add_argument("--max-images", type=int, default=12,
                        help="round bound on reviewed images")
    args = parser.parse_args()

    table = args.repo / "tools/design-review/packet.tsv"
    sessions, images = parse_packet_table(table)
    if len(images) > args.max_images:
        print(f"make_packet: {len(images)} images exceed the round bound "
              f"of {args.max_images}", file=sys.stderr)
        return 1
    by_name = {session["name"]: session for session in sessions}

    packet = args.run_dir / "packet"
    image_dir = packet / "images"
    image_dir.mkdir(parents=True, exist_ok=True)

    source_identity = args.run_dir / "source.sha256"
    entries = []
    for image in images:
        session = by_name[image["session"]]
        bmp = args.run_dir / session["name"] / "shots" / f"{image['checkpoint']}.bmp"
        if not bmp.exists():
            print(f"make_packet: missing checkpoint {bmp}", file=sys.stderr)
            return 1
        records = parse_records(args.run_dir / f"{session['name']}.obs")
        record = records.get(image["checkpoint"])
        if record is None:
            print(f"make_packet: no observation record for "
                  f"{image['checkpoint']} in {session['name']}.obs",
                  file=sys.stderr)
            return 1
        scene = record["fields"].get("scene")
        if scene != image["scene"]:
            print(f"make_packet: {image['id']} records scene={scene}, "
                  f"expected {image['scene']}", file=sys.stderr)
            return 1
        width, height = (int(part) for part in session["window"].split("x"))
        with Image.open(bmp) as frame:
            if frame.size != (width, height):
                print(f"make_packet: {bmp} is {frame.size[0]}x{frame.size[1]}, "
                      f"expected {width}x{height}", file=sys.stderr)
                return 1
            png = image_dir / f"{image['id']}.png"
            frame.save(png)
        entries.append({
            "id": image["id"],
            "file": f"images/{image['id']}.png",
            "width": width,
            "height": height,
            "window": session["window"],
            "session": session["name"],
            "seed": session["seed"],
            "checkpoint": image["checkpoint"],
            "scene": scene,
            "covers": image["covers"],
            "expected": image["expected"],
            "observation": {key: record["fields"].get(key)
                            for key in RECORD_FIELDS},
            "record_line": record["line"],
            "source_bmp": f"{session['name']}/shots/{image['checkpoint']}.bmp",
        })

    manifest = {
        "round": args.round,
        "date": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "image_count": len(entries),
        "packet_table": "tools/design-review/packet.tsv",
        "source_identity": "source.sha256",
        "commit": args.commit,
        "sessions": sessions,
        "images": entries,
    }
    if source_identity.exists():
        for line in source_identity.read_text().splitlines():
            if line.startswith("date:"):
                manifest["capture_date"] = line.split(":", 1)[1].strip()
                break

    (packet / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    lines = [
        "# Design-review packet manifest",
        "",
        f"Round: {manifest['round']}. Date: {manifest['date']}.",
        f"Images: {len(entries)}. Commit: {args.commit or 'none recorded'}.",
        f"Source identity: {source_identity.name} next to this packet.",
        "",
        "| id | session | seed | scene | tick | window | covers |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for entry in entries:
        lines.append(
            f"| {entry['id']} | {entry['session']} | {entry['seed']} | "
            f"{entry['scene']} | {entry['observation']['tick']} | "
            f"{entry['window']} | {entry['covers']} |")
    lines += ["", "## Frames", ""]
    for entry in entries:
        obs = entry["observation"]
        lines += [
            f"### {entry['id']}",
            "",
            f"- Covers: {entry['covers']}.",
            f"- Session {entry['session']}, seed {entry['seed']}, "
            f"checkpoint {entry['checkpoint']}, scene {entry['scene']}, "
            f"tick {obs['tick']}, window {entry['window']}.",
            f"- Record: active={obs['active']} ghost={obs['ghost']} "
            f"hold={obs['hold']} queue={obs['queue']} score={obs['score']} "
            f"lines={obs['lines']} level={obs['level']} menu={obs['menu']}.",
            f"- Expected: {entry['expected']}.",
            f"- File: {entry['file']} (from {entry['source_bmp']}).",
            "",
        ]
    (packet / "manifest.md").write_text("\n".join(lines))

    print(f"make_packet: {len(entries)} images in {packet}")
    for entry in entries:
        print(f"  {entry['id']:22s} {entry['window']:9s} "
              f"{entry['session']:10s} tick={entry['observation']['tick']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
