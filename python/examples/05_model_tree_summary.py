"""Create and print a simple model-tree summary from a PRC/PDF file.

Run:
    python python/examples/05_model_tree_summary.py path/to/model.prc
"""

from __future__ import annotations

import argparse

import nanoprc_py


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Print model tree information for a nanoprc_py document")
    parser.add_argument("path", help="Path to input PRC/PDF file")
    return parser.parse_args()


def print_node(node: nanoprc_py.ModelNode, indent: int = 0) -> None:
    prefix = " " * indent
    print(f"{prefix}- {node.name} (model={node.is_model}, children={node.num_children}, markups={node.num_markups}, has_part={node.has_part})")
    for child in node.children():
        print_node(child, indent + 2)


def main() -> int:
    args = parse_args()
    ctx = nanoprc_py.Context()
    doc = ctx.open(args.path)

    print(f"Opened: {args.path}")
    counts = doc.prepare_model_tree()
    print(f"Model tree counts: parts={counts[0]}, products={counts[1]}, markups={counts[2]}")

    root = doc.create_model_tree()
    print("Model tree:")
    print_node(root)

    tess_counts = doc.tessellation_counts()
    print(f"Tessellation counts: tess={tess_counts[0]}, line_tess={tess_counts[1]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())