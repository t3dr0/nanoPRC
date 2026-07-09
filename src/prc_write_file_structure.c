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
#include "prc_write_compress_tess.h"
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
        else if (e->kind == PRC_WRITE_TESS_KIND_COMPRESSED)
        {
            /* A zero-valued tolerance/crease-angle (the memset(0) default
               most callers start from) means "use a sensible default"
               rather than a literal zero-mm weld distance or a zero-degree
               crease angle -- see the field doc comments in prc_api.h. */
            prc_write_tolerance tol = (e->tolerance.value > 0.0) ? e->tolerance : prc_write_tol_relative(1e-6);
            double crease = (e->crease_angle_degrees > 0.0) ? e->crease_angle_degrees : 30.0;

            if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_TESS_3D_Compressed) != 0) goto fail;
            if (prc_write_compress_tess_entry(ctx, s, e->positions, e->num_positions,
                    e->normals, e->num_normals, e->tri_indices, e->norm_indices, e->num_triangles,
                    e->face_tri_counts, e->num_faces, tol, crease) != 0)
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
prc_write_geometry_section_to_stream(prc_context *ctx, prc_bit_write_state *s)
{
    if (ctx == NULL || s == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_geometry_section_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_FileStructureGeometry) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* base.attribute_count */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;     /* base.name.same */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* exact_geometry.topo_context_count */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* user_data.stream_size */

    return 0;

fail:
    return s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;
}

/* PRC_TYPE_ASM_FileStructureExtraGeometry (Table 51): a real, independently-
   produced, tessellation-only compressed PRC file (used as a working
   reference specifically because it opens correctly in Acrobat) has SIX
   sections in its file structure's offset table (file-struct-header +
   five content sections); this write facility only ever produced five
   (file-struct-header + schema/globals + tree + tessellation + geometry),
   omitting ExtraGeometry entirely -- matching the pre-existing comment on
   prc_write_geometry_section_to_stream's caller about "some third-party
   PRC readers assum[ing] Table 6's fixed section set is always present
   and misread[ing] a later section's bytes as geometry otherwise". Field
   order/content mirrors prc_parse_file_extra_geometry exactly (prc_parse_
   file_structure.c): tag, ContentPRCBase (attribute_count, name), then
   extra_geom_count -- that parser does NOT consume a trailing user_data
   stream despite the struct having a user_data field, so this writer
   doesn't emit one either. */
int
prc_write_extra_geometry_section_to_stream(prc_context *ctx, prc_bit_write_state *s)
{
    if (ctx == NULL || s == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_extra_geometry_section_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_FileStructureExtraGeometry) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* base.attribute_count */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;     /* base.name.same */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* extra_geom_count */

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

void
prc_write_file_struct_header_bytes(uint8_t *out, uint32_t min_vers_for_read, uint32_t auth_vers)
{
    uint8_t *p = out;

    p[0] = 'P'; p[1] = 'R'; p[2] = 'C';
    p += PRC_WRITE_SIGNATURE_BYTES;
    p = prc_write_le_uint32(p, min_vers_for_read);
    p = prc_write_le_uint32(p, auth_vers);
    /* Must equal the main header's file_info[0].unique_id and the model
       section's far reference to this file structure -- see
       PRC_WRITE_FILE_STRUCT_UID0's doc comment in prc_write_common.h. */
    p = prc_write_le_unique_id(p, PRC_WRITE_FILE_STRUCT_UID0);
    p = prc_write_le_unique_id(p, PRC_WRITE_APP_UID0);
    p = prc_write_le_uint32(p, 0); /* file_count (embedded uncompressed files) */
}
