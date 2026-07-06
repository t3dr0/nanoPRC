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
   edge adjacency, connected components).
   Extended in Session 3 with the first full-bitstream round trips: encoder
   (Steps A+B+C1+E) -> real, unmodified prc_parse_tess_3d_compressed. */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "prc_test.h"
#include "prc_context.h"
#include "prc_write_compress_tess.h"
#include "prc_parse_tess.h"

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

static double
dist3(const double *a, const double *b)
{
    double dx = a[0] - b[0];
    double dy = a[1] - b[1];
    double dz = a[2] - b[2];

    return sqrt(dx * dx + dy * dy + dz * dz);
}

/* Frees everything prc_parse_tess_3d_compressed allocated: the same field
   list as prc_release.c's (static) prc_release_tess_3d_compressed, minus the
   texture/behavior sub-objects this encoder never emits. */
static void
free_parsed_tess(prc_context *ctx, prc_tess_3d_compressed *d)
{
    if (d == NULL)
        return;
    if (d->point_array != NULL)
        prc_free(ctx, d->point_array);
    if (d->edge_status_array != NULL)
        prc_free(ctx, d->edge_status_array);
    if (d->triangle_face_array != NULL)
        prc_free(ctx, d->triangle_face_array);
    if (d->points_is_reference_array != NULL)
        prc_free(ctx, d->points_is_reference_array);
    if (d->point_reference_array != NULL)
        prc_free(ctx, d->point_reference_array);
    if (d->normal_is_reversed != NULL)
        prc_free(ctx, d->normal_is_reversed);
    if (d->normal_binary_data != NULL)
        prc_free(ctx, d->normal_binary_data);
    if (d->normal_angle_array != NULL)
        prc_free(ctx, d->normal_angle_array);
    if (d->normal_angle_array_scaled != NULL)
        prc_free(ctx, d->normal_angle_array_scaled);
    if (d->is_face_planar != NULL)
        prc_free(ctx, d->is_face_planar);
    if (d->is_point_color_on_face != NULL)
        prc_free(ctx, d->is_point_color_on_face);
    if (d->point_color_array != NULL)
        prc_free(ctx, d->point_color_array);
    if (d->decoded_point_color_array != NULL)
        prc_free(ctx, d->decoded_point_color_array);
    if (d->is_multiple_line_attribute_on_face != NULL)
        prc_free(ctx, d->is_multiple_line_attribute_on_face);
    if (d->line_attribute_array != NULL)
        prc_free(ctx, d->line_attribute_array);
    if (d->face_has_texture != NULL)
        prc_free(ctx, d->face_has_texture);
    if (d->uv_coordinates_3d != NULL)
        prc_free(ctx, d->uv_coordinates_3d);
    if (d->triangle_indices_prc_compressed_3d != NULL)
        prc_free(ctx, d->triangle_indices_prc_compressed_3d);
    if (d->point_colors_prc_compressed_3d != NULL)
        prc_free(ctx, d->point_colors_prc_compressed_3d);
    if (d->normal_indices_prc_compressed_3d != NULL)
        prc_free(ctx, d->normal_indices_prc_compressed_3d);
    if (d->vertices_prc_compressed_3d != NULL)
        prc_free(ctx, d->vertices_prc_compressed_3d);
    if (d->normals_prc_compressed_3d != NULL)
        prc_free(ctx, d->normals_prc_compressed_3d);
    if (d->triangle_styles != NULL)
        prc_free(ctx, d->triangle_styles);
    if (d->edge_vertices != NULL)
        prc_free(ctx, d->edge_vertices);
    if (d->edge_indices != NULL)
        prc_free(ctx, d->edge_indices);
    prc_free(ctx, d);
}

/* Session 3 gate: cube through the complete C1 pipeline and the REAL parser.
   preprocess -> traversal -> C1 reversal bits -> full bitstream -> flush ->
   prc_parse_tess_3d_compressed (which itself runs the real decoder). */
