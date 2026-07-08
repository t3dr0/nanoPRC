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

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "prc_write_compress_tess.h"
#include "prc_vector_util.h"
#include "prc_parse_common.h"
#include "prc_decode_compressed_tess.h"
#include "prc_huff.h"

typedef struct
{
    int64_t key[3];
    uint32_t index;
    uint8_t used;
} prc_vtx_slot;

typedef struct
{
    uint32_t v0, v1;
    uint32_t edge_index;
    uint8_t used;
} prc_edge_slot;

static size_t
prc_next_pow2(size_t v)
{
    size_t c = 16;
    while (c < v)
        c *= 2;
    return c;
}

static uint64_t
prc_mix64(uint64_t h)
{
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return h;
}

static uint64_t
prc_vtx_hash(int64_t kx, int64_t ky, int64_t kz)
{
    uint64_t h = prc_mix64((uint64_t)kx);
    h = prc_mix64(h ^ (uint64_t)ky);
    h = prc_mix64(h ^ (uint64_t)kz);
    return h;
}

/* Path halving keeps find() strictly iterative, per the project rule that
   input-size-driven depth must never use C call-stack recursion. */
static uint32_t
prc_uf_find(uint32_t *parent, uint32_t x)
{
    while (parent[x] != x)
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

int
prc_encode_preprocess(prc_context *ctx,
    const double *positions, uint32_t num_positions,
    const uint32_t *tri_indices, uint32_t num_triangles,
    prc_write_tolerance tolerance,
    prc_encode_mesh *out)
{
    prc_vtx_slot *vtable = NULL;
    prc_edge_slot *etable = NULL;
    uint32_t *remap = NULL;
    uint32_t *parent = NULL;
    uint32_t *label = NULL;
    double diagonal, tol;
    uint32_t i;
    uint32_t clean_tris = 0;
    int ret = PRC_ERROR_INTERNAL;

    if (out == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_preprocess: NULL output mesh\n");
        return PRC_ERROR_INTERNAL;
    }
    memset(out, 0, sizeof(*out));

    if ((num_positions > 0 && positions == NULL) ||
        (num_triangles > 0 && tri_indices == NULL))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_preprocess: NULL input array with non-zero count\n");
        return PRC_ERROR_INTERNAL;
    }

    {
        size_t n;
        for (n = 0; n < (size_t)num_triangles * 3; n++)
        {
            if (tri_indices[n] >= num_positions)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_preprocess: triangle index out of range\n");
                return PRC_ERROR_INTERNAL;
            }
        }
    }

    if (num_positions > 0)
    {
        out->bbox[0] = out->bbox[3] = positions[0];
        out->bbox[1] = out->bbox[4] = positions[1];
        out->bbox[2] = out->bbox[5] = positions[2];
        for (i = 1; i < num_positions; i++)
        {
            uint32_t k;
            for (k = 0; k < 3; k++)
            {
                double v = positions[(size_t)i * 3 + k];
                if (v < out->bbox[k])
                    out->bbox[k] = v;
                if (v > out->bbox[k + 3])
                    out->bbox[k + 3] = v;
            }
        }
    }
    diagonal = sqrt(
        (out->bbox[3] - out->bbox[0]) * (out->bbox[3] - out->bbox[0]) +
        (out->bbox[4] - out->bbox[1]) * (out->bbox[4] - out->bbox[1]) +
        (out->bbox[5] - out->bbox[2]) * (out->bbox[5] - out->bbox[2]));
    tol = prc_write_tol_resolve(ctx, tolerance, diagonal);
    out->tolerance_mm = tol;

    if (num_positions > 0)
    {
        /* Fixed-capacity table: capacity is a power of two >= 2 * the maximum
           number of insertions, so the load factor stays <= 0.5 and linear
           probing always terminates without needing rehash/growth. */
        size_t vcap = prc_next_pow2((size_t)num_positions * 2);
        uint32_t ndedup = 0;

        vtable = (prc_vtx_slot *)prc_calloc(ctx, vcap, sizeof(prc_vtx_slot));
        remap = (uint32_t *)prc_malloc(ctx, (size_t)num_positions * sizeof(uint32_t));
        out->positions = (double *)prc_malloc(ctx, (size_t)num_positions * 3 * sizeof(double));
        if (vtable == NULL || remap == NULL || out->positions == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_preprocess vertex dedup\n");
            ret = PRC_ERROR_MEMORY;
            goto fail;
        }

        for (i = 0; i < num_positions; i++)
        {
            const double *p = positions + (size_t)i * 3;
            int64_t kx = (int64_t)llround(p[0] / tol);
            int64_t ky = (int64_t)llround(p[1] / tol);
            int64_t kz = (int64_t)llround(p[2] / tol);
            size_t slot = (size_t)(prc_vtx_hash(kx, ky, kz) & (uint64_t)(vcap - 1));

            for (;;)
            {
                prc_vtx_slot *s = &vtable[slot];
                if (!s->used)
                {
                    s->used = 1;
                    s->key[0] = kx;
                    s->key[1] = ky;
                    s->key[2] = kz;
                    s->index = ndedup;
                    memcpy(out->positions + (size_t)ndedup * 3, p, 3 * sizeof(double));
                    remap[i] = ndedup;
                    ndedup++;
                    break;
                }
                if (s->key[0] == kx && s->key[1] == ky && s->key[2] == kz)
                {
                    remap[i] = s->index;
                    break;
                }
                slot = (slot + 1) & (vcap - 1);
            }
        }
        out->num_positions = ndedup;
        if (ndedup < num_positions)
        {
            double *shrunk = (double *)prc_realloc(ctx, out->positions, (size_t)ndedup * 3 * sizeof(double));
            if (shrunk != NULL)
                out->positions = shrunk;
        }
        prc_free(ctx, vtable);
        vtable = NULL;
    }

    if (num_triangles > 0)
    {
        uint32_t removed = 0;

        out->tri_indices = (uint32_t *)prc_malloc(ctx, (size_t)num_triangles * 3 * sizeof(uint32_t));
        out->tri_orig_index = (uint32_t *)prc_malloc(ctx, (size_t)num_triangles * sizeof(uint32_t));
        if (out->tri_indices == NULL || out->tri_orig_index == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_preprocess triangle remap\n");
            ret = PRC_ERROR_MEMORY;
            goto fail;
        }

        for (i = 0; i < num_triangles; i++)
        {
            uint32_t a = remap[tri_indices[(size_t)i * 3 + 0]];
            uint32_t b = remap[tri_indices[(size_t)i * 3 + 1]];
            uint32_t c = remap[tri_indices[(size_t)i * 3 + 2]];
            if (a == b || b == c || a == c)
            {
                removed++;
                continue;
            }
            out->tri_indices[(size_t)clean_tris * 3 + 0] = a;
            out->tri_indices[(size_t)clean_tris * 3 + 1] = b;
            out->tri_indices[(size_t)clean_tris * 3 + 2] = c;
            out->tri_orig_index[clean_tris] = i;
            clean_tris++;
        }
        out->num_triangles = clean_tris;
        (void)removed;

        if (clean_tris == 0)
        {
            prc_free(ctx, out->tri_indices);
            out->tri_indices = NULL;
            prc_free(ctx, out->tri_orig_index);
            out->tri_orig_index = NULL;
        }
        else if (clean_tris < num_triangles)
        {
            uint32_t *shrunk = (uint32_t *)prc_realloc(ctx, out->tri_indices, (size_t)clean_tris * 3 * sizeof(uint32_t));
            uint32_t *shrunk_orig = (uint32_t *)prc_realloc(ctx, out->tri_orig_index, (size_t)clean_tris * sizeof(uint32_t));
            if (shrunk != NULL)
                out->tri_indices = shrunk;
            if (shrunk_orig != NULL)
                out->tri_orig_index = shrunk_orig;
        }
    }
    if (remap != NULL)
    {
        prc_free(ctx, remap);
        remap = NULL;
    }

    if (clean_tris > 0)
    {
        size_t max_edges = (size_t)clean_tris * 3;
        size_t ecap = prc_next_pow2(max_edges * 2);
        uint32_t nedges = 0;

        etable = (prc_edge_slot *)prc_calloc(ctx, ecap, sizeof(prc_edge_slot));
        out->edges = (prc_encode_edge *)prc_malloc(ctx, max_edges * sizeof(prc_encode_edge));
        if (etable == NULL || out->edges == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_preprocess edge adjacency\n");
            ret = PRC_ERROR_MEMORY;
            goto fail;
        }

        for (i = 0; i < clean_tris; i++)
        {
            uint32_t e;
            for (e = 0; e < 3; e++)
            {
                uint32_t a = out->tri_indices[(size_t)i * 3 + e];
                uint32_t b = out->tri_indices[(size_t)i * 3 + ((e + 1) % 3)];
                uint32_t v0 = a < b ? a : b;
                uint32_t v1 = a < b ? b : a;
                size_t slot = (size_t)(prc_mix64(((uint64_t)v0 << 32) | (uint64_t)v1) & (uint64_t)(ecap - 1));

                for (;;)
                {
                    prc_edge_slot *s = &etable[slot];
                    if (!s->used)
                    {
                        s->used = 1;
                        s->v0 = v0;
                        s->v1 = v1;
                        s->edge_index = nedges;
                        out->edges[nedges].v0 = v0;
                        out->edges[nedges].v1 = v1;
                        out->edges[nedges].tri0 = (int32_t)i;
                        out->edges[nedges].tri1 = -1;
                        nedges++;
                        break;
                    }
                    if (s->v0 == v0 && s->v1 == v1)
                    {
                        /* Non-manifold input (3+ triangles on one edge) is out
                           of scope; keep the first two adjacencies and ignore
                           the rest rather than failing. */
                        if (out->edges[s->edge_index].tri1 == -1)
                            out->edges[s->edge_index].tri1 = (int32_t)i;
                        break;
                    }
                    slot = (slot + 1) & (ecap - 1);
                }
            }
        }
        out->num_edges = nedges;
        if ((size_t)nedges < max_edges)
        {
            prc_encode_edge *shrunk = (prc_encode_edge *)prc_realloc(ctx, out->edges, (size_t)nedges * sizeof(prc_encode_edge));
            if (shrunk != NULL)
                out->edges = shrunk;
        }
        prc_free(ctx, etable);
        etable = NULL;
    }

    if (clean_tris > 0)
    {
        uint32_t ncomp = 0;

        parent = (uint32_t *)prc_malloc(ctx, (size_t)clean_tris * sizeof(uint32_t));
        label = (uint32_t *)prc_malloc(ctx, (size_t)clean_tris * sizeof(uint32_t));
        out->tri_component = (uint32_t *)prc_malloc(ctx, (size_t)clean_tris * sizeof(uint32_t));
        if (parent == NULL || label == NULL || out->tri_component == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_preprocess components\n");
            ret = PRC_ERROR_MEMORY;
            goto fail;
        }

        for (i = 0; i < clean_tris; i++)
        {
            parent[i] = i;
            label[i] = UINT32_MAX;
        }
        for (i = 0; i < out->num_edges; i++)
        {
            if (out->edges[i].tri1 != -1)
            {
                uint32_t ra = prc_uf_find(parent, (uint32_t)out->edges[i].tri0);
                uint32_t rb = prc_uf_find(parent, (uint32_t)out->edges[i].tri1);
                if (ra != rb)
                    parent[ra] = rb;
            }
        }
        for (i = 0; i < clean_tris; i++)
        {
            uint32_t root = prc_uf_find(parent, i);
            if (label[root] == UINT32_MAX)
            {
                label[root] = ncomp;
                ncomp++;
            }
            out->tri_component[i] = label[root];
        }
        out->num_components = ncomp;

        prc_free(ctx, parent);
        parent = NULL;
        prc_free(ctx, label);
        label = NULL;
    }

    return 0;

