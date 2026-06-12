"""Summarize tessellation face counts and show sample vertex positions.

Run:
    python python/examples/06_tessellation_summary.py path/to/model.prc
"""

from __future__ import annotations

import argparse

import nanoprc_py


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize tessellation faces in a nanoprc_py document")
    parser.add_argument("path", help="Path to input PRC/PDF file")
    return parser.parse_args()


def format_vertices(vertices: list[tuple[float, float, float]], limit: int = 5) -> str:
    sample = vertices[:limit]
    lines = [f"    {i}: ({x:.4f}, {y:.4f}, {z:.4f})" for i, (x, y, z) in enumerate(sample)]
    if len(vertices) > limit:
        lines.append(f"    ... and {len(vertices) - limit} more vertices")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    ctx = nanoprc_py.Context()
    doc = ctx.open(args.path)

    print(f"Opened: {args.path}")
    counts = doc.prepare_model_tree()
    root = doc.create_model_tree()
    tess_counts = doc.tessellation_counts()
    print(f"Tessellations: {tess_counts[0]}, line tessellations: {tess_counts[1]}")

    for tess_index in range(tess_counts[0]):
        face_count = doc.number_of_faces(tess_index)
        print(f"Tessellation {tess_index}: {face_count} faces")
        if face_count == 0:
            continue
        vertex_count = doc.face_vertex_count(tess_index, 0)
        print(f"  First face vertex count: {vertex_count}")
        vertices = doc.face_vertices(tess_index, 0)
        print("  First face sample vertices:")
        print(format_vertices(vertices, limit=100))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())