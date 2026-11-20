#!/usr/bin/env python3
"""Check local Social Bridge readiness only; not an end-to-end microphone test."""
import argparse
import getpass
import os
import urllib.error
import urllib.request
import json

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--url", required=True, help="e.g. http://192.168.1.50:8765")
    args = p.parse_args()
    token = os.getenv("ALA_BRIDGE_TOKEN") or getpass.getpass("Ala bridge token (hidden): ")
    if not args.url.startswith("http://"):
        raise SystemExit("ONLY current experimental HTTP LAN transport is supported")
    req = urllib.request.Request(args.url.rstrip("/") + "/v1/health",
                                 headers={"Authorization": "Bearer " + token})
    try:
        with urllib.request.urlopen(req, timeout=5) as response:
            payload = json.load(response)
    except (urllib.error.URLError, TimeoutError, ValueError) as error:
        raise SystemExit(f"BACKEND NOT READY: {type(error).__name__}") from None
    if not payload.get("ready"):
        raise SystemExit("BACKEND NOT READY")
    print("BACKEND RESPONDS. Speech, semantic model and speaker NOT VERIFIED.")
    print("Transport: " + str(payload.get("transport", "unknown")))
    print("Remember: HTTP+bearer is NOT encrypted and requires a trusted LAN.")

if __name__ == "__main__":
    main()
