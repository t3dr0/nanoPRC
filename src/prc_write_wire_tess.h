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

/* prc_write_wire_element is an alias of the public prc_api_write_wire_element
   (include/prc_api.h) -- see that type's doc comment for the field
   semantics (PRC_3DWIRETESSDATA_IsClosing/IsContinuous mapping, exact-match
   vertex dedup, default-white color fill). Aliased rather than redefined
   so the internal encoder and the public API can never drift apart. */
typedef prc_api_write_wire_element prc_write_wire_element;

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
