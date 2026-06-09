"""Pytest-style smoke check driven by an environment variable.

Run:
    PowerShell:
        $env:NANOPRC_SAMPLE = "path/to/model.prc"
        python python/examples/03_pytest_style_smoke.py
    Bash:
        NANOPRC_SAMPLE=path/to/model.prc python python/examples/03_pytest_style_smoke.py

This mirrors a tiny pytest test function but remains runnable as a plain script.
"""

from __future__ import annotations

import os

import nanoprc_py


def test_open_sample_from_env() -> None:
    sample = os.environ.get("NANOPRC_SAMPLE")
    if not sample:
        raise RuntimeError("Set NANOPRC_SAMPLE to a PRC/PDF path before running this smoke check")

    ctx = nanoprc_py.Context()
    doc = ctx.open(sample)
    assert doc.is_open, "Document handle should be valid for a readable sample file"


def main() -> int:
    test_open_sample_from_env()
    print("Smoke test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
