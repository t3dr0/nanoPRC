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

#ifndef PRC_WRITE_WIRE_TESS_H
#define PRC_WRITE_WIRE_TESS_H

#include "prc_write_common.h"
#include "prc_data.h"
#include "prc_bit.h"

/* One polyline or line-segment element for the PRC_TYPE_TESS_3D_Wire
   encoder (Table 142), the exact inverse of prc_parse_tess_3d_wire in
   prc_parse_tess.c.

   A line segment is an element with num_vertices == 2 and is_closed == 0.
   A polyline is an element with num_vertices >= 2. A closed polygon (wire
   loop) sets is_closed = 1; the reader implies the closing edge from the
   last vertex back to the first automatically
   (PRC_3DWIRETESSDATA_IsClosing). is_continuous marks that this element
   shares its first vertex with the previous element
   (PRC_3DWIRETESSDATA_IsContinuous); it has no effect on encoding beyond
   that one flag bit -- vertex deduplication is always exact-position-based
   across every element regardless of it. */
typedef struct prc_write_wire_element_s
{
    const float *positions;     /* 3 floats per vertex: x, y, z */
    uint32_t     num_vertices;
    uint8_t      is_closed;     /* 1 = closed polyline (PRC_3DWIRETESSDATA_IsClosing) */
    uint8_t      is_continuous; /* 1 = shares first vertex with previous element */
    const float *colors;        /* 4 floats per vertex (RGBA), or NULL */
} prc_write_wire_element;

/* Encode a wire (line/polyline) tessellation. Vertex positions are
   deduplicated (exact float match) across all elements before encoding;
   the shared coordinate pool and per-element index lists are built
   internally from the raw per-vertex positions supplied here.

   has_vertex_colors is written 1 if any element supplies a non-NULL
   `colors` array, 0 otherwise. When 1, every vertex in every element gets
   a color entry -- elements that did not supply colors are filled with
   opaque white (255,255,255,255). */
int prc_write_wire_tess(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_wire_element *elements, uint32_t num_elements);

#endif
