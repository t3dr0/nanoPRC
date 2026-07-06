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

#include "prc_write_tess_3d.h"
#include "prc_data.h"
#include "prc_vector_util.h"

/* The per-vertex normal/position index pairs for every face are NOT stored
   in that face's own triangulateddata (which holds only structural counts,
   e.g. a bare triangle count for our PRC_FACETESSDATA_Triangle /
   TriangleOneNormal-only case); they live concatenated in the tess_3d-level
   triangulated_index_array, and each face's start_triangulated is its
   starting offset into that shared array (see
   prc_tri_primitives_api.c's src_index_data / prc_internal_api_set_triangles,
   and prc_internal_api_get_normal_position_index for the exact per-vertex
   layout this mirrors). So the whole array is built in memory first, since
   every face's start_triangulated must be known before any face record is
   written, and the array's own size must be written before its contents. */
int
prc_write_tess_3d(prc_context *ctx, prc_bit_write_state *s,
    const double *positions, uint32_t num_positions,
    const double *normals, uint32_t num_normals,
    const uint32_t *tri_indices, const uint32_t *norm_indices,
    uint32_t num_triangles,
    const uint32_t *face_tri_counts, uint32_t num_faces)
{
    uint32_t *global_idx = NULL;
    uint32_t *face_start = NULL;
    double *face_normals = NULL;
    uint32_t global_count = 0;
    uint32_t f, k, c, i, tri_cursor, check_sum;
    int ret = PRC_ERROR_INTERNAL;

    (void)num_normals;

    if (ctx == NULL || s == NULL || positions == NULL || tri_indices == NULL ||
        face_tri_counts == NULL || num_faces == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tess_3d: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }
    check_sum = 0;
    for (f = 0; f < num_faces; f++)
        check_sum += face_tri_counts[f];
    if (check_sum != num_triangles)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tess_3d: face_tri_counts does not sum to num_triangles\n");
        return PRC_ERROR_INTERNAL;
    }

    global_idx = (uint32_t *)prc_malloc(ctx, sizeof(uint32_t) * (size_t)num_triangles * 6);
    face_start = (uint32_t *)prc_malloc(ctx, sizeof(uint32_t) * num_faces);
    if (global_idx == NULL || face_start == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_tess_3d\n");
        goto cleanup;
    }

    if (norm_indices == NULL)
    {
        face_normals = (double *)prc_malloc(ctx, sizeof(double) * 3 * num_faces);
        if (face_normals == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_tess_3d\n");
            goto cleanup;
        }
        tri_cursor = 0;
        for (f = 0; f < num_faces; f++)
        {
            const uint32_t *t = &tri_indices[(size_t)tri_cursor * 3];
            prc_vec3 v0, v1, v2, e1, e2, n;

            v0.x = positions[(size_t)t[0] * 3 + 0]; v0.y = positions[(size_t)t[0] * 3 + 1]; v0.z = positions[(size_t)t[0] * 3 + 2];
            v1.x = positions[(size_t)t[1] * 3 + 0]; v1.y = positions[(size_t)t[1] * 3 + 1]; v1.z = positions[(size_t)t[1] * 3 + 2];
            v2.x = positions[(size_t)t[2] * 3 + 0]; v2.y = positions[(size_t)t[2] * 3 + 1]; v2.z = positions[(size_t)t[2] * 3 + 2];
            prc_vec_sub(v1, v0, &e1);
            prc_vec_sub(v2, v0, &e2);
            prc_vec_cross(e1, e2, &n);
            if (prc_vec_normalize(&n) < 0)
                n.x = n.y = n.z = 0.0; /* degenerate first triangle: harmless zero normal */
            face_normals[(size_t)f * 3 + 0] = n.x;
            face_normals[(size_t)f * 3 + 1] = n.y;
            face_normals[(size_t)f * 3 + 2] = n.z;
            tri_cursor += face_tri_counts[f];
        }
    }

    tri_cursor = 0;
    for (f = 0; f < num_faces; f++)
    {
        face_start[f] = global_count;
        for (k = 0; k < face_tri_counts[f]; k++)
        {
            uint32_t t = tri_cursor + k;

            if (norm_indices != NULL)
            {
                for (c = 0; c < 3; c++)
                {
                    global_idx[global_count++] = norm_indices[(size_t)t * 3 + c] * 3;
                    global_idx[global_count++] = tri_indices[(size_t)t * 3 + c] * 3;
                }
            }
            else
            {
                global_idx[global_count++] = f * 3;
                for (c = 0; c < 3; c++)
                    global_idx[global_count++] = tri_indices[(size_t)t * 3 + c] * 3;
            }
        }
        tri_cursor += face_tri_counts[f];
    }

    if (prc_bitwrite_bit(ctx, s, 0) != 0) goto fail;                          /* is_calculated */
    if (prc_bitwrite_uint32(ctx, s, num_positions * 3) != 0) goto fail;
    for (i = 0; i < num_positions * 3; i++)
        if (prc_bitwrite_double(ctx, s, positions[i]) != 0) goto fail;

    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;                          /* has_faces */
    if (prc_bitwrite_bit(ctx, s, 0) != 0) goto fail;                          /* has_loops */
    if (prc_bitwrite_bit(ctx, s, 0) != 0) goto fail;                          /* must_calculate_normals */

    if (norm_indices != NULL)
    {
        if (prc_bitwrite_uint32(ctx, s, num_normals * 3) != 0) goto fail;
        for (i = 0; i < num_normals * 3; i++)
            if (prc_bitwrite_double(ctx, s, normals[i]) != 0) goto fail;
    }
    else
    {
        if (prc_bitwrite_uint32(ctx, s, num_faces * 3) != 0) goto fail;
        for (i = 0; i < num_faces * 3; i++)
            if (prc_bitwrite_double(ctx, s, face_normals[i]) != 0) goto fail;
    }

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;                       /* number_of_wire_indices */

    if (prc_bitwrite_uint32(ctx, s, global_count) != 0) goto fail;            /* number_of_triangulated_indicies */
    for (i = 0; i < global_count; i++)
        if (prc_bitwrite_uint32(ctx, s, global_idx[i]) != 0) goto fail;

    if (prc_bitwrite_uint32(ctx, s, num_faces) != 0) goto fail;               /* number_of_face_tessellation */
    for (f = 0; f < num_faces; f++)
    {
        if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_TESS_Face) != 0) goto fail;  /* tag */
        if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;                   /* size_of_line_attributes */
        if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;                   /* start_of_wire_data */
        if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;                   /* size_of_sizes_wire */
        if (prc_bitwrite_uint32(ctx, s, norm_indices != NULL ?
                (uint32_t)PRC_FACETESSDATA_Triangle : (uint32_t)PRC_FACETESSDATA_TriangleOneNormal) != 0)
            goto fail;                                                       /* used_entities_flag */
        if (prc_bitwrite_uint32(ctx, s, face_start[f]) != 0) goto fail;       /* start_triangulated */
        if (prc_bitwrite_uint32(ctx, s, 1) != 0) goto fail;                  /* size_of_triangulateddata */
        if (prc_bitwrite_uint32(ctx, s, face_tri_counts[f]) != 0) goto fail;  /* triangulateddata[0] */
        if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;                  /* number_of_textured_coordinate_indexes */
        if (prc_bitwrite_bit(ctx, s, 0) != 0) goto fail;                     /* has_vertex_colors */
    }

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;                      /* number_of_texture_coordinates */

    ret = 0;
    goto cleanup;

fail:
    ret = s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;

cleanup:
    if (global_idx != NULL) prc_free(ctx, global_idx);
    if (face_start != NULL) prc_free(ctx, face_start);
    if (face_normals != NULL) prc_free(ctx, face_normals);
    return ret;
}