fail:
    if (vtable != NULL)
        prc_free(ctx, vtable);
    if (etable != NULL)
        prc_free(ctx, etable);
    if (remap != NULL)
        prc_free(ctx, remap);
    if (parent != NULL)
        prc_free(ctx, parent);
    if (label != NULL)
        prc_free(ctx, label);
    prc_encode_preprocess_free(ctx, out);
    return ret;
}

/* Depth-first traversal (Step B): emits the compressed-tessellation arrays by
   simulating, in lockstep, exactly what prc_decode_compressed_tess will do
   when it reads them back. All predictions (chain-start deltas, edge bases,
   averages) are computed from the decoder-visible RECONSTRUCTED positions --
   not the original input positions -- using the same prc_vec_* helpers in the
   same order, so encoder and decoder stay bit-for-bit synchronized. */

#define PRC_ENCODE_MAX_CHAIN 65536

typedef struct
{
    prc_vec3 x_basis, y_basis, z_basis;
    prc_vec3 origin;
    int32_t index0, index1;   /* decoder point indices of the edge, index0 < index1 */
    uint32_t mesh_v0, mesh_v1; /* mesh vertex ids aligned with index0/index1 */
    uint32_t target_tri;      /* mesh triangle to grow across this edge */
} prc_encode_grow_op;

typedef struct
{
    prc_context *ctx;
    const prc_encode_mesh *mesh;
    double tol;
    prc_vec3 origin;
    int32_t *vtx_map;         /* mesh vertex -> decoder point index, -1 unseen */
    prc_vec3 *decoded_pos;    /* decoder-exact reconstructed positions */
    uint32_t n_points;
    uint8_t *visited;
    uint8_t *pending;         /* triangle has a live grow op on the stack */
    int32_t *neighbor;        /* 3 per triangle (local edge slots), -1 = boundary */
    /* Explicit heap stack for the depth-first growth (mesh-size-driven depth
       must never use C call-stack recursion). */
    prc_encode_grow_op *stack;
    uint32_t stack_size;
    uint32_t stack_capacity;
    uint32_t chain_len;
    uint32_t current_chain;   /* 0-based id of the chain being grown */
    uint32_t chain_offset;    /* points created so far within the current chain */
    prc_vertex_analysis *analysis; /* NULL unless the caller requested capture */
    prc_encode_traversal_result *out;
} prc_encode_state;

/* Chain bookkeeping only this phase: reconstructed_position stays zeroed
   until the reconstruction pass lands. Must run before n_points advances
   (the entry index is the point being created). */
static void
prc_encode_record_analysis(prc_encode_state *st, uint32_t mesh_vtx)
{
    if (st->analysis != NULL)
    {
        prc_vertex_analysis *a = &st->analysis[st->n_points];
        const double *p = st->mesh->positions + (size_t)mesh_vtx * 3;

        a->original_position[0] = (float)p[0];
        a->original_position[1] = (float)p[1];
        a->original_position[2] = (float)p[2];
        a->reconstructed_position[0] = 0.0f;
        a->reconstructed_position[1] = 0.0f;
        a->reconstructed_position[2] = 0.0f;
        a->chain_index = st->current_chain;
        a->chain_offset = st->chain_offset;
    }
    st->chain_offset++;
}

static int
prc_encode_quantize(prc_context *ctx, double v, double tol, int32_t *dv)
{
    long long q = llround(v / tol);

    if (q > (long long)INT32_MAX || q < (long long)INT32_MIN)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: delta exceeds int32 range\n");
        return PRC_ERROR_INTERNAL;
    }
    *dv = (int32_t)q;
    return 0;
}

/* Emit a new point predicted along the global axes: DV = round((P - base)/tol),
   reconstructed exactly like the decoder's prc_vec_add(point_array_scaled, base). */
static int
prc_encode_emit_axis_point(prc_encode_state *st, uint32_t mesh_vtx, prc_vec3 base,
    int32_t *out_index)
{
    const double *p = st->mesh->positions + (size_t)mesh_vtx * 3;
    prc_vec3 scaled, rec;
    int32_t dv[3];
    int code;

    code = prc_encode_quantize(st->ctx, p[0] - base.x, st->tol, &dv[0]);
    if (code < 0)
        return code;
    code = prc_encode_quantize(st->ctx, p[1] - base.y, st->tol, &dv[1]);
    if (code < 0)
        return code;
    code = prc_encode_quantize(st->ctx, p[2] - base.z, st->tol, &dv[2]);
    if (code < 0)
        return code;

    st->out->point_array[st->out->point_array_size + 0] = dv[0];
    st->out->point_array[st->out->point_array_size + 1] = dv[1];
    st->out->point_array[st->out->point_array_size + 2] = dv[2];
    st->out->point_array_size += 3;

    scaled.x = ((double)dv[0]) * st->tol;
    scaled.y = ((double)dv[1]) * st->tol;
    scaled.z = ((double)dv[2]) * st->tol;
    prc_vec_add(scaled, base, &rec);

    st->decoded_pos[st->n_points] = rec;
    st->vtx_map[mesh_vtx] = (int32_t)st->n_points;
    *out_index = (int32_t)st->n_points;
    prc_encode_record_analysis(st, mesh_vtx);
    st->n_points++;
    return 0;
}

/* Emit a new point predicted in a grow op's orthonormal edge basis; the
   reconstruction mirrors prc_decode_next_point_post_scale exactly. */
static int
prc_encode_emit_basis_point(prc_encode_state *st, uint32_t mesh_vtx,
    const prc_encode_grow_op *op, int32_t *out_index)
{
    const double *p = st->mesh->positions + (size_t)mesh_vtx * 3;
    prc_vec3 diff, x, y, z, temp, temp2, rec;
    int32_t dv[3];
    int code;

    diff.x = p[0] - op->origin.x;
    diff.y = p[1] - op->origin.y;
    diff.z = p[2] - op->origin.z;

    code = prc_encode_quantize(st->ctx, prc_vec_dot_product(diff, op->x_basis), st->tol, &dv[0]);
    if (code < 0)
        return code;
    code = prc_encode_quantize(st->ctx, prc_vec_dot_product(diff, op->y_basis), st->tol, &dv[1]);
    if (code < 0)
        return code;
    code = prc_encode_quantize(st->ctx, prc_vec_dot_product(diff, op->z_basis), st->tol, &dv[2]);
    if (code < 0)
        return code;

    st->out->point_array[st->out->point_array_size + 0] = dv[0];
    st->out->point_array[st->out->point_array_size + 1] = dv[1];
    st->out->point_array[st->out->point_array_size + 2] = dv[2];
    st->out->point_array_size += 3;

    x = op->x_basis;
    x.x = ((double)dv[0]) * st->tol * x.x;
    x.y = ((double)dv[0]) * st->tol * x.y;
    x.z = ((double)dv[0]) * st->tol * x.z;
    y = op->y_basis;
    y.x = ((double)dv[1]) * st->tol * y.x;
    y.y = ((double)dv[1]) * st->tol * y.y;
    y.z = ((double)dv[1]) * st->tol * y.z;
    z = op->z_basis;
    z.x = ((double)dv[2]) * st->tol * z.x;
    z.y = ((double)dv[2]) * st->tol * z.y;
    z.z = ((double)dv[2]) * st->tol * z.z;
    prc_vec_add(op->origin, x, &temp);
    prc_vec_add(temp, y, &temp2);
    prc_vec_add(temp2, z, &rec);

    st->decoded_pos[st->n_points] = rec;
    st->vtx_map[mesh_vtx] = (int32_t)st->n_points;
    *out_index = (int32_t)st->n_points;
    prc_encode_record_analysis(st, mesh_vtx);
    st->n_points++;
    return 0;
}

/* Re-derivation of the decoder's prc_compute_triangle_basis: the decoder
   recomputes this basis from its reconstructed vertices when it pops a grow
   op, so the encoder must reproduce it exactly, including the
   prc_vec_make_orth_basis fallback and the unchecked final y-normalize.
   prc_vec_compute_basis_origin looks similar but uses the opposite X sign
   convention (v1-v0) and lacks the y-dot-w flip, so it cannot be used here. */
