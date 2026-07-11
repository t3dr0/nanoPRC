/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PRC_WRITE_TESS_3D_H
#define PRC_WRITE_TESS_3D_H

#include "prc_write_common.h"
#include "prc_data.h"
#include "prc_bit.h"

/* Encoder for PRC_TYPE_TESS_3D (Table 138), the uncompressed tessellation --
   the exact inverse of prc_parse_tess_3d in prc_parse_tess.c. No
   deduplication, no quantization: positions and normals are stored as raw
   doubles, one face-index array entry per triangle vertex.

   positions: 3 doubles per position, num_positions entries.
   normals/norm_indices: if norm_indices is non-NULL, it supplies 3 normal
     indices per triangle (into `normals`, num_normals entries) and every
     triangle is written PRC_FACETESSDATA_Triangle (per-vertex normals). If
     norm_indices is NULL, `normals`/`num_normals` are ignored: one normal is
     computed per face (the normalized cross product of the face's first
     triangle's first two edges) and every triangle in that face is written
     PRC_FACETESSDATA_TriangleOneNormal referencing it.
   tri_indices: 3 position indices per triangle, num_triangles entries.
   face_tri_counts: num_faces entries; face_tri_counts[f] triangles are
     consumed from tri_indices/norm_indices, in order, for face f. Must sum
     to num_triangles.

   Face-embedded wire indices are out of scope (not written). */
int prc_write_tess_3d(prc_context *ctx, prc_bit_write_state *s,
    const double *positions, uint32_t num_positions,
    const double *normals, uint32_t num_normals,
    const uint32_t *tri_indices, const uint32_t *norm_indices,
    uint32_t num_triangles,
    const uint32_t *face_tri_counts, uint32_t num_faces);

#endif
