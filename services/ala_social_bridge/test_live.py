#!/usr/bin/env python3
"""Opt-in LIVE OpenAI smoke test. Every call may incur API charges.

Never sends private recordings: the caller supplies a deliberately prepared,
non-sensitive Polish WAV fixture. No transcripts, recordings or keys are logged
or saved. This test is NOT part of CI.
"""
import argparse
import os
import time
from pathlib import Path

from server import BridgeError, OpenAIAdapter, MAX_TTS_PCM, validate_wav

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ack-paid-calls", action="store_true",
                        help="Explicit consent for paid OpenAI STT/LLM/TTS calls")
    parser.add_argument("--wav", type=Path,
                        help="Locally prepared, non-sensitive Polish 16 kHz mono PCM16 WAV")
    args = parser.parse_args()
    key = os.getenv("OPENAI_API_KEY", "")
    if not args.ack_paid_calls or not key or args.wav is None:
        print("NOT RUN: requires --ack-paid-calls --wav <fixture> and OPENAI_API_KEY")
        return 0
    if not args.wav.is_file():
        print("NOT RUN: WAV fixture missing")
        return 0
    data = args.wav.read_bytes()
    try:
        validate_wav(data)
        adapter = OpenAIAdapter(key)
        start = time.perf_counter()
        transcript = adapter.transcribe(data)
        t_stt = time.perf_counter()
        if not transcript:
            raise RuntimeError("STT empty; provide an audible Polish test utterance")
        verdict = adapter.verdict([transcript])
        t_llm = time.perf_counter()
        if verdict.get("action") in ("JOKE", "SHORT_COMMENT", "SUPPORT"):
            pcm = adapter.speak(verdict["text"])
            if len(pcm) % 2 or not 960 <= len(pcm) <= MAX_TTS_PCM:
                raise RuntimeError("TTS PCM invalid")
            pcm_bytes = len(pcm)
        else:
            pcm_bytes = 0
        t_tts = time.perf_counter()
        quiet = adapter.verdict(["Babcia jest ciężko chora i bardzo się o nią martwię."])
        t_quiet = time.perf_counter()
        if quiet["action"] != "IGNORE" or quiet["text"]:
            raise RuntimeError("Sensitive family conversation did not result in silence")
        print("LIVE PASS: live STT, semantic JSON and sensitive-context silence verified.")
        print(f"Verdict={verdict['action']}; PCM bytes={pcm_bytes}; source audio not logged.")
        print(f"STT={t_stt-start:.2f}s LLM={t_llm-t_stt:.2f}s "
              f"TTS={t_tts-t_llm:.2f}s silence-model={t_quiet-t_tts:.2f}s")
        if pcm_bytes == 0:
            print("TTS NOT RUN: model intentionally selected silence; provide a safe humorous fixture.")
        return 0
    except (BridgeError, RuntimeError, OSError, ValueError) as exc:
        # Never dump provider exception details or transcripts.
        print(f"LIVE FAIL: {type(exc).__name__}; inspect the test configuration.")
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