static int
prc_encode_edge_basis(prc_vec3 E0, prc_vec3 E1, prc_vec3 E3,
    prc_vec3 *x_out, prc_vec3 *y_out, prc_vec3 *z_out, prc_vec3 *origin_out)
{
    prc_vec3 x, z, z_temp, w, origin;
    prc_vec3 y = { 0.0, 0.0, 0.0 }; /* always overwritten before use; silences a
                                       false-positive uninitialized-use warning
                                       the compiler can't resolve across the
                                       use_alternate_basis branches */
    prc_basis basis;
    int code;
    uint8_t use_alternate_basis = 0;

    prc_vec_avg(E0, E1, &origin);
    prc_vec_sub(E0, E1, &x);
    code = prc_vec_normalize(&x);
    if (code < 0)
        return code;

    prc_vec_sub(E3, origin, &z_temp);
    prc_vec_cross(z_temp, x, &z);
    code = prc_vec_normalize(&z);
    if (code < 0)
        use_alternate_basis = 1;

    if (!use_alternate_basis)
    {
        prc_vec_cross(z, x, &y);
        code = prc_vec_normalize(&y);
        if (code < 0)
            use_alternate_basis = 1;
    }

    if (use_alternate_basis)
    {
        basis.X = x;
        code = prc_vec_make_orth_basis(&basis);
        if (code < 0)
            return code;
        y = basis.Y;
        z = basis.Z;
        prc_vec_cross(z, x, &y);
        /* the decoder ignores this normalize result; mirror that */
        (void)prc_vec_normalize(&y);
    }

    prc_vec_sub(origin, E3, &w);
    if (prc_vec_dot_product(y, w) > 0.0)
    {
        prc_vec_negate(&z);
        prc_vec_negate(&y);
    }

    *x_out = x;
    *y_out = y;
    *z_out = z;
    *origin_out = origin;
    return 0;
}

static int32_t
prc_encode_local_edge_slot(const prc_encode_mesh *mesh, uint32_t tri,
    uint32_t va, uint32_t vb)
{
    uint32_t e;

    for (e = 0; e < 3; e++)
    {
        uint32_t a = mesh->tri_indices[(size_t)tri * 3 + e];
        uint32_t b = mesh->tri_indices[(size_t)tri * 3 + (e + 1) % 3];
        if ((a == va && b == vb) || (a == vb && b == va))
            return (int32_t)e;
    }
    return -1;
}

