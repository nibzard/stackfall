#!/usr/bin/env python3
"""Validate the codex review reports and merge their findings.

Usage:
  check_review.py --packet PACKET_DIR --review-dir REVIEW_DIR

Every call-NN.review.json must parse, match the packet image ids, list
exactly the images attached to its call, and fill every finding field
with the allowed values. A report that fails validation is recorded and
fails the run; a failed reviewer cannot count as a successful review
(docs/DESIGN_REVIEW.md). Findings merge across calls by key, so the same
root cause reported twice leaves one merged finding.
"""

import argparse
import json
import re
import sys
from pathlib import Path

FINDING_FIELDS = [
    "key", "title", "priority", "image_ids", "region", "observed_problem",
    "player_effect", "confidence", "evidence_type", "proposed_improvement",
    "scope", "reproduction_steps", "acceptance_criteria", "affected_files",
]
TOP_FIELDS = ["inspected_images", "positive_observations", "findings",
              "limitations"]
ALLOWED_CONFIDENCE = {"high", "medium", "low"}
ALLOWED_EVIDENCE = {"direct", "inferred"}
ALLOWED_SCOPE = {"approved_fix", "optional_cosmetic", "product_requirement_change"}
KEY_PATTERN = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")


def load_calls(review_dir):
    """Return [(call_name, attached_ids, report)] from the call files."""
    calls = []
    for exit_file in sorted(review_dir.glob("call-*.exit")):
        name = exit_file.name[: -len(".exit")]
        attached = []
        for line in exit_file.read_text().splitlines():
            if line.startswith("images="):
                attached = [part for part in line.split("=", 1)[1].split(",")
                            if part]
        report_file = review_dir / f"{name}.review.json"
        if not report_file.exists():
            calls.append((name, attached, None))
            continue
        try:
            report = json.loads(report_file.read_text())
        except json.JSONDecodeError as error:
            calls.append((name, attached, {"__parse_error__": str(error)}))
            continue
        calls.append((name, attached, report))
    return calls


def validate(name, attached, report, known_ids, problems):
    """Append every defect of one report to `problems`."""
    if report is None:
        problems.append(f"{name}: no review.json was written")
        return None
    if "__parse_error__" in report:
        problems.append(f"{name}: review.json is not valid JSON "
                        f"({report['__parse_error__']})")
        return None

    for field in TOP_FIELDS:
        if field not in report:
            problems.append(f"{name}: missing top-level field {field}")
    if not isinstance(report.get("inspected_images"), list) or \
            not report["inspected_images"]:
        problems.append(f"{name}: inspected_images is empty or not a list")
        return None

    inspected = set(report["inspected_images"])
    for image_id in inspected:
        if image_id not in known_ids:
            problems.append(f"{name}: inspected image {image_id} is not in "
                            f"the packet")
    missing = set(attached) - inspected
    if missing:
        problems.append(f"{name}: attached images not inspected: "
                        f"{', '.join(sorted(missing))}")

    findings = report.get("findings")
    if not isinstance(findings, list):
        problems.append(f"{name}: findings is not a list")
        findings = []
    for index, finding in enumerate(findings):
        where = f"{name} finding[{index}]"
        if not isinstance(finding, dict):
            problems.append(f"{where}: not an object")
            continue
        for field in FINDING_FIELDS:
            if field not in finding:
                problems.append(f"{where}: missing field {field}")
        for field in ("key", "title", "region", "observed_problem",
                      "player_effect", "proposed_improvement",
                      "reproduction_steps", "acceptance_criteria"):
            if field in finding and not str(finding[field]).strip():
                problems.append(f"{where}: empty {field}")
        if not KEY_PATTERN.match(str(finding.get("key", ""))):
            problems.append(f"{where}: key '{finding.get('key')}' is not "
                            f"kebab-case")
        priority = finding.get("priority")
        if not isinstance(priority, int) or not 1 <= priority <= 5:
            problems.append(f"{where}: priority {priority} is not 1..5")
        image_ids = finding.get("image_ids")
        if not isinstance(image_ids, list) or not image_ids:
            problems.append(f"{where}: image_ids is empty or not a list")
        else:
            for image_id in image_ids:
                if image_id not in known_ids:
                    problems.append(f"{where}: unknown image id {image_id}")
                elif image_id not in attached:
                    problems.append(f"{where}: image {image_id} was not "
                                    f"attached to {name}")
        if finding.get("confidence") not in ALLOWED_CONFIDENCE:
            problems.append(f"{where}: confidence "
                            f"'{finding.get('confidence')}' is not one of "
                            f"{sorted(ALLOWED_CONFIDENCE)}")
        if finding.get("evidence_type") not in ALLOWED_EVIDENCE:
            problems.append(f"{where}: evidence_type "
                            f"'{finding.get('evidence_type')}' is not one of "
                            f"{sorted(ALLOWED_EVIDENCE)}")
        if finding.get("scope") not in ALLOWED_SCOPE:
            problems.append(f"{where}: scope '{finding.get('scope')}' is not "
                            f"one of {sorted(ALLOWED_SCOPE)}")
        if not isinstance(finding.get("affected_files"), list):
            problems.append(f"{where}: affected_files is not a list")
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packet", required=True, type=Path)
    parser.add_argument("--review-dir", required=True, type=Path)
    args = parser.parse_args()

    manifest = json.loads((args.packet / "manifest.json").read_text())
    known_ids = {entry["id"] for entry in manifest["images"]}
    calls = load_calls(args.review_dir)
    if not calls:
        print("check_review: no call reports in "
              f"{args.review_dir}", file=sys.stderr)
        return 1

    problems = []
    merged = {}
    positives = []
    limitations = []
    per_call = []
    for name, attached, report in calls:
        findings = validate(name, attached, report, known_ids, problems)
        if report is None or "__parse_error__" in (report or {}):
            per_call.append({"call": name, "attached": attached,
                             "valid": False})
            continue
        per_call.append({
            "call": name,
            "attached": attached,
            "inspected": report["inspected_images"],
            "finding_count": len(findings or []),
            "valid": True,
        })
        positives.extend(f"{name}: {text}"
                         for text in report.get("positive_observations", []))
        limitations.extend(f"{name}: {text}"
                           for text in report.get("limitations", []))
        for finding in findings or []:
            key = finding["key"]
            if key not in merged:
                merged[key] = dict(finding)
                merged[key]["calls"] = [name]
                merged[key]["reported_count"] = 1
            else:
                merged[key]["calls"].append(name)
                merged[key]["reported_count"] += 1

    validation = {
        "packet": str(args.packet),
        "review_dir": str(args.review_dir),
        "calls": per_call,
        "problems": problems,
        "merged_finding_count": len(merged),
        "duplicate_keys": {key: value["reported_count"] for key, value
                           in merged.items() if value["reported_count"] > 1},
    }
    (args.review_dir / "validation.json").write_text(
        json.dumps(validation, indent=2) + "\n")

    merged_report = {
        "packet": str(args.packet),
        "round": manifest.get("round"),
        "calls": per_call,
        "positive_observations": positives,
        "limitations": limitations,
        "findings": sorted(merged.values(),
                           key=lambda item: item.get("priority", 9)),
    }
    (args.review_dir / "merged.json").write_text(
        json.dumps(merged_report, indent=2) + "\n")

    for problem in problems:
        print(f"check_review: {problem}", file=sys.stderr)
    print(f"check_review: {len(calls)} call(s), "
          f"{len(merged)} merged finding(s), "
          f"{len(problems)} validation problem(s)")
    print(f"check_review: merged report in {args.review_dir / 'merged.json'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
