#!/usr/bin/env python3
"""Prevent bootloops caused by ESP_ERR_NVS_KEY_TOO_LONG (max 15 bytes)."""
from pathlib import Path
import re

MAX_NVS_KEY_BYTES = 15
# The Settings wrapper passes literal keys straight into nvs_get/set_*.
KEY_CALL = re.compile(
    r'\b(?:Get|Set)(?:Bool|Int|String|Float|Double)\s*\(\s*"([^"]+)"'
)


def check_text(path: str, text: str) -> list[str]:
    errors: list[str] = []
    for match in KEY_CALL.finditer(text):
        key = match.group(1)
        length = len(key.encode("utf-8"))
        if length > MAX_NVS_KEY_BYTES:
            lineno = text.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path}:{lineno}: NVS key {key!r} has {length} bytes "
                f"(max {MAX_NVS_KEY_BYTES})"
            )
    return errors


def main() -> None:
    # Assert the test itself catches the regression in 0.2.7.
    assert check_text("example", 'cfg.SetBool("speaker_defaults_027", true);')
    assert not check_text("example", 'cfg.SetBool("spk_init_v027", true);')

    errors: list[str] = []
    for path in list(Path("firmware").rglob("*.cc")) + list(Path("firmware").rglob("*.h")):
        errors.extend(check_text(str(path), path.read_text(encoding="utf-8")))
    if errors:
        raise SystemExit("\n".join(errors))
    print("NVS settings keys: all literal keys fit ESP-IDF 15-byte limit")


if __name__ == "__main__":
    main()