static int
prc_encode_stack_push(prc_encode_state *st, const prc_encode_grow_op *op)
{
    if (st->stack_size == st->stack_capacity)
    {
        uint32_t new_cap = st->stack_capacity ? st->stack_capacity * 2 : 64;
        prc_encode_grow_op *grown = (prc_encode_grow_op *)prc_realloc(st->ctx,
            st->stack, (size_t)new_cap * sizeof(prc_encode_grow_op));
        if (grown == NULL)
        {
            prc_error(st->ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_traversal stack\n");
            return PRC_ERROR_MEMORY;
        }
        st->stack = grown;
        st->stack_capacity = new_cap;
    }
    st->stack[st->stack_size] = *op;
    st->stack_size++;
    return 0;
}

/* Start a fresh chain at triangle tri (the decoder's empty-stack path).
   Vertex order is kept exactly as given in mesh->tri_indices; already-emitted
   vertices become references so shared vertices are never duplicated. */
static int
prc_encode_chain_start(prc_encode_state *st, uint32_t tri,
    int32_t idx[3], uint32_t mv[3])
{
    prc_encode_traversal_result *out = st->out;
    uint8_t r[3];
    uint32_t num_refs, k;
    prc_vec3 temp;
    int code;

    for (k = 0; k < 3; k++)
        mv[k] = st->mesh->tri_indices[(size_t)tri * 3 + k];
    num_refs = 0;
    for (k = 0; k < 3; k++)
    {
        r[k] = (uint8_t)(st->vtx_map[mv[k]] >= 0 ? 1 : 0);
        num_refs += r[k];
        out->points_is_reference_array[out->points_is_reference_array_size] = r[k];
        out->points_is_reference_array_size++;
    }

    if (num_refs == 0)
    {
        /* Mirrors prc_compute_first_triangle */
        code = prc_encode_emit_axis_point(st, mv[0], st->origin, &idx[0]);
        if (code < 0)
            return code;
        code = prc_encode_emit_axis_point(st, mv[1], st->decoded_pos[idx[0]], &idx[1]);
        if (code < 0)
            return code;
        prc_vec_avg(st->decoded_pos[idx[0]], st->decoded_pos[idx[1]], &temp);
        code = prc_encode_emit_axis_point(st, mv[2], temp, &idx[2]);
        if (code < 0)
            return code;
    }
    else if (num_refs == 1)
    {
        /* Mirrors prc_set_one_ref_treated_triangle */
        if (r[0])
        {
            idx[0] = st->vtx_map[mv[0]];
            out->point_reference_array[out->point_reference_array_size++] = idx[0];
            code = prc_encode_emit_axis_point(st, mv[1], st->decoded_pos[idx[0]], &idx[1]);
            if (code < 0)
                return code;
            prc_vec_avg(st->decoded_pos[idx[0]], st->decoded_pos[idx[1]], &temp);
            code = prc_encode_emit_axis_point(st, mv[2], temp, &idx[2]);
            if (code < 0)
                return code;
        }
        else if (r[1])
        {
            idx[1] = st->vtx_map[mv[1]];
            out->point_reference_array[out->point_reference_array_size++] = idx[1];
            code = prc_encode_emit_axis_point(st, mv[0], st->origin, &idx[0]);
            if (code < 0)
                return code;
            prc_vec_avg(st->decoded_pos[idx[1]], st->decoded_pos[idx[0]], &temp);
            code = prc_encode_emit_axis_point(st, mv[2], temp, &idx[2]);
            if (code < 0)
                return code;
        }
        else
        {
            idx[2] = st->vtx_map[mv[2]];
            out->point_reference_array[out->point_reference_array_size++] = idx[2];
            code = prc_encode_emit_axis_point(st, mv[0], st->origin, &idx[0]);
            if (code < 0)
                return code;
            code = prc_encode_emit_axis_point(st, mv[1], st->decoded_pos[idx[0]], &idx[1]);
            if (code < 0)
                return code;
        }
    }
    else if (num_refs == 2)
    {
        /* Mirrors prc_set_two_ref_treated_triangle */
        if (!r[0])
        {
            idx[1] = st->vtx_map[mv[1]];
            idx[2] = st->vtx_map[mv[2]];
            out->point_reference_array[out->point_reference_array_size++] = idx[1];
            out->point_reference_array[out->point_reference_array_size++] = idx[2];
            code = prc_encode_emit_axis_point(st, mv[0], st->origin, &idx[0]);
            if (code < 0)
                return code;
        }
        else if (!r[1])
        {
            idx[0] = st->vtx_map[mv[0]];
            idx[2] = st->vtx_map[mv[2]];
            out->point_reference_array[out->point_reference_array_size++] = idx[0];
            out->point_reference_array[out->point_reference_array_size++] = idx[2];
            code = prc_encode_emit_axis_point(st, mv[1], st->decoded_pos[idx[0]], &idx[1]);
            if (code < 0)
                return code;
        }
        else
        {
            idx[0] = st->vtx_map[mv[0]];
            idx[1] = st->vtx_map[mv[1]];
            out->point_reference_array[out->point_reference_array_size++] = idx[0];
            out->point_reference_array[out->point_reference_array_size++] = idx[1];
            prc_vec_avg(st->decoded_pos[idx[0]], st->decoded_pos[idx[1]], &temp);
            code = prc_encode_emit_axis_point(st, mv[2], temp, &idx[2]);
            if (code < 0)
                return code;
        }
    }
    else
    {
        /* Mirrors prc_set_three_ref_treated_triangle */
        for (k = 0; k < 3; k++)
        {
            idx[k] = st->vtx_map[mv[k]];
            out->point_reference_array[out->point_reference_array_size++] = idx[k];
        }
    }

    /* Mirror prc_handle_empty_stack_decode's final index0 > index1 swap so the
       left/right edge bookkeeping below matches the decoder's view. */
    if (idx[0] > idx[1])
    {
        int32_t ti = idx[0];
        uint32_t tm = mv[0];
        idx[0] = idx[1];
        idx[1] = ti;
        mv[0] = mv[1];
        mv[1] = tm;
    }
    return 0;
}

/* Grow the popped op's target triangle: new triangle is (V0, V1, third). */
static int
prc_encode_grow_triangle(prc_encode_state *st, const prc_encode_grow_op *op,
    int32_t idx[3], uint32_t mv[3])
{
    prc_encode_traversal_result *out = st->out;
    uint32_t t = op->target_tri;
    uint32_t third = UINT32_MAX;
    uint32_t k;
    int code;

    for (k = 0; k < 3; k++)
    {
        uint32_t v = st->mesh->tri_indices[(size_t)t * 3 + k];
        if (v != op->mesh_v0 && v != op->mesh_v1)
            third = v;
    }
    if (third == UINT32_MAX)
    {
        prc_error(st->ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: grow target has no apex vertex\n");
        return PRC_ERROR_INTERNAL;
    }

    if (st->vtx_map[third] >= 0)
    {
        out->points_is_reference_array[out->points_is_reference_array_size++] = 1;
        idx[2] = st->vtx_map[third];
        out->point_reference_array[out->point_reference_array_size++] = idx[2];
    }
    else
    {
        out->points_is_reference_array[out->points_is_reference_array_size++] = 0;
        code = prc_encode_emit_basis_point(st, third, op, &idx[2]);
        if (code < 0)
            return code;
    }

    idx[0] = op->index0;
    idx[1] = op->index1;
    mv[0] = op->mesh_v0;
    mv[1] = op->mesh_v1;
    mv[2] = third;
    return 0;
}

/* Decide the just-emitted triangle's edge status bits and push its growable
   edges (right first, then left -- the decoder's LIFO push order).

   Scope decision (traversal-only session): normals are not encoded yet, so
   every triangle is treated as normal_was_reversed == 0 and only the
   un-swapped branch of the decoder's prc_set_left_right_edge_indices applies.

   An edge is growable only if its neighbor exists, is unvisited AND has no
   grow op already pending on the stack. Never re-pushing a pending triangle
   means a popped op's target is always fresh, so the decoder never needs its
   treated-edge discard search and both sides pop in exact lockstep. */
static int
prc_encode_edge_status(prc_encode_state *st, uint32_t tri,
    const int32_t idx[3], const uint32_t mv[3], uint8_t *status_out)
{
    uint8_t bits[2];
    uint32_t e;
    /* [0] = right edge (idx1, idx2, apex idx0), [1] = left edge (idx0, idx2, apex idx1) */
    int32_t ex[2], ey[2], ez[2];
    uint32_t ma[2], mb[2];
    int code;

    ex[0] = idx[1];
    ey[0] = idx[2];
    ez[0] = idx[0];
    ma[0] = mv[1];
    mb[0] = mv[2];

    ex[1] = idx[0];
    ey[1] = idx[2];
    ez[1] = idx[1];
    ma[1] = mv[0];
    mb[1] = mv[2];

    for (e = 0; e < 2; e++)
    {
        int32_t slot, nb;
        uint8_t growable;

        if (ex[e] > ey[e])
        {
            int32_t ti = ex[e];
            uint32_t tm = ma[e];
            ex[e] = ey[e];
            ey[e] = ti;
            ma[e] = mb[e];
            mb[e] = tm;
        }

        slot = prc_encode_local_edge_slot(st->mesh, tri, ma[e], mb[e]);
        nb = slot >= 0 ? st->neighbor[(size_t)tri * 3 + (uint32_t)slot] : -1;
        /* The 64K chain cap counts already-emitted chain triangles plus the
           pending grow ops (each adds exactly one more triangle to this
           chain). Once the budget is spent we simply stop declaring growable
           edges and let the decoder's stack drain naturally -- abandoning
           live stack entries is not an option because the decoder would still
           pop them; unreached neighbors are picked up later by the outer
           unvisited-triangle scan as fresh chain starts. */
        growable = (uint8_t)(nb >= 0 &&
            !st->visited[nb] && !st->pending[nb] &&
            st->chain_len + st->stack_size < PRC_ENCODE_MAX_CHAIN);

        if (growable)
        {
            prc_encode_grow_op op;

            code = prc_encode_edge_basis(st->decoded_pos[ex[e]],
                st->decoded_pos[ey[e]], st->decoded_pos[ez[e]],
                &op.x_basis, &op.y_basis, &op.z_basis, &op.origin);
            if (code < 0)
            {
                /* Degenerate edge: the decoder would fail computing this
                   basis, so leave the edge un-grown; the neighbor is reached
                   later as its own chain start. */
                growable = 0;
            }
            else
            {
                op.index0 = ex[e];
                op.index1 = ey[e];
                op.mesh_v0 = ma[e];
                op.mesh_v1 = mb[e];
                op.target_tri = (uint32_t)nb;
                code = prc_encode_stack_push(st, &op);
                if (code < 0)
                    return code;
                st->pending[nb] = 1;
            }
        }
        bits[e] = growable;
    }

    *status_out = (uint8_t)((bits[0] ? 1 : 0) | (bits[1] ? 2 : 0));
    return 0;
}

int
prc_encode_traversal(prc_context *ctx, const prc_encode_mesh *mesh,
    const uint32_t *face_indices, double tolerance_mm,
    prc_encode_traversal_result *out,
    prc_vertex_analysis **analysis_out, uint32_t *analysis_count_out)
{
    prc_encode_state st;
    uint32_t i, num_tris, num_pos;
    uint32_t emitted = 0;
    uint32_t scan_pos = 0;
    int ret = PRC_ERROR_INTERNAL;
    int code;

    if (out == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: NULL output\n");
        return PRC_ERROR_INTERNAL;
    }
    memset(out, 0, sizeof(*out));
    memset(&st, 0, sizeof(st));

    if (analysis_out != NULL)
        *analysis_out = NULL;
    if (analysis_count_out != NULL)
        *analysis_count_out = 0;

    if (mesh == NULL || !(tolerance_mm > 0.0))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: bad mesh/tolerance\n");
        return PRC_ERROR_INTERNAL;
    }

    /* One global origin (the decoder's origin_array): the bbox min corner,
       used by every chain start and one/two-ref branch. */
    out->origin[0] = mesh->bbox[0];
    out->origin[1] = mesh->bbox[1];
    out->origin[2] = mesh->bbox[2];

    num_tris = mesh->num_triangles;
    num_pos = mesh->num_positions;
    if (num_tris == 0)
        return 0;

    st.ctx = ctx;
    st.mesh = mesh;
    st.tol = tolerance_mm;
    st.origin.x = out->origin[0];
    st.origin.y = out->origin[1];
    st.origin.z = out->origin[2];
    st.out = out;

    /* Every emitted point is a distinct deduplicated mesh vertex, so
       num_pos triples bounds point_array; 3 reference-bit slots and 3
       reference entries per triangle bound the other variable arrays. */
    out->point_array = (int32_t *)prc_malloc(ctx, (size_t)num_pos * 3 * sizeof(int32_t));
    out->edge_status_array = (uint8_t *)prc_malloc(ctx, (size_t)num_tris * sizeof(uint8_t));
    out->triangle_face_array = (int32_t *)prc_malloc(ctx, (size_t)num_tris * sizeof(int32_t));
    out->points_is_reference_array = (uint8_t *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(uint8_t));
    out->point_reference_array = (int32_t *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(int32_t));
    out->triangle_point_indices = (int32_t *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(int32_t));
    out->triangle_mesh_order = (uint32_t *)prc_malloc(ctx, (size_t)num_tris * sizeof(uint32_t));
    out->point_mesh_vertex = (int32_t *)prc_malloc(ctx, (size_t)num_pos * sizeof(int32_t));
    out->decoded_positions = (double *)prc_malloc(ctx, (size_t)num_pos * 3 * sizeof(double));
    st.visited = (uint8_t *)prc_calloc(ctx, num_tris, sizeof(uint8_t));
    st.pending = (uint8_t *)prc_calloc(ctx, num_tris, sizeof(uint8_t));
    st.neighbor = (int32_t *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(int32_t));
    st.vtx_map = (int32_t *)prc_malloc(ctx, (size_t)num_pos * sizeof(int32_t));
    st.decoded_pos = (prc_vec3 *)prc_malloc(ctx, (size_t)num_pos * sizeof(prc_vec3));
    if (out->point_array == NULL || out->edge_status_array == NULL ||
        out->triangle_face_array == NULL || out->points_is_reference_array == NULL ||
        out->point_reference_array == NULL || out->triangle_point_indices == NULL ||
        out->triangle_mesh_order == NULL || out->point_mesh_vertex == NULL ||
        out->decoded_positions == NULL || st.visited == NULL ||
        st.pending == NULL || st.neighbor == NULL || st.vtx_map == NULL ||
        st.decoded_pos == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_traversal\n");
        ret = PRC_ERROR_MEMORY;
        goto fail;
    }
    if (analysis_out != NULL)
    {
        /* Same num_pos upper bound as decoded_pos: one entry per point the
           decoder will actually emit, shrunk to n_points on success. */
        st.analysis = (prc_vertex_analysis *)prc_malloc(ctx,
            (size_t)num_pos * sizeof(prc_vertex_analysis));
        if (st.analysis == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_traversal analysis\n");
            ret = PRC_ERROR_MEMORY;
            goto fail;
        }
    }

    for (i = 0; i < num_tris * 3; i++)
        st.neighbor[i] = -1;
    for (i = 0; i < num_pos; i++)
        st.vtx_map[i] = -1;
    for (i = 0; i < mesh->num_edges; i++)
    {
        const prc_encode_edge *edge = &mesh->edges[i];
        int32_t s0, s1;

        if (edge->tri1 == -1)
            continue;
        s0 = prc_encode_local_edge_slot(mesh, (uint32_t)edge->tri0, edge->v0, edge->v1);
        s1 = prc_encode_local_edge_slot(mesh, (uint32_t)edge->tri1, edge->v0, edge->v1);
        if (s0 < 0 || s1 < 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: adjacency/index mismatch\n");
            goto fail;
        }
        st.neighbor[(size_t)edge->tri0 * 3 + (uint32_t)s0] = edge->tri1;
        st.neighbor[(size_t)edge->tri1 * 3 + (uint32_t)s1] = edge->tri0;
    }

    while (emitted < num_tris)
    {
        int32_t idx[3];
        uint32_t mv[3];
        uint32_t cur;

        if (st.stack_size > 0)
        {
            prc_encode_grow_op op;

            st.stack_size--;
            op = st.stack[st.stack_size];
            if (st.visited[op.target_tri])
            {
                /* Cannot happen: growable edges are never declared toward
                   visited or pending triangles, so popped targets are fresh. */
                prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: popped a visited target\n");
                goto fail;
            }
            code = prc_encode_grow_triangle(&st, &op, idx, mv);
            if (code < 0)
            {
                ret = code;
                goto fail;
            }
            cur = op.target_tri;
            st.visited[cur] = 1;
            st.pending[cur] = 0;
            st.chain_len++;
        }
        else
        {
            while (scan_pos < num_tris && st.visited[scan_pos])
                scan_pos++;
            if (scan_pos >= num_tris)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_traversal: no unvisited triangle left\n");
                goto fail;
            }
            cur = scan_pos;
            /* Both the first chain and every restart pass through here;
               emitted == 0 distinguishes the first so chain ids start at 0. */
            if (emitted > 0)
                st.current_chain++;
            st.chain_offset = 0;
            code = prc_encode_chain_start(&st, cur, idx, mv);
            if (code < 0)
            {
                ret = code;
                goto fail;
            }
            st.visited[cur] = 1;
            st.chain_len = 1;
        }

        /* Face ids are a pure side channel reordered into traversal order;
           without caller-provided ids every triangle maps to face 0. */
        out->triangle_face_array[emitted] = face_indices != NULL ? (int32_t)face_indices[cur] : 0;

        out->triangle_point_indices[(size_t)emitted * 3 + 0] = idx[0];
        out->triangle_point_indices[(size_t)emitted * 3 + 1] = idx[1];
        out->triangle_point_indices[(size_t)emitted * 3 + 2] = idx[2];
        out->triangle_mesh_order[emitted] = cur;

        code = prc_encode_edge_status(&st, cur, idx, mv, &out->edge_status_array[emitted]);
        if (code < 0)
        {
            ret = code;
            goto fail;
        }
        emitted++;
    }

    out->edge_status_array_size = num_tris;
    out->triangle_face_array_size = num_tris;

    out->num_decoded_points = st.n_points;
    for (i = 0; i < num_pos; i++)
        out->point_mesh_vertex[i] = -1;
    for (i = 0; i < num_pos; i++)
    {
        if (st.vtx_map[i] >= 0)
            out->point_mesh_vertex[st.vtx_map[i]] = (int32_t)i;
    }
    for (i = 0; i < st.n_points; i++)
    {
        out->decoded_positions[(size_t)i * 3 + 0] = st.decoded_pos[i].x;
        out->decoded_positions[(size_t)i * 3 + 1] = st.decoded_pos[i].y;
        out->decoded_positions[(size_t)i * 3 + 2] = st.decoded_pos[i].z;
    }

    if (out->point_array_size < num_pos * 3)
    {
        if (out->point_array_size == 0)
        {
            prc_free(ctx, out->point_array);
            out->point_array = NULL;
        }
        else
        {
            int32_t *shrunk = (int32_t *)prc_realloc(ctx, out->point_array,
                (size_t)out->point_array_size * sizeof(int32_t));
            if (shrunk != NULL)
                out->point_array = shrunk;
        }
    }
    if (out->points_is_reference_array_size < num_tris * 3)
    {
        uint8_t *shrunk = (uint8_t *)prc_realloc(ctx, out->points_is_reference_array,
            (size_t)out->points_is_reference_array_size * sizeof(uint8_t));
        if (shrunk != NULL)
            out->points_is_reference_array = shrunk;
    }
    if (out->point_reference_array_size < num_tris * 3)
    {
        if (out->point_reference_array_size == 0)
        {
            prc_free(ctx, out->point_reference_array);
            out->point_reference_array = NULL;
        }
        else
        {
            int32_t *shrunk = (int32_t *)prc_realloc(ctx, out->point_reference_array,
                (size_t)out->point_reference_array_size * sizeof(int32_t));
            if (shrunk != NULL)
                out->point_reference_array = shrunk;
        }
    }

    if (analysis_out != NULL)
    {
        if (st.n_points > 0 && st.n_points < num_pos)
        {
            prc_vertex_analysis *shrunk = (prc_vertex_analysis *)prc_realloc(ctx,
                st.analysis, (size_t)st.n_points * sizeof(prc_vertex_analysis));
            if (shrunk != NULL)
                st.analysis = shrunk;
        }
        *analysis_out = st.analysis;
        st.analysis = NULL;
        if (analysis_count_out != NULL)
            *analysis_count_out = out->num_decoded_points;
    }

    ret = 0;
    goto cleanup;

fail:
    prc_encode_traversal_free(ctx, out);

cleanup:
    if (st.analysis != NULL)
        prc_free(ctx, st.analysis);
    if (st.visited != NULL)
        prc_free(ctx, st.visited);
    if (st.pending != NULL)
        prc_free(ctx, st.pending);
    if (st.neighbor != NULL)
        prc_free(ctx, st.neighbor);
    if (st.vtx_map != NULL)
        prc_free(ctx, st.vtx_map);
    if (st.decoded_pos != NULL)
        prc_free(ctx, st.decoded_pos);
    if (st.stack != NULL)
        prc_free(ctx, st.stack);
    return ret;
}

