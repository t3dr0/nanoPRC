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

#include <string.h>
#include <math.h>
#include "prc_write_compress_tess.h"
#include "prc_vector_util.h"

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
        if (out->tri_indices == NULL)
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
            clean_tris++;
        }
        out->num_triangles = clean_tris;
        (void)removed;

        if (clean_tris == 0)
        {
            prc_free(ctx, out->tri_indices);
            out->tri_indices = NULL;
        }
        else if (clean_tris < num_triangles)
        {
            uint32_t *shrunk = (uint32_t *)prc_realloc(ctx, out->tri_indices, (size_t)clean_tris * 3 * sizeof(uint32_t));
            if (shrunk != NULL)
                out->tri_indices = shrunk;
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
    prc_encode_traversal_result *out;
} prc_encode_state;

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
    prc_vec3 x, y, z, z_temp, w, origin;
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
    prc_encode_traversal_result *out)
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
    st.visited = (uint8_t *)prc_calloc(ctx, num_tris, sizeof(uint8_t));
    st.pending = (uint8_t *)prc_calloc(ctx, num_tris, sizeof(uint8_t));
    st.neighbor = (int32_t *)prc_malloc(ctx, (size_t)num_tris * 3 * sizeof(int32_t));
    st.vtx_map = (int32_t *)prc_malloc(ctx, (size_t)num_pos * sizeof(int32_t));
    st.decoded_pos = (prc_vec3 *)prc_malloc(ctx, (size_t)num_pos * sizeof(prc_vec3));
    if (out->point_array == NULL || out->edge_status_array == NULL ||
        out->triangle_face_array == NULL || out->points_is_reference_array == NULL ||
        out->point_reference_array == NULL || st.visited == NULL ||
        st.pending == NULL || st.neighbor == NULL || st.vtx_map == NULL ||
        st.decoded_pos == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_encode_traversal\n");
        ret = PRC_ERROR_MEMORY;
        goto fail;
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

    ret = 0;
    goto cleanup;

fail:
    prc_encode_traversal_free(ctx, out);

cleanup:
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
    memset(out, 0, sizeof(*out));
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
    if (m->edges != NULL)
        prc_free(ctx, m->edges);
    if (m->tri_component != NULL)
        prc_free(ctx, m->tri_component);
    m->positions = NULL;
    m->tri_indices = NULL;
    m->edges = NULL;
    m->tri_component = NULL;
    m->num_positions = 0;
    m->num_triangles = 0;
    m->num_edges = 0;
    m->num_components = 0;
}
