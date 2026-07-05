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

/* Phase 1b, Session 1 gate test: prc_encode_preprocess Step A preprocessing
   (bbox, tolerance resolution, quantized vertex dedup, degenerate removal,
   edge adjacency, connected components). */

#include <stdlib.h>
#include <stdint.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_write_compress_tess.h"

static void
test_single_triangle(prc_context *ctx)
{
    double positions[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0
    };
    uint32_t tris[] = { 0, 1, 2 };
    prc_encode_mesh mesh;

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 3, tris, 1,
        prc_write_tol_absolute(1e-4), &mesh), 0);
    PRC_ASSERT_EQ(mesh.num_positions, 3);
    PRC_ASSERT_EQ(mesh.num_triangles, 1);
    PRC_ASSERT_EQ(mesh.num_edges, 3);
    PRC_ASSERT_EQ(mesh.num_components, 1);
    PRC_ASSERT_NOT_NULL(mesh.edges);
    PRC_ASSERT_EQ(mesh.edges[0].tri1, -1);
    PRC_ASSERT_EQ(mesh.edges[1].tri1, -1);
    PRC_ASSERT_EQ(mesh.edges[2].tri1, -1);
    PRC_ASSERT(mesh.tolerance_mm == 1e-4);
    PRC_ASSERT(mesh.bbox[0] == 0.0 && mesh.bbox[3] == 1.0);
    prc_encode_preprocess_free(ctx, &mesh);
    PRC_ASSERT_NULL(mesh.positions);
    prc_encode_preprocess_free(ctx, &mesh);
}

static void
test_quad_shared_edge(prc_context *ctx)
{
    double positions[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0, 1.0, 0.0,
        0.0, 1.0, 0.0
    };
    /* diagonal 0-2 shared between the two triangles */
    uint32_t tris[] = { 0, 1, 2, 0, 2, 3 };
    prc_encode_mesh mesh;
    uint32_t i, shared_found = 0;

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 4, tris, 2,
        prc_write_tol_absolute(1e-4), &mesh), 0);
    PRC_ASSERT_EQ(mesh.num_positions, 4);
    PRC_ASSERT_EQ(mesh.num_triangles, 2);
    PRC_ASSERT_EQ(mesh.num_edges, 5);
    PRC_ASSERT_EQ(mesh.num_components, 1);
    for (i = 0; i < mesh.num_edges; i++)
    {
        if (mesh.edges[i].v0 == 0 && mesh.edges[i].v1 == 2)
        {
            shared_found = 1;
            PRC_ASSERT(mesh.edges[i].tri0 != -1);
            PRC_ASSERT(mesh.edges[i].tri1 != -1);
            PRC_ASSERT((mesh.edges[i].tri0 == 0 && mesh.edges[i].tri1 == 1) ||
                       (mesh.edges[i].tri0 == 1 && mesh.edges[i].tri1 == 0));
        }
        else
        {
            PRC_ASSERT_EQ(mesh.edges[i].tri1, -1);
        }
    }
    PRC_ASSERT_EQ(shared_found, 1);
    prc_encode_preprocess_free(ctx, &mesh);
}

static void
test_disjoint_triangles(prc_context *ctx)
{
    double positions[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        10.0, 0.0, 0.0,
        11.0, 0.0, 0.0,
        10.0, 1.0, 0.0
    };
    uint32_t tris[] = { 0, 1, 2, 3, 4, 5 };
    prc_encode_mesh mesh;

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 6, tris, 2,
        prc_write_tol_absolute(1e-4), &mesh), 0);
    PRC_ASSERT_EQ(mesh.num_positions, 6);
    PRC_ASSERT_EQ(mesh.num_triangles, 2);
    PRC_ASSERT_EQ(mesh.num_edges, 6);
    PRC_ASSERT_EQ(mesh.num_components, 2);
    PRC_ASSERT(mesh.tri_component[0] != mesh.tri_component[1]);
    prc_encode_preprocess_free(ctx, &mesh);
}

static void
test_degenerate_weld(prc_context *ctx)
{
    /* vertices 1 and 2 differ by 1e-8 << 1e-4 tolerance: they weld to one
       deduplicated slot, making the triangle degenerate */
    double positions[] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        1.0 + 1e-8, 0.0, 0.0
    };
    uint32_t tris[] = { 0, 1, 2 };
    prc_encode_mesh mesh;

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 3, tris, 1,
        prc_write_tol_absolute(1e-4), &mesh), 0);
    PRC_ASSERT_EQ(mesh.num_positions, 2);
    PRC_ASSERT_EQ(mesh.num_triangles, 0);
    PRC_ASSERT_EQ(mesh.num_edges, 0);
    PRC_ASSERT_EQ(mesh.num_components, 0);
    PRC_ASSERT_NULL(mesh.tri_indices);
    prc_encode_preprocess_free(ctx, &mesh);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("compress_tess preprocessing");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_single_triangle(ctx);
    test_quad_shared_edge(ctx);
    test_disjoint_triangles(ctx);
    test_degenerate_weld(ctx);

    prc_release_context(ctx);

    PRC_TEST_END;
}