void
prc_encode_traversal_free(prc_context *ctx, prc_encode_traversal_result *out)
{
    if (out == NULL)
        return;
    if (out->point_array != NULL)
        prc_free(ctx, out->point_array);
    if (out->edge_status_array != NULL)
        prc_free(ctx, out->edge_status_array);
    if (out->triangle_face_array != NULL)
        prc_free(ctx, out->triangle_face_array);
    if (out->points_is_reference_array != NULL)
        prc_free(ctx, out->points_is_reference_array);
    if (out->point_reference_array != NULL)
        prc_free(ctx, out->point_reference_array);
    if (out->triangle_point_indices != NULL)
        prc_free(ctx, out->triangle_point_indices);
    if (out->triangle_mesh_order != NULL)
        prc_free(ctx, out->triangle_mesh_order);
    if (out->point_mesh_vertex != NULL)
        prc_free(ctx, out->point_mesh_vertex);
    if (out->decoded_positions != NULL)
        prc_free(ctx, out->decoded_positions);
    memset(out, 0, sizeof(*out));
}

/* ---- Step C: normal encoding ------------------------------------------ */

#define PRC_ENCODE_NORMAL_ANGLE_BITS 10
#define PRC_ENCODE_NORMAL_ANGLE_MAX ((1 << PRC_ENCODE_NORMAL_ANGLE_BITS) - 1)

typedef struct
{
    int32_t theta_q;
    int32_t phi_q;
    uint8_t tri_reversed;
    uint8_t x_reversed;
    uint8_t y_reversed;
} prc_encode_normal_tuple;

typedef struct
{
    uint8_t state;            /* prc_vertex_norm_case_t values */
    uint8_t all_same;
    uint8_t has_first;
    prc_vec3 first_normal;
    prc_vec3 single_decoded;
    uint32_t num_stored;
    uint32_t cap;
    prc_vec3 *slot_input;     /* input normal that created each stored slot */
    prc_vec3 *slot_decoded;   /* decoder-visible vector each slot resolves to */
} prc_encode_point_norm;

static prc_vec3
prc_encode_decoded_vec(const prc_encode_traversal_result *trav, int32_t point_index)
{
    prc_vec3 v;

    v.x = trav->decoded_positions[(size_t)point_index * 3 + 0];
    v.y = trav->decoded_positions[(size_t)point_index * 3 + 1];
    v.z = trav->decoded_positions[(size_t)point_index * 3 + 2];
    return v;
}

static int
prc_encode_check_trav_arrays(prc_context *ctx, const prc_encode_mesh *mesh,
    const prc_encode_traversal_result *trav)
{
    uint32_t k, num_tris;

    if (mesh == NULL || trav == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "normal encoding: NULL mesh/traversal\n");
        return PRC_ERROR_INTERNAL;
    }
    num_tris = trav->edge_status_array_size;
    if (num_tris == 0)
        return 0;
    if (trav->triangle_point_indices == NULL || trav->triangle_mesh_order == NULL ||
        trav->point_mesh_vertex == NULL || trav->decoded_positions == NULL ||
        trav->edge_status_array == NULL || trav->num_decoded_points == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "normal encoding: incomplete traversal result\n");
        return PRC_ERROR_INTERNAL;
    }
    /* edge_status_array_size / num_decoded_points / mesh counts are
       independently supplied; validate every cross-index before any of the
       encoding passes below dereferences through them. */
    for (k = 0; k < num_tris * 3; k++)
    {
        int32_t pt = trav->triangle_point_indices[k];
        int32_t mv;

        if (pt < 0 || (uint32_t)pt >= trav->num_decoded_points)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "normal encoding: point index out of range\n");
            return PRC_ERROR_INTERNAL;
        }
        mv = trav->point_mesh_vertex[pt];
        if (mv < 0 || (uint32_t)mv >= mesh->num_positions)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "normal encoding: mesh vertex out of range\n");
            return PRC_ERROR_INTERNAL;
        }
    }
    for (k = 0; k < num_tris; k++)
    {
        if (trav->triangle_mesh_order[k] >= mesh->num_triangles)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "normal encoding: mesh triangle out of range\n");
            return PRC_ERROR_INTERNAL;
        }
    }
    return 0;
}

int
prc_encode_normals_c1(prc_context *ctx, const prc_encode_mesh *mesh,
    const prc_encode_traversal_result *trav, const double *input_normals,
    uint8_t **normal_is_reversed_out)
{
    uint8_t *rev;
    uint32_t k, num_tris;
    int code;

    if (normal_is_reversed_out == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_normals_c1: NULL output\n");
        return PRC_ERROR_INTERNAL;
    }
    *normal_is_reversed_out = NULL;
    code = prc_encode_check_trav_arrays(ctx, mesh, trav);
    if (code < 0)
        return code;
    num_tris = trav->edge_status_array_size;
    if (num_tris == 0)
        return 0;

    rev = (uint8_t *)prc_calloc(ctx, num_tris, sizeof(uint8_t));
    if (rev == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_normals_c1\n");
        return PRC_ERROR_MEMORY;
    }

    if (input_normals != NULL)
    {
        for (k = 0; k < num_tris; k++)
        {
            const int32_t *idx = &trav->triangle_point_indices[(size_t)k * 3];
            prc_vec3 P0 = prc_encode_decoded_vec(trav, idx[0]);
            prc_vec3 P1 = prc_encode_decoded_vec(trav, idx[1]);
            prc_vec3 P2 = prc_encode_decoded_vec(trav, idx[2]);
            prc_vec3 e1, e2, raw_cross, avg;
            uint32_t c;
            double dot_val;

            prc_vec_sub(P1, P0, &e1);
            prc_vec_sub(P2, P0, &e2);
            prc_vec_cross(e1, e2, &raw_cross);

            avg.x = avg.y = avg.z = 0.0;
            for (c = 0; c < 3; c++)
            {
                const double *n = &input_normals[(size_t)trav->point_mesh_vertex[idx[c]] * 3];

                avg.x += n[0] / 3.0;
                avg.y += n[1] / 3.0;
                avg.z += n[2] / 3.0;
            }
            dot_val = prc_vec_dot_product(avg, raw_cross);
            /* prc_derive_normal negates its cross product when the stored bit
               is 0, so bit = 1 selects the un-negated (+cross) direction.
               Empirically verified against the real decoder by the
               single-triangle round-trip cases in test_compress_tess.c. */
            rev[k] = (uint8_t)(dot_val > 0.0);
            /* A set bit makes the decoder swap its left/right edge handling
               for this triangle's grow pushes, which the already-emitted
               traversal arrays assumed never happens; refuse to build a
               stream the decoder would walk differently. */
            if (rev[k] && trav->edge_status_array[k] != 0)
            {
                prc_free(ctx, rev);
                prc_error(ctx, PRC_ERROR_INTERNAL,
                    "prc_encode_normals_c1: normals reverse a growing triangle (unsupported)\n");
                return PRC_ERROR_INTERNAL;
            }
        }
    }
    *normal_is_reversed_out = rev;
    return 0;
}

/* Mirror of the basis phase of the decoder's prc_compute_vertex_normal,
   BEFORE any reversal bit is applied: the decoder negates Z first and only
   then derives Y = normalize(cross(Z, X)), and normalize(-v) == -normalize(v)
   exactly, so the reversed variants differ from this raw basis only by signs
   the tuple's tri/x/y bits reproduce. Same helpers, same operation order,
   including the prc_vec_make_orth_basis_normals fallback. */
