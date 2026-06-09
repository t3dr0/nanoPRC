"""List available named views from a PRC/PDF file.

Run:
    python python/examples/04_list_views.py path/to/model.prc
"""

from __future__ import annotations

import argparse

import nanoprc_py


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="List views in a nanoprc_py document")
    parser.add_argument("path", help="Path to input PRC/PDF file")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ctx = nanoprc_py.Context()
    doc = ctx.open(args.path)

    print(f"Opened: {args.path}")
    print(f"Number of views: {doc.number_of_views}")

    for index in range(doc.number_of_views):
        view = doc.get_view(index)
        print(f"View {index}: {view.name}")
        print(f"  camera_z = {view.camera_z}")
        print(f"  matrix = {[round(x, 4) for x in view.matrix]}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())