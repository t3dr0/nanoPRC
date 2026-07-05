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