static int
prc_encode_vertex_normal_basis(prc_vec3 V1, prc_vec3 V2, prc_vec3 V3,
    prc_vec3 *x_out, prc_vec3 *y_out, prc_vec3 *z_out)
{
    prc_vec3 V1_norm, V2_norm, V3_norm, temp1, temp2;
    prc_vec3 X_norm, Y_norm, Z_norm;
    double theta1 = 0, theta2 = 0, theta3 = 0;
    int code;

    prc_vec_sub(V2, V1, &V1_norm);
    code = prc_vec_normalize(&V1_norm);
    if (code < 0)
        return code;
    prc_vec_sub(V3, V1, &V2_norm);
    code = prc_vec_normalize(&V2_norm);
    if (code < 0)
        return code;
    prc_vec_sub(V3, V2, &V3_norm);
    code = prc_vec_normalize(&V3_norm);
    if (code < 0)
        return code;

    code = prc_vec_angle_between_vectors_normal(V1_norm, V2_norm, &theta1);
    if (code < 0)
        return code;
    prc_vec_copy(V1_norm, &temp1, 1);
    code = prc_vec_angle_between_vectors_normal(V3_norm, temp1, &theta2);
    if (code < 0)
        return code;
    prc_vec_copy(V2_norm, &temp1, 1);
    prc_vec_copy(V3_norm, &temp2, 1);
    code = prc_vec_angle_between_vectors_normal(temp1, temp2, &theta3);
    if (code < 0)
        return code;

    if ((theta1 < theta2) && (theta1 < theta3))
    {
        prc_vec_copy(V1_norm, &X_norm, 0);
        prc_vec_cross(V1_norm, V2_norm, &Z_norm);
    }
    else if (theta2 < theta3)
    {
        prc_vec_copy(V3_norm, &X_norm, 0);
        prc_vec_copy(V3_norm, &temp1, 1);
        prc_vec_cross(temp1, V1_norm, &Z_norm);
    }
    else
    {
        prc_vec_copy(V2_norm, &X_norm, 1);
        prc_vec_cross(V2_norm, V3_norm, &Z_norm);
    }

    code = prc_vec_normalize(&Z_norm);
    if (code < 0)
    {
        prc_basis basis;

        basis.X = X_norm;
        code = prc_vec_make_orth_basis_normals(&basis);
        if (code < 0)
            return code;
        Z_norm = basis.Z;
    }

    prc_vec_cross(Z_norm, X_norm, &Y_norm);
    code = prc_vec_normalize(&Y_norm);
    if (code < 0)
        return code;

    *x_out = X_norm;
    *y_out = Y_norm;
    *z_out = Z_norm;
    return 0;
}

static int32_t
prc_encode_quantize_angle(double angle)
{
    long long q = llround(angle * (double)PRC_ENCODE_NORMAL_ANGLE_MAX / PRC_HALF_PI);

    if (q < 0)
        q = 0;
    if (q > PRC_ENCODE_NORMAL_ANGLE_MAX)
        q = PRC_ENCODE_NORMAL_ANGLE_MAX;
    return (int32_t)q;
}

/* Invert the decoder's reconstruction
     n = cos(theta)cos(phi)*Xf + sin(theta)cos(phi)*Yf + sin(phi)*Zf
   where Zf = +-Z (tri bit), Yf = +-cross(Zf, X) (y bit), Xf = +-X (x bit) and
   theta, phi are stored unsigned in [0, PI/2]: project N onto the raw basis
   and fold every sign into the three bits. */
static void
prc_encode_project_normal(prc_vec3 N, prc_vec3 X, prc_vec3 Y, prc_vec3 Z,
    prc_encode_normal_tuple *t)
{
    double nx = prc_vec_dot_product(N, X);
    double ny = prc_vec_dot_product(N, Y);
    double nz = prc_vec_dot_product(N, Z);
    double az = fabs(nz);

    t->x_reversed = (uint8_t)(nx < 0.0);
    t->tri_reversed = (uint8_t)(nz < 0.0);
    t->y_reversed = (uint8_t)((ny < 0.0) != (nz < 0.0));
    if (az > 1.0)
        az = 1.0;
    t->theta_q = prc_encode_quantize_angle(atan2(fabs(ny), fabs(nx)));
    t->phi_q = prc_encode_quantize_angle(asin(az));
}

/* The vector the decoder will actually store for this tuple, needed both to
   predict its normal_was_reversed decision and to know what a back-reference
   would resolve to. */
static void
prc_encode_simulate_decoded_normal(prc_vec3 X, prc_vec3 Y, prc_vec3 Z,
    const prc_encode_normal_tuple *t, prc_vec3 *out)
{
    double scale = PRC_HALF_PI / (double)PRC_ENCODE_NORMAL_ANGLE_MAX;
    double theta = (double)t->theta_q * scale;
    double phi = (double)t->phi_q * scale;
    double f1 = cos(theta) * cos(phi);
    double f2 = sin(theta) * cos(phi);
    double f3 = sin(phi);

    if (t->tri_reversed)
    {
        prc_vec_negate(&Z);
        prc_vec_negate(&Y);
    }
    if (t->x_reversed)
        prc_vec_negate(&X);
    if (t->y_reversed)
        prc_vec_negate(&Y);

    out->x = f1 * X.x + f2 * Y.x + f3 * Z.x;
    out->y = f1 * X.y + f2 * Y.y + f3 * Z.y;
    out->z = f1 * X.z + f2 * Y.z + f3 * Z.z;
    (void)prc_vec_normalize(out);
}

static int
prc_encode_point_norm_append(prc_context *ctx, prc_encode_point_norm *p,
    prc_vec3 input_n, prc_vec3 decoded_n)
{
    if (p->num_stored == p->cap)
    {
        uint32_t new_cap = p->cap ? p->cap * 2 : 4;
        prc_vec3 *grown;

        grown = (prc_vec3 *)prc_realloc(ctx, p->slot_input, (size_t)new_cap * sizeof(prc_vec3));
        if (grown == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_point_norm_append\n");
            return PRC_ERROR_MEMORY;
        }
        p->slot_input = grown;
        grown = (prc_vec3 *)prc_realloc(ctx, p->slot_decoded, (size_t)new_cap * sizeof(prc_vec3));
        if (grown == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_point_norm_append\n");
            return PRC_ERROR_MEMORY;
        }
        p->slot_decoded = grown;
        p->cap = new_cap;
    }
    p->slot_input[p->num_stored] = input_n;
    p->slot_decoded[p->num_stored] = decoded_n;
    p->num_stored++;
    return 0;
}

