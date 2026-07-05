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

#ifndef PRC_WRITE_COMPRESS_TESS_H
#define PRC_WRITE_COMPRESS_TESS_H

#include <stdint.h>
#include "prc_write_common.h"

typedef struct prc_encode_edge_s
{
    uint32_t v0, v1;     /* deduplicated vertex indices, v0 < v1 */
    int32_t  tri0, tri1; /* -1 if boundary (only one adjacent triangle) */
} prc_encode_edge;

typedef struct prc_encode_mesh_s
{
    double   *positions;       /* num_positions * 3 doubles, deduplicated */
    uint32_t  num_positions;
    uint32_t *tri_indices;     /* num_triangles * 3, into positions[], after dedup + degenerate removal */
    uint32_t  num_triangles;
    prc_encode_edge *edges;    /* one entry per unique undirected edge in the clean index array */
    uint32_t  num_edges;
    uint32_t *tri_component;   /* num_triangles entries: connected-component label per triangle */
    uint32_t  num_components;
    double    bbox[6];         /* xmin,ymin,zmin,xmax,ymax,zmax */
    double    tolerance_mm;    /* resolved tolerance actually used for dedup */
} prc_encode_mesh;

int prc_encode_preprocess(prc_context *ctx,
    const double *positions, uint32_t num_positions,
    const uint32_t *tri_indices, uint32_t num_triangles,
    prc_write_tolerance tolerance,
    prc_encode_mesh *out);

void prc_encode_preprocess_free(prc_context *ctx, prc_encode_mesh *m);

#endif