static void
test_cube_c1_roundtrip(prc_context *ctx)
{
    double positions[8 * 3] = {
        -0.5, -0.5, -0.5,
         0.5, -0.5, -0.5,
         0.5,  0.5, -0.5,
        -0.5,  0.5, -0.5,
        -0.5, -0.5,  0.5,
         0.5, -0.5,  0.5,
         0.5,  0.5,  0.5,
        -0.5,  0.5,  0.5
    };
    uint32_t tris[12 * 3] = {
        0, 2, 1,   0, 3, 2,   /* bottom */
        4, 5, 6,   4, 6, 7,   /* top */
        0, 1, 5,   0, 5, 4,   /* front */
        1, 2, 6,   1, 6, 5,   /* right */
        2, 3, 7,   2, 7, 6,   /* back */
        3, 0, 4,   3, 4, 7    /* left */
    };
    prc_encode_mesh mesh;
    prc_encode_traversal_result res;
    prc_tess_3d_compressed *parsed = NULL;
    uint8_t *rev = NULL;
    prc_bit_write_state w;
    prc_bit_state r;
    uint32_t k, c, i;
    int code;

    printf("  sub-case: cube C1 round trip\n");

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 8, tris, 12,
        prc_write_tol_absolute(1e-4), &mesh), 0);
    PRC_ASSERT_EQ(mesh.num_positions, 8);
    PRC_ASSERT_EQ(mesh.num_triangles, 12);
    PRC_ASSERT_EQ(prc_encode_traversal(ctx, &mesh, NULL, mesh.tolerance_mm, &res), 0);
    PRC_ASSERT_NOT_NULL(res.triangle_point_indices);
    PRC_ASSERT_NOT_NULL(res.decoded_positions);
    PRC_ASSERT_EQ(res.num_decoded_points, 8);

    PRC_ASSERT_EQ(prc_encode_normals_c1(ctx, &mesh, &res, NULL, &rev), 0);
    PRC_ASSERT_NOT_NULL(rev);
    for (k = 0; k < 12; k++)
        PRC_ASSERT_EQ(rev[k], 0);

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 256), 0);
    PRC_ASSERT_EQ(prc_write_compress_tess_to_stream(ctx, &w, &res, mesh.tolerance_mm,
        rev, 30.0, NULL, 0, NULL, 0, 1), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    code = prc_parse_tess_3d_compressed(ctx, &r, &parsed, 0);
    if (code < 0)
        prc_print_error_stack(ctx);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_NOT_NULL(parsed);

    PRC_ASSERT_EQ(parsed->num_triangle_indices_prc_compressed_3d / 3, 12);
    PRC_ASSERT_EQ(parsed->num_vertices_prc_compressed_3d, 8);
    PRC_ASSERT_NOT_NULL(parsed->vertices_prc_compressed_3d);
    PRC_ASSERT_NOT_NULL(parsed->triangle_indices_prc_compressed_3d);

    for (k = 0; k < 12; k++)
    {
        for (c = 0; c < 3; c++)
        {
            uint32_t idx = parsed->triangle_indices_prc_compressed_3d[k * 3 + c];
            const double *p;
            double best_d = 0.0;

            PRC_ASSERT(idx < 8);
            p = &parsed->vertices_prc_compressed_3d[(size_t)idx * 3];
            for (i = 0; i < 8; i++)
            {
                double d = dist3(p, &positions[(size_t)i * 3]);

                if (i == 0 || d < best_d)
                    best_d = d;
            }
            PRC_ASSERT(best_d <= 2.0 * mesh.tolerance_mm);
        }
    }

    free_parsed_tess(ctx, parsed);
    prc_free(ctx, rev);
    prc_bitwrite_release(ctx, &w);
    prc_encode_traversal_free(ctx, &res);
    prc_encode_preprocess_free(ctx, &mesh);
}

/* Empirical pin of the C1 reversal-bit sign convention: a single triangle
   (no growable edges, so any bit value keeps the traversal in sync) with
   supplied normals parallel then anti-parallel to its winding cross product.
   The decoded normal must come back on the supplied side both times. */
static void
test_c1_sign_convention(prc_context *ctx, double nz)
{
    double positions[3 * 3] = {
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0
    };
    uint32_t tris[3] = { 0, 1, 2 };
    double normals[3 * 3] = {
        0.0, 0.0, nz,
        0.0, 0.0, nz,
        0.0, 0.0, nz
    };
    prc_encode_mesh mesh;
    prc_encode_traversal_result res;
    prc_tess_3d_compressed *parsed = NULL;
    uint8_t *rev = NULL;
    prc_bit_write_state w;
    prc_bit_state r;
    uint32_t nidx;
    double dot;
    int code;

    printf("  sub-case: C1 sign convention, input normal z = %+.0f\n", nz);

    PRC_ASSERT_EQ(prc_encode_preprocess(ctx, positions, 3, tris, 1,
        prc_write_tol_absolute(1e-4), &mesh), 0);
    PRC_ASSERT_EQ(prc_encode_traversal(ctx, &mesh, NULL, mesh.tolerance_mm, &res), 0);
    PRC_ASSERT_EQ(prc_encode_normals_c1(ctx, &mesh, &res, normals, &rev), 0);
    PRC_ASSERT_NOT_NULL(rev);

    PRC_ASSERT_EQ(prc_bitwrite_init(ctx, &w, 128), 0);
    PRC_ASSERT_EQ(prc_write_compress_tess_to_stream(ctx, &w, &res, mesh.tolerance_mm,
        rev, 30.0, NULL, 0, NULL, 0, 1), 0);
    PRC_ASSERT_EQ(prc_bitwrite_flush(ctx, &w), 0);

    prc_init_bit_state(ctx, &r, w.buf, w.byte_pos);
    code = prc_parse_tess_3d_compressed(ctx, &r, &parsed, 0);
    if (code < 0)
        prc_print_error_stack(ctx);
    PRC_ASSERT_EQ(code, 0);
    PRC_ASSERT_NOT_NULL(parsed);
    PRC_ASSERT_EQ(parsed->num_triangle_indices_prc_compressed_3d / 3, 1);
    PRC_ASSERT_NOT_NULL(parsed->normal_indices_prc_compressed_3d);
    PRC_ASSERT_NOT_NULL(parsed->normals_prc_compressed_3d);

    nidx = parsed->normal_indices_prc_compressed_3d[0];
    PRC_ASSERT(nidx < (uint32_t)parsed->num_normals_prc_compressed_3d);
    dot = parsed->normals_prc_compressed_3d[(size_t)nidx * 3 + 2] * nz;
    PRC_ASSERT(dot > 0.9);

    free_parsed_tess(ctx, parsed);
    prc_free(ctx, rev);
    prc_bitwrite_release(ctx, &w);
    prc_encode_traversal_free(ctx, &res);
    prc_encode_preprocess_free(ctx, &mesh);
}

int
main(void)
{
    prc_context *ctx;

    PRC_TEST_BEGIN("compress_tess preprocessing + C1 round trip");

    ctx = prc_new_context(NULL);
    PRC_ASSERT_NOT_NULL(ctx);

    test_single_triangle(ctx);
    test_quad_shared_edge(ctx);
    test_disjoint_triangles(ctx);
    test_degenerate_weld(ctx);
    test_cube_c1_roundtrip(ctx);
    test_c1_sign_convention(ctx, 1.0);
    test_c1_sign_convention(ctx, -1.0);

    prc_release_context(ctx);

    PRC_TEST_END;
}