int
prc_encode_normals_c2(prc_context *ctx, const prc_encode_mesh *mesh,
    const prc_encode_traversal_result *trav, const double *corner_normals,
    int32_t **normal_angle_array_out, uint32_t *normal_angle_count_out,
    uint8_t **normal_binary_data_out, uint32_t *normal_binary_data_size_out)
{
    uint32_t num_tris, npts, k, c, v;
    prc_vec3 *visit_normals = NULL;
    prc_vec3 *bx = NULL, *by = NULL, *bz = NULL;
    prc_encode_normal_tuple *tuples = NULL;
    prc_encode_point_norm *pn = NULL;
    int32_t *angles = NULL;
    uint8_t *bin = NULL;
    uint32_t angle_count = 0, bin_count = 0;
    int ret = PRC_ERROR_INTERNAL;
    int code;

    if (normal_angle_array_out == NULL || normal_angle_count_out == NULL ||
        normal_binary_data_out == NULL || normal_binary_data_size_out == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_normals_c2: NULL output\n");
        return PRC_ERROR_INTERNAL;
    }
    *normal_angle_array_out = NULL;
    *normal_angle_count_out = 0;
    *normal_binary_data_out = NULL;
    *normal_binary_data_size_out = 0;

    code = prc_encode_check_trav_arrays(ctx, mesh, trav);
    if (code < 0)
        return code;
    num_tris = trav->edge_status_array_size;
    if (num_tris == 0)
        return 0;
    if (corner_normals == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_normals_c2: NULL corner normals\n");
        return PRC_ERROR_INTERNAL;
    }
    npts = trav->num_decoded_points;

    visit_normals = (prc_vec3 *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(prc_vec3));
    bx = (prc_vec3 *)prc_malloc(ctx, (size_t)num_tris * sizeof(prc_vec3));
    by = (prc_vec3 *)prc_malloc(ctx, (size_t)num_tris * sizeof(prc_vec3));
    bz = (prc_vec3 *)prc_malloc(ctx, (size_t)num_tris * sizeof(prc_vec3));
    tuples = (prc_encode_normal_tuple *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(prc_encode_normal_tuple));
    pn = (prc_encode_point_norm *)prc_calloc(ctx, npts, sizeof(prc_encode_point_norm));
    angles = (int32_t *)prc_malloc(ctx, (size_t)num_tris * 6 * sizeof(int32_t));
    /* Per-visit worst case: 4 fresh bits, or 1 reference bit plus at most 32
       back-reference index bits. */
    bin = (uint8_t *)prc_malloc(ctx, (size_t)num_tris * 3 * 40 * sizeof(uint8_t));
    if (visit_normals == NULL || bx == NULL || by == NULL || bz == NULL ||
        tuples == NULL || pn == NULL || angles == NULL || bin == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_normals_c2\n");
        ret = PRC_ERROR_MEMORY;
        goto fail;
    }

    for (k = 0; k < num_tris; k++)
    {
        const int32_t *idx = &trav->triangle_point_indices[(size_t)k * 3];
        uint32_t mesh_tri = trav->triangle_mesh_order[k];

        code = prc_encode_vertex_normal_basis(prc_encode_decoded_vec(trav, idx[0]),
            prc_encode_decoded_vec(trav, idx[1]), prc_encode_decoded_vec(trav, idx[2]),
            &bx[k], &by[k], &bz[k]);
        if (code < 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_normals_c2: degenerate triangle basis\n");
            goto fail;
        }

        for (c = 0; c < 3; c++)
        {
            int32_t mv = trav->point_mesh_vertex[idx[c]];
            uint32_t j, corner = 3;
            prc_vec3 n;

            for (j = 0; j < 3; j++)
            {
                if (mesh->tri_indices[(size_t)mesh_tri * 3 + j] == (uint32_t)mv)
                    corner = j;
            }
            if (corner == 3)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_normals_c2: traversal/mesh corner mismatch\n");
                goto fail;
            }
            n.x = corner_normals[(size_t)mesh_tri * 9 + (size_t)corner * 3 + 0];
            n.y = corner_normals[(size_t)mesh_tri * 9 + (size_t)corner * 3 + 1];
            n.z = corner_normals[(size_t)mesh_tri * 9 + (size_t)corner * 3 + 2];
            if (prc_vec_normalize(&n) < 0)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL, "prc_encode_normals_c2: zero-length input normal\n");
                goto fail;
            }
            visit_normals[(size_t)k * 3 + c] = n;
            prc_encode_project_normal(n, bx[k], by[k], bz[k], &tuples[(size_t)k * 3 + c]);
        }
    }

    /* has_multiple_normals must be fixed for a point's entire lifetime at its
       first visit, so decide it up front: only if every incident visit wants
       the same input normal can later visits legally consume zero bits. */
    for (v = 0; v < npts; v++)
        pn[v].all_same = 1;
    for (k = 0; k < num_tris * 3; k++)
    {
        int32_t pt = trav->triangle_point_indices[k];

        if (!pn[pt].has_first)
        {
            pn[pt].first_normal = visit_normals[k];
            pn[pt].has_first = 1;
        }
        else if (prc_vec_dot_product(pn[pt].first_normal, visit_normals[k]) < 1.0 - 1.0e-9)
        {
            pn[pt].all_same = 0;
        }
    }

    for (k = 0; k < num_tris; k++)
    {
        const int32_t *idx = &trav->triangle_point_indices[(size_t)k * 3];
        prc_vec3 corner0_decoded;

        corner0_decoded.x = corner0_decoded.y = corner0_decoded.z = 0.0;
        for (c = 0; c < 3; c++)
        {
            uint32_t visit = k * 3 + c;
            const prc_encode_normal_tuple *t = &tuples[visit];
            prc_encode_point_norm *p = &pn[idx[c]];
            prc_vec3 assigned;

            if (p->state == PRC_VERTEX_NORM_NOT_ENCOUNTERED)
            {
                uint8_t hm = (uint8_t)!p->all_same;

                bin[bin_count++] = hm;
                bin[bin_count++] = t->tri_reversed;
                bin[bin_count++] = t->x_reversed;
                bin[bin_count++] = t->y_reversed;
                angles[angle_count++] = t->theta_q;
                angles[angle_count++] = t->phi_q;
                prc_encode_simulate_decoded_normal(bx[k], by[k], bz[k], t, &assigned);
                if (hm)
                {
                    p->state = PRC_VERTEX_NORM_IS_MULTIPLE;
                    code = prc_encode_point_norm_append(ctx, p, visit_normals[visit], assigned);
                    if (code < 0)
                    {
                        ret = code;
                        goto fail;
                    }
                }
                else
                {
                    p->state = PRC_VERTEX_NORM_IS_NOT_MULTIPLE;
                    p->single_decoded = assigned;
                }
            }
            else if (p->state == PRC_VERTEX_NORM_IS_NOT_MULTIPLE)
            {
                /* the decoder reuses the single stored normal without reading
                   any bits; valid because all_same guaranteed every incident
                   visit wants this normal */
                assigned = p->single_decoded;
            }
            else
            {
                uint32_t s, found = UINT32_MAX;

                for (s = 0; s < p->num_stored; s++)
                {
                    if (prc_vec_dot_product(p->slot_input[s], visit_normals[visit]) > 1.0 - 1.0e-9)
                    {
                        found = s;
                        break;
                    }
                }
                if (found != UINT32_MAX)
                {
                    /* inverse of the decoder's
                       ref_index = (num_stored - 1) - read_index, emitted
                       LSB-first over exactly the bit width the decoder will
                       compute from its own num_stored */
                    uint32_t number_bits = get_number_bits_to_store_unsigned_integer2(p->num_stored - 1);
                    uint32_t read_index = (p->num_stored - 1) - found;
                    uint32_t b;

                    bin[bin_count++] = 1;
                    for (b = 0; b < number_bits; b++)
                        bin[bin_count++] = (uint8_t)((read_index >> b) & 1u);
                    assigned = p->slot_decoded[found];
                }
                else
                {
                    bin[bin_count++] = 0;
                    bin[bin_count++] = t->tri_reversed;
                    bin[bin_count++] = t->x_reversed;
                    bin[bin_count++] = t->y_reversed;
                    angles[angle_count++] = t->theta_q;
                    angles[angle_count++] = t->phi_q;
                    prc_encode_simulate_decoded_normal(bx[k], by[k], bz[k], t, &assigned);
                    code = prc_encode_point_norm_append(ctx, p, visit_normals[visit], assigned);
                    if (code < 0)
                    {
                        ret = code;
                        goto fail;
                    }
                }
            }
            if (c == 0)
                corner0_decoded = assigned;
        }

        /* The decoder derives normal_was_reversed from the corner-0 normal
           (prc_is_normal_reversed_single_normal) and, when set, swaps its
           left/right edge handling for this triangle's grow pushes -- which
           the already-emitted traversal arrays assumed never happens. Refuse
           to build a stream the decoder would silently walk differently. */
        {
            prc_vec3 P0 = prc_encode_decoded_vec(trav, idx[0]);
            prc_vec3 P1 = prc_encode_decoded_vec(trav, idx[1]);
            prc_vec3 P2 = prc_encode_decoded_vec(trav, idx[2]);
            prc_vec3 mid, e1, e2, cross1;

            prc_vec_avg(P0, P1, &mid);
            prc_vec_sub(P1, P0, &e1);
            prc_vec_sub(P2, mid, &e2);
            prc_vec_cross(e2, e1, &cross1);
            if (prc_vec_dot_product(cross1, corner0_decoded) < 0.0 &&
                trav->edge_status_array[k] != 0)
            {
                prc_error(ctx, PRC_ERROR_INTERNAL,
                    "prc_encode_normals_c2: normals reverse a growing triangle (unsupported)\n");
                goto fail;
            }
        }
    }

    if (angle_count > 0 && (size_t)angle_count < (size_t)num_tris * 6)
    {
        int32_t *shrunk = (int32_t *)prc_realloc(ctx, angles, (size_t)angle_count * sizeof(int32_t));

        if (shrunk != NULL)
            angles = shrunk;
    }
    if (bin_count > 0 && (size_t)bin_count < (size_t)num_tris * 3 * 40)
    {
        uint8_t *shrunk = (uint8_t *)prc_realloc(ctx, bin, (size_t)bin_count * sizeof(uint8_t));

        if (shrunk != NULL)
            bin = shrunk;
    }
    *normal_angle_array_out = angles;
    *normal_angle_count_out = angle_count;
    *normal_binary_data_out = bin;
    *normal_binary_data_size_out = bin_count;
    angles = NULL;
    bin = NULL;
    ret = 0;

fail:
    if (angles != NULL)
        prc_free(ctx, angles);
    if (bin != NULL)
        prc_free(ctx, bin);
    if (visit_normals != NULL)
        prc_free(ctx, visit_normals);
    if (bx != NULL)
        prc_free(ctx, bx);
    if (by != NULL)
        prc_free(ctx, by);
    if (bz != NULL)
        prc_free(ctx, bz);
    if (tuples != NULL)
        prc_free(ctx, tuples);
    if (pn != NULL)
    {
        for (v = 0; v < npts; v++)
        {
            if (pn[v].slot_input != NULL)
                prc_free(ctx, pn[v].slot_input);
            if (pn[v].slot_decoded != NULL)
                prc_free(ctx, pn[v].slot_decoded);
        }
        prc_free(ctx, pn);
    }
    return ret;
}

/* ---- Step E: bitstream assembly ---------------------------------------- */

