"""Open a PRC/PDF file and report whether the document handle is valid.

Run:
    python python/examples/02_open_file.py path/to/model.prc
"""

from __future__ import annotations

import argparse

import nanoprc_py


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Open a PRC/PDF file with nanoprc_py")
    parser.add_argument("path", help="Path to input PRC/PDF file")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ctx = nanoprc_py.Context()

    try:
        doc = ctx.open(args.path)
    except RuntimeError as exc:
        print(f"Failed to open '{args.path}': {exc}")
        print("nanoPRC error stack:")
        ctx.print_error_stack()
        return 1

    print("Opened:", args.path)
    print("Document handle valid:", doc.is_open)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
