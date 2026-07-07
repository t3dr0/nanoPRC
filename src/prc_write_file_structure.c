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
#include "prc_write_file_structure.h"
#include "prc_write_tess_3d.h"
#include "prc_data.h"
#include "zlib.h"

int
prc_write_schema_and_globals_to_stream(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_global_tables *tables)
{
    if (ctx == NULL || s == NULL || tables == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_schema_and_globals_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    /* Table 8 PRC_Schema: no custom entity schema */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* schema_count */

    /* Table 39 PRC_TYPE_ASM_FileStructureGlobals */
    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_FileStructureGlobals) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* base.attribute_count */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;     /* base.name.same */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* external file_count (cross-file references) */

    if (prc_write_globals_to_stream(ctx, s, tables) != 0) goto fail;

    return 0;

fail:
    return s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;
}

int
prc_write_tessellation_section_to_stream(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_tess_entry *entries, uint32_t num_entries)
{
    uint32_t i;

    if (ctx == NULL || s == NULL || (entries == NULL && num_entries > 0))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tessellation_section_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_FileStructureTessellation) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* base.attribute_count */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;     /* base.name.same */

    if (prc_bitwrite_uint32(ctx, s, num_entries) != 0) goto fail; /* tess_count */
    for (i = 0; i < num_entries; i++)
    {
        const prc_write_tess_entry *e = &entries[i];

        if (e->kind == PRC_WRITE_TESS_KIND_3D)
        {
            if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_TESS_3D) != 0) goto fail;
            if (prc_write_tess_3d(ctx, s, e->positions, e->num_positions, e->normals, e->num_normals,
                    e->tri_indices, e->norm_indices, e->num_triangles, e->face_tri_counts, e->num_faces) != 0)
                goto fail;
        }
        else
        {
            if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_TESS_3D_Wire) != 0) goto fail;
            if (prc_write_wire_tess(ctx, s, e->wire_elements, e->num_wire_elements) != 0) goto fail;
        }
    }

    return 0;

fail:
    return s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;
}

int
prc_write_deflate(prc_context *ctx, const uint8_t *src, size_t src_len, uint8_t **out, size_t *out_len)
{
    z_stream strm;
    uLong bound;
    uint8_t *dst;
    int ret;

    if (ctx == NULL || out == NULL || out_len == NULL || (src == NULL && src_len > 0))
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_deflate: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    memset(&strm, 0, sizeof(strm));
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_deflate: deflateInit failed\n");
        return PRC_ERROR_INTERNAL;
    }

    bound = deflateBound(&strm, (uLong)src_len);
    dst = (uint8_t *)prc_malloc(ctx, bound == 0 ? 1 : bound);
    if (dst == NULL)
    {
        deflateEnd(&strm);
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_deflate\n");
        return PRC_ERROR_MEMORY;
    }

    strm.next_in = (Bytef *)src;
    strm.avail_in = (uInt)src_len;
    strm.next_out = (Bytef *)dst;
    strm.avail_out = (uInt)bound;

    ret = deflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END)
    {
        deflateEnd(&strm);
        prc_free(ctx, dst);
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_deflate: deflate failed\n");
        return PRC_ERROR_INTERNAL;
    }

    *out_len = strm.total_out;
    deflateEnd(&strm);
    *out = dst;
    return 0;
}

static uint8_t *
prc_write_le_uint32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
    return p + 4;
}

void
prc_write_file_struct_header_bytes(uint8_t *out, uint32_t min_vers_for_read, uint32_t auth_vers)
{
    uint8_t *p = out;
    int i;

    p[0] = 'P'; p[1] = 'R'; p[2] = 'C';
    p += 3;
    p = prc_write_le_uint32(p, min_vers_for_read);
    p = prc_write_le_uint32(p, auth_vers);
    for (i = 0; i < 4; i++) p = prc_write_le_uint32(p, 0); /* unique_id_file */
    for (i = 0; i < 4; i++) p = prc_write_le_uint32(p, 0); /* unique_id_application */
    p = prc_write_le_uint32(p, 0);                          /* file_count (embedded uncompressed files) */
}