int
prc_write_compress_tess_to_stream(prc_context *ctx, prc_bit_write_state *state,
    const prc_encode_traversal_result *trav, double tolerance_mm,
    const uint8_t *normal_is_reversed_c1, double crease_angle_degrees,
    const int32_t *normal_angle_array, uint32_t normal_angle_array_count,
    const uint8_t *normal_binary_data, uint32_t normal_binary_data_size,
    uint8_t must_recalculate_normals)
{
    uint32_t k, num_refs = 0;

    if (state == NULL || trav == NULL || !(tolerance_mm > 0.0))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_to_stream: bad arguments\n");
        return PRC_ERROR_INTERNAL;
    }
    if ((trav->point_array == NULL && trav->point_array_size != 0) ||
        (trav->edge_status_array == NULL && trav->edge_status_array_size != 0) ||
        (trav->triangle_face_array == NULL && trav->triangle_face_array_size != 0) ||
        (trav->points_is_reference_array == NULL && trav->points_is_reference_array_size != 0) ||
        (trav->point_reference_array == NULL && trav->point_reference_array_size != 0))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_to_stream: NULL array with non-zero count\n");
        return PRC_ERROR_INTERNAL;
    }
    if (must_recalculate_normals)
    {
        if (normal_is_reversed_c1 == NULL && trav->triangle_face_array_size != 0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_to_stream: missing C1 reversal bits\n");
            return PRC_ERROR_INTERNAL;
        }
    }
    else if ((normal_angle_array == NULL && normal_angle_array_count != 0) ||
        (normal_binary_data == NULL && normal_binary_data_size != 0))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_to_stream: missing C2 normal data\n");
        return PRC_ERROR_INTERNAL;
    }

    for (k = 0; k < trav->points_is_reference_array_size; k++)
    {
        if (trav->points_is_reference_array[k])
            num_refs++;
    }
    /* The reader sizes point_reference_array by counting these 1-bits, so a
       mismatch would silently desynchronize everything after it. */
    if (num_refs != trav->point_reference_array_size)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_to_stream: reference bookkeeping mismatch\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_bit(ctx, state, 0) != 0)   /* is_calculated */
        goto werr;
    /* has_faces: per the spec (Table 175), Required, "TRUE if the entity
       is built using geometrical faces". trav->triangle_face_array is
       always real, populated per-triangle face-index data (prc_encode_
       traversal defaults every entry to face 0 when the caller supplies
       no explicit face grouping, never leaves it meaningless), so this
       must always be TRUE -- confirmed the hard way: writing FALSE here
       while still emitting a real triangle_face_array is self-
       contradictory, and while nanoPRC's own decoder tolerates the
       inconsistency (has_faces gates a few of its own optional-data
       branches but the core geometry path doesn't depend on it), an
       independent, stricter PRC engine silently returns a null/empty
       geometry object for the whole entity rather than reject or repair
       it. */
    if (prc_bitwrite_bit(ctx, state, 1) != 0)   /* has_faces */
        goto werr;
    if (prc_bitwrite_double(ctx, state, tolerance_mm) != 0)
        goto werr;
    /* float per the format; the reader applies the same truncation */
    if (prc_bitwrite_float(ctx, state, (float)trav->origin[0]) != 0)
        goto werr;
    if (prc_bitwrite_float(ctx, state, (float)trav->origin[1]) != 0)
        goto werr;
    if (prc_bitwrite_float(ctx, state, (float)trav->origin[2]) != 0)
        goto werr;
    if (prc_bitwrite_compressed_integer_array(ctx, state, trav->point_array,
            trav->point_array_size) != 0)
        goto werr;
    if (prc_bitwrite_character_array(ctx, state, trav->edge_status_array,
            trav->edge_status_array_size, 2, 1, 0) != 0)
        goto werr;
    if (prc_bitwrite_compressed_indice_array(ctx, state, trav->triangle_face_array,
            trav->triangle_face_array_size, 1, 0) != 0)
        goto werr;
    if (prc_bitwrite_uint32(ctx, state, trav->points_is_reference_array_size) != 0)
        goto werr;
    for (k = 0; k < trav->points_is_reference_array_size; k++)
    {
        if (prc_bitwrite_bit(ctx, state, trav->points_is_reference_array[k]) != 0)
            goto werr;
    }
    if (prc_bitwrite_compressed_indice_array(ctx, state, trav->point_reference_array,
            trav->point_reference_array_size, 0, num_refs) != 0)
        goto werr;
    if (prc_bitwrite_bit(ctx, state, must_recalculate_normals ? 1 : 0) != 0)
        goto werr;

    if (must_recalculate_normals)
    {
        for (k = 0; k < trav->triangle_face_array_size; k++)
        {
            if (prc_bitwrite_bit(ctx, state, normal_is_reversed_c1[k]) != 0)
                goto werr;
        }
        /* stored in degrees; the reader converts to radians itself */
        if (prc_bitwrite_double(ctx, state, crease_angle_degrees) != 0)
            goto werr;
        if (prc_bitwrite_uint8(ctx, state, 0) != 0)   /* normal_recalculation_flags */
            goto werr;
    }
    else
    {
        uint32_t face_count = 1;

        for (k = 0; k < trav->triangle_face_array_size; k++)
        {
            if (trav->triangle_face_array[k] >= 0 &&
                (uint32_t)trav->triangle_face_array[k] + 1 > face_count)
                face_count = (uint32_t)trav->triangle_face_array[k] + 1;
        }
        if (prc_bitwrite_uint8(ctx, state, PRC_ENCODE_NORMAL_ANGLE_BITS) != 0)
            goto werr;
        if (prc_bitwrite_uint32(ctx, state, normal_binary_data_size) != 0)
            goto werr;
        for (k = 0; k < normal_binary_data_size; k++)
        {
            if (prc_bitwrite_bit(ctx, state, normal_binary_data[k]) != 0)
                goto werr;
        }
        if (prc_bitwrite_short_array(ctx, state, normal_angle_array,
                normal_angle_array_count, 1, PRC_ENCODE_NORMAL_ANGLE_BITS) != 0)
            goto werr;
        /* every face non-planar: routes all decoding through the per-vertex
           path, never the separate per-face-planar normal-sharing path */
        for (k = 0; k < face_count; k++)
        {
            if (prc_bitwrite_bit(ctx, state, 0) != 0)
                goto werr;
        }
    }

    if (prc_bitwrite_bit(ctx, state, 0) != 0)   /* is_point_color */
        goto werr;
    if (prc_bitwrite_bit(ctx, state, 0) != 0)   /* is_multiple_line_attribute */
        goto werr;
    /* read unconditionally by the parser. The C2 branch forces has_faces on
       the decode side, whose style bookkeeping dereferences
       line_attribute_array[0], so C2 must carry one no-style (biased 0)
       entry; C1 keeps it empty. */
    if (must_recalculate_normals)
    {
        if (prc_bitwrite_short_array(ctx, state, NULL, 0, 1, 16) != 0)
            goto werr;
    }
    else
    {
        int32_t no_style = 0;

        if (prc_bitwrite_short_array(ctx, state, &no_style, 1, 1, 16) != 0)
            goto werr;
    }
    if (prc_bitwrite_bit(ctx, state, 1) != 0)   /* no_texture */
        goto werr;
    if (prc_bitwrite_bit(ctx, state, 0) != 0)   /* has_behaviors */
        goto werr;
    return 0;

werr:
    prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_to_stream: bit write failed\n");
    return PRC_ERROR_INTERNAL;
}

void
prc_encode_preprocess_free(prc_context *ctx, prc_encode_mesh *m)
{
    if (m == NULL)
        return;
    if (m->positions != NULL)
        prc_free(ctx, m->positions);
    if (m->tri_indices != NULL)
        prc_free(ctx, m->tri_indices);
    if (m->tri_orig_index != NULL)
        prc_free(ctx, m->tri_orig_index);
    if (m->edges != NULL)
        prc_free(ctx, m->edges);
    if (m->tri_component != NULL)
        prc_free(ctx, m->tri_component);
    m->positions = NULL;
    m->tri_indices = NULL;
    m->tri_orig_index = NULL;
    m->edges = NULL;
    m->tri_component = NULL;
    m->num_positions = 0;
    m->num_triangles = 0;
    m->num_edges = 0;
    m->num_components = 0;
}

int
prc_write_compress_tess_entry(prc_context *ctx, prc_bit_write_state *s,
    const double *positions, uint32_t num_positions,
    const double *normals, uint32_t num_normals,
    const uint32_t *tri_indices, const uint32_t *norm_indices, uint32_t num_triangles,
    const uint32_t *face_tri_counts, uint32_t num_faces,
    prc_write_tolerance tolerance, double crease_angle_degrees)
{
    prc_encode_mesh mesh;
    prc_encode_traversal_result trav;
    uint32_t *orig_face_id = NULL;      /* num_triangles entries, ORIGINAL (pre-preprocess) order */
    uint32_t *face_indices_post = NULL; /* mesh.num_triangles entries, POST-preprocess order */
    double *corner_normals = NULL;      /* mesh.num_triangles * 9, POST-preprocess order */
    uint8_t *rev = NULL;
    int32_t *angles = NULL;
    uint8_t *bin = NULL;
    uint32_t acount = 0, bsize = 0;
    uint8_t must_recalculate_normals;
    int mesh_ready = 0, trav_ready = 0;
    int ret = PRC_ERROR_INTERNAL;
    int code;
    uint32_t f, t, k;

    (void)num_normals;

    if (ctx == NULL || s == NULL || num_triangles == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_entry: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    code = prc_encode_preprocess(ctx, positions, num_positions, tri_indices, num_triangles, tolerance, &mesh);
    if (code != 0) return code;
    mesh_ready = 1;

    if (mesh.num_triangles == 0)
    {
        /* Every input triangle was degenerate after welding; nothing to
           encode. Treated as a caller error (same as num_triangles == 0
           above) rather than silently emitting an empty compressed
           record, which this pipeline has never been exercised against. */
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_compress_tess_entry: no surviving triangles after weld\n");
        goto cleanup;
    }

    if (face_tri_counts != NULL && num_faces > 0)
    {
        orig_face_id = (uint32_t *)prc_malloc(ctx, (size_t)num_triangles * sizeof(uint32_t));
        face_indices_post = (uint32_t *)prc_malloc(ctx, (size_t)mesh.num_triangles * sizeof(uint32_t));
        if (orig_face_id == NULL || face_indices_post == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_compress_tess_entry\n");
            ret = PRC_ERROR_MEMORY;
            goto cleanup;
        }
        t = 0;
        for (f = 0; f < num_faces; f++)
        {
            uint32_t n = face_tri_counts[f];
            for (k = 0; k < n; k++)
                orig_face_id[t++] = f;
        }
        for (k = 0; k < mesh.num_triangles; k++)
            face_indices_post[k] = orig_face_id[mesh.tri_orig_index[k]];
    }

    code = prc_encode_traversal(ctx, &mesh, face_indices_post, mesh.tolerance_mm, &trav, NULL, NULL);
    if (code != 0) goto cleanup;
    trav_ready = 1;

    must_recalculate_normals = (normals == NULL) ? 1u : 0u;
    if (!must_recalculate_normals)
    {
        corner_normals = (double *)prc_malloc(ctx, (size_t)mesh.num_triangles * 9 * sizeof(double));
        if (corner_normals == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_compress_tess_entry\n");
            ret = PRC_ERROR_MEMORY;
            goto cleanup;
        }
        for (k = 0; k < mesh.num_triangles; k++)
        {
            uint32_t orig_tri = mesh.tri_orig_index[k];
            uint32_t c;
            for (c = 0; c < 3; c++)
            {
                uint32_t nidx = norm_indices[(size_t)orig_tri * 3 + c];
                memcpy(&corner_normals[((size_t)k * 3 + c) * 3], &normals[(size_t)nidx * 3], 3 * sizeof(double));
            }
        }
        code = prc_encode_normals_c2(ctx, &mesh, &trav, corner_normals, &angles, &acount, &bin, &bsize);
        if (code != 0)
        {
            /* Supplied per-corner normals can legitimately conflict with the
               decoder's canonical traversal winding for a "growing" (non-
               leaf) triangle -- a real geometric constraint of the C2 path,
               not a bug (see prc_encode_normals_c2's own error message).
               Rather than fail the whole entry over it, fall back to C1
               (decoder-reconstructed normals from geometry): every real
               mesh has SOME valid encoding, and reconstructed-but-rendered
               beats exact-but-rejected. */
            prc_free(ctx, corner_normals);
            corner_normals = NULL;
            must_recalculate_normals = 1u;
            code = prc_encode_normals_c1(ctx, &mesh, &trav, NULL, &rev);
        }
    }
    else
    {
        code = prc_encode_normals_c1(ctx, &mesh, &trav, NULL, &rev);
    }
    if (code != 0) goto cleanup;

    code = prc_write_compress_tess_to_stream(ctx, s, &trav, mesh.tolerance_mm,
        rev, crease_angle_degrees, angles, acount, bin, bsize, must_recalculate_normals);
    ret = code;

cleanup:
    if (orig_face_id != NULL) prc_free(ctx, orig_face_id);
    if (face_indices_post != NULL) prc_free(ctx, face_indices_post);
    if (corner_normals != NULL) prc_free(ctx, corner_normals);
    if (rev != NULL) prc_free(ctx, rev);
    if (angles != NULL) prc_free(ctx, angles);
    if (bin != NULL) prc_free(ctx, bin);
    if (trav_ready) prc_encode_traversal_free(ctx, &trav);
    if (mesh_ready) prc_encode_preprocess_free(ctx, &mesh);
    return ret;
}
