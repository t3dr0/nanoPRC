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
#include "prc_write_model.h"
#include "prc_data.h"

int
prc_write_model_file_to_stream(prc_context *ctx, prc_bit_write_state *s,
    uint32_t root_unique_id, uint32_t file_struct_count)
{
    uint32_t k;

    if (ctx == NULL || s == NULL || file_struct_count == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_model_file_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* leading uint32 the parser skips unexplained */
    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_ModelFile) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* base.attribute_count */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;     /* base.name.same */

    if (prc_bitwrite_bit(ctx, s, 0) != 0) goto fail;      /* units_from_CAD_flag */
    if (prc_bitwrite_double(ctx, s, 1.0) != 0) goto fail; /* units_from_CAD_file */

    if (prc_bitwrite_uint32(ctx, s, 1) != 0) goto fail; /* number_of_root_product_occurrences */

    /* ProductOccurrenceReference: unique_id + root_index + is_active */
    if (prc_bitwrite_uint32(ctx, s, root_unique_id) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* root_index */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;    /* product_occurence_is_active */

    for (k = 0; k < file_struct_count; k++)
        if (prc_bitwrite_uint32(ctx, s, k) != 0) goto fail; /* file_structure_index_in_model_file[k] */

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* user_data.stream_size */

    return 0;

fail:
    return s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;
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

/* Table 5/35 main prc_header, single file structure only: "PRC" +
   min_vers_for_read + auth_vers + 2 unique ids + filestructure_count(=1) +
   [unique_id + 4-byte reserved/pad + section_count + section_offset[]] +
   start_offset + end_offset + file_count(=0, no multi-file). */
static size_t
prc_write_main_header_size(uint32_t section_count)
{
    return 3u + 4u + 4u + 16u + 16u + 4u   /* signature..filestructure_count */
         + 16u + 4u + 4u + 4u * section_count /* the one file structure's own table */
         + 4u + 4u + 4u;                    /* start_offset, end_offset, file_count */
}

static void
prc_write_main_header_bytes(uint8_t *out, uint32_t section_count, const uint32_t *section_offsets,
    uint32_t start_offset, uint32_t end_offset)
{
    uint8_t *p = out;
    uint32_t i;

    p[0] = 'P'; p[1] = 'R'; p[2] = 'C';
    p += 3;
    p = prc_write_le_uint32(p, 0); /* min_vers_for_read: 0 accepts any reader */
    p = prc_write_le_uint32(p, 0); /* auth_vers */
    for (i = 0; i < 4; i++) p = prc_write_le_uint32(p, 0); /* unique_id_file */
    for (i = 0; i < 4; i++) p = prc_write_le_uint32(p, 0); /* unique_id_application */
    p = prc_write_le_uint32(p, 1); /* filestructure_count */

    for (i = 0; i < 4; i++) p = prc_write_le_uint32(p, 0); /* file_info[0].unique_id */
    p = prc_write_le_uint32(p, 0); /* 4-byte reserved field the parser skips past */
    p = prc_write_le_uint32(p, section_count);
    for (i = 0; i < section_count; i++)
        p = prc_write_le_uint32(p, section_offsets[i]);

    p = prc_write_le_uint32(p, start_offset);
    p = prc_write_le_uint32(p, end_offset);
    p = prc_write_le_uint32(p, 0); /* file_count: multi-file not supported */
}

int
prc_write_prc_file(prc_context *ctx, const char *filename,
    const prc_write_global_tables *tables,
    const prc_write_tree_node *root,
    const prc_write_tess_entry *tess_entries, uint32_t num_tess_entries)
{
    prc_bit_write_state schema_s, tree_s, tess_s, model_s;
    uint8_t *schema_comp = NULL, *tree_comp = NULL, *tess_comp = NULL, *model_comp = NULL;
    size_t schema_comp_len = 0, tree_comp_len = 0, tess_comp_len = 0, model_comp_len = 0;
    uint32_t root_unique_id = 0;
    FILE *fid = NULL;
    int ret = PRC_ERROR_INTERNAL;
    const uint32_t section_count = 4; /* file-struct-header(implicit) + schema_globals + tree + tessellation */
    uint32_t section_offsets[4];
    uint32_t start_offset = 0, end_offset = 0;
    size_t header_size;
    uint8_t *header_buf = NULL;

    memset(&schema_s, 0, sizeof(schema_s));
    memset(&tree_s, 0, sizeof(tree_s));
    memset(&tess_s, 0, sizeof(tess_s));
    memset(&model_s, 0, sizeof(model_s));

    if (ctx == NULL || filename == NULL || tables == NULL || root == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_prc_file: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_init(ctx, &schema_s, 1024) != 0) goto cleanup;
    if (prc_write_schema_and_globals_to_stream(ctx, &schema_s, tables) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &schema_s) != 0) goto cleanup;

    if (prc_bitwrite_init(ctx, &tree_s, 1024) != 0) goto cleanup;
    if (prc_write_tree_to_stream(ctx, &tree_s, root, &root_unique_id) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &tree_s) != 0) goto cleanup;

    if (prc_bitwrite_init(ctx, &tess_s, 1024) != 0) goto cleanup;
    if (prc_write_tessellation_section_to_stream(ctx, &tess_s, tess_entries, num_tess_entries) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &tess_s) != 0) goto cleanup;

    if (prc_bitwrite_init(ctx, &model_s, 256) != 0) goto cleanup;
    if (prc_write_model_file_to_stream(ctx, &model_s, root_unique_id, 1) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &model_s) != 0) goto cleanup;

    if (prc_write_deflate(ctx, schema_s.buf, schema_s.byte_pos, &schema_comp, &schema_comp_len) != 0) goto cleanup;
    if (prc_write_deflate(ctx, tree_s.buf, tree_s.byte_pos, &tree_comp, &tree_comp_len) != 0) goto cleanup;
    if (prc_write_deflate(ctx, tess_s.buf, tess_s.byte_pos, &tess_comp, &tess_comp_len) != 0) goto cleanup;
    if (prc_write_deflate(ctx, model_s.buf, model_s.byte_pos, &model_comp, &model_comp_len) != 0) goto cleanup;

    header_size = prc_write_main_header_size(section_count);
    header_buf = (uint8_t *)prc_malloc(ctx, header_size);
    if (header_buf == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_prc_file\n");
        goto cleanup;
    }
    memset(header_buf, 0, header_size);

    fid = fopen(filename, "wb");
    if (fid == NULL)
    {
        prc_error(ctx, PRC_ERROR_IO, "prc_write_prc_file: failed to open output file\n");
        goto cleanup;
    }

    /* Reserve the header's fixed-size space with a placeholder; every
       offset field in it is patched in a single fseek+fwrite at the end,
       once every section's real position is known. */
    if (fwrite(header_buf, 1, header_size, fid) != header_size) goto io_fail;

    section_offsets[0] = (uint32_t)ftell(fid);
    {
        uint8_t fh[PRC_WRITE_FILE_STRUCT_HEADER_SIZE];
        prc_write_file_struct_header_bytes(fh, 0, 0);
        if (fwrite(fh, 1, sizeof(fh), fid) != sizeof(fh)) goto io_fail;
    }

    /* The model section is addressed via start_offset/end_offset directly,
       not through section_offset[] -- see prc_parse_main.c's
       "the model file is defined special" handling. */
    start_offset = (uint32_t)ftell(fid);
    if (fwrite(model_comp, 1, model_comp_len, fid) != model_comp_len) goto io_fail;
    end_offset = (uint32_t)ftell(fid);

    section_offsets[1] = (uint32_t)ftell(fid);
    if (fwrite(schema_comp, 1, schema_comp_len, fid) != schema_comp_len) goto io_fail;

    section_offsets[2] = (uint32_t)ftell(fid);
    if (fwrite(tree_comp, 1, tree_comp_len, fid) != tree_comp_len) goto io_fail;

    section_offsets[3] = (uint32_t)ftell(fid);
    if (fwrite(tess_comp, 1, tess_comp_len, fid) != tess_comp_len) goto io_fail;

    prc_write_main_header_bytes(header_buf, section_count, section_offsets, start_offset, end_offset);
    if (fseek(fid, 0, SEEK_SET) != 0) goto io_fail;
    if (fwrite(header_buf, 1, header_size, fid) != header_size) goto io_fail;

    ret = 0;
    goto cleanup;

io_fail:
    prc_error(ctx, PRC_ERROR_IO, "prc_write_prc_file: I/O error writing output file\n");
    ret = PRC_ERROR_IO;

cleanup:
    if (fid != NULL) fclose(fid);
    if (header_buf != NULL) prc_free(ctx, header_buf);
    if (schema_comp != NULL) prc_free(ctx, schema_comp);
    if (tree_comp != NULL) prc_free(ctx, tree_comp);
    if (tess_comp != NULL) prc_free(ctx, tess_comp);
    if (model_comp != NULL) prc_free(ctx, model_comp);
    prc_bitwrite_release(ctx, &schema_s);
    prc_bitwrite_release(ctx, &tree_s);
    prc_bitwrite_release(ctx, &tess_s);
    prc_bitwrite_release(ctx, &model_s);
    return ret;
}
