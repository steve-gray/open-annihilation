#!/usr/bin/env python3
# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

"""The interface-words listing holds every text a lookup wrapper is handed.

A wrapper handed a text that no module lists fails this. The lists live in
the modules; this reads the built oa-tool and the sources, and compares them.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from interface_words_scan import required_words  # noqa: E402


def main() -> int:
    """Run oa-tool interface-words --json and compare it with the sources."""
    if len(sys.argv) != 3:
        print("usage: interface_words_test.py OA-TOOL ROOT", file=sys.stderr)
        return 2
    tool = sys.argv[1]
    root = sys.argv[2]
    listed = subprocess.run(
        [tool, "interface-words", "--json"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if listed.returncode != 0:
        print(listed.stderr, end="")
        print(f"interface-words --json exited {listed.returncode}")
        return 1
    try:
        words = json.loads(listed.stdout)
    except json.JSONDecodeError as error:
        print(f"the listing is not JSON: {error}")
        return 1
    if not isinstance(words, list) or not all(isinstance(word, str) for word in words):
        print("the listing is not a JSON array of texts")
        return 1
    if words != sorted(set(words)):
        print("the array is not sorted, or a text is repeated")
        return 1
    if "" in words:
        print("an empty text is listed")
        return 1
    required = required_words(root)
    missing = sorted(required - set(words))
    if missing:
        print(f"{len(missing)} looked-up texts are not in the listing:")
        for item in missing:
            print(f"  {item!r}")
        return 1
    refused = subprocess.run(
        [tool, "interface-words"],
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if refused.returncode != 2 or "interface-words" not in refused.stderr:
        print(f"interface-words without --json exited {refused.returncode}")
        print(refused.stderr, end="")
        return 1
    print(f"interface-words: {len(words)} texts, every looked-up text is listed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
