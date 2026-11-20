#!/usr/bin/env python3
"""32 Polish social judgment scenarios; structural checks are free, live calls opt-in."""
import argparse
import json
import os
from pathlib import Path

from server import OpenAIAdapter

DATA = Path(__file__).with_name("evaluation_cases.json")
ALL_ACTIONS = {"IGNORE", "WAIT", "VISUAL_REACTION", "JOKE", "SHORT_COMMENT", "SUPPORT"}

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ack-paid-calls", action="store_true")
    args = parser.parse_args()
    cases = json.loads(DATA.read_text(encoding="utf-8"))
    assert len(cases) >= 30
    assert len({row["id"] for row in cases}) == len(cases)
    for row in cases:
        assert row["utterance"] and set(row["allowed_actions"]) <= ALL_ACTIONS
        if not row["spoken_allowed"]:
            assert not set(row["allowed_actions"]) & {"JOKE", "SHORT_COMMENT", "SUPPORT"}
    print(f"Evaluation dataset PASS: {len(cases)} Polish scenarios and allowed-decision bands.")
    if not args.ack_paid_calls or not os.getenv("OPENAI_API_KEY"):
        print("LIVE EVALUATION NOT RUN: needs --ack-paid-calls and OPENAI_API_KEY.")
        return 0
    adapter = OpenAIAdapter(os.environ["OPENAI_API_KEY"])
    errors = []
    for row in cases:
        verdict = adapter.verdict([row["utterance"]])
        if verdict["action"] not in row["allowed_actions"]:
            errors.append(f"case={row['id']} category={row['category']} action={verdict['action']}")
    print(f"LIVE EVALUATION: {len(cases)-len(errors)}/{len(cases)} expected decision bands.")
    for error in errors:
        print(error)
    return bool(errors)

if __name__ == "__main__":
    raise SystemExit(main())
