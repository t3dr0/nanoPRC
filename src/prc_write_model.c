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
    const char *model_name, uint32_t root_biased_index, uint32_t file_struct_count)
{
    uint32_t k;

    if (ctx == NULL || s == NULL || file_struct_count == 0)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_model_file_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* leading uint32 the parser skips unexplained */
    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_ModelFile) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;        /* base.attribute_count */
    if (prc_write_name(ctx, s, model_name) != 0) goto fail;    /* base.name */

    if (prc_bitwrite_bit(ctx, s, 0) != 0) goto fail;      /* units_from_CAD_flag */
    if (prc_bitwrite_double(ctx, s, 1.0) != 0) goto fail; /* units_from_CAD_file */

    if (prc_bitwrite_uint32(ctx, s, 1) != 0) goto fail; /* number_of_root_product_occurrences */

    /* ProductOccurrenceReference: unique_id + root_index + is_active.
       `unique_id` here is a FAR REFERENCE to the FILE STRUCTURE that owns
       the actual root product occurrence -- NOT the product's own
       tree-assigned ContentPRCRefBase.unique_id (confirmed the hard way:
       an external reader failed with "can not find FileStructure
       {1,0,0,0}" when this was written as the tree's root unique_id).
       This write facility only ever emits one file structure, so the
       reference here must match that file structure's own identity
       (PRC_WRITE_FILE_STRUCT_UID0 -- see its doc comment in
       prc_write_common.h for how this was confirmed against a real PRC
       stream), not the root product's identity.

       root_index also turned out to be a BIASED (1-based, 0 = none) index
       into that file structure's products[] array, like every other
       cross-reference in this format -- NOT a plain 0-based index despite
       its parser-side field name. Confirmed against a real PRC stream: a
       2-product file structure had root_index=2 pointing at its LAST
       product (0-based index 1); writing a plain 0-based root_index=0
       here was silently treated as "no root", which produced an "Empty
       Scene Detected" error even though every other part of the file
       parsed and resolved correctly. root_biased_index is
       prc_write_tree_to_stream's *root_biased_index_out, which -- since
       the tree encoder always places the root last -- equals that file
       structure's total product count. */
    if (prc_bitwrite_uint32(ctx, s, PRC_WRITE_FILE_STRUCT_UID0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, root_biased_index) != 0) goto fail; /* root_index */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) goto fail;    /* product_occurence_is_active */

    for (k = 0; k < file_struct_count; k++)
        if (prc_bitwrite_uint32(ctx, s, k) != 0) goto fail; /* file_structure_index_in_model_file[k] */

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) goto fail; /* user_data.stream_size */

    return 0;

fail:
    return s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;
}

/* Table 5/35 main prc_header, single file structure only. This is the ONE
   place that knows the field order/sizes; prc_write_main_header_bytes (the
   writer) and every reader of a field position (prc_write_main_header_size,
   and any external caller such as a test independently re-reading
   start_offset/end_offset out of a written file) go through this function
   instead of separately re-deriving byte offsets, so the two can't
   silently drift apart when a field is added, removed, or the section
   count changes. */
void
prc_write_main_header_compute_layout(uint32_t section_count, prc_write_main_header_layout *out)
{
    size_t pos = 0;

    pos += PRC_WRITE_SIGNATURE_BYTES; /* "PRC" */
    pos += PRC_WRITE_U32_BYTES;       /* min_vers_for_read */
    pos += PRC_WRITE_U32_BYTES;       /* auth_vers */
    pos += PRC_WRITE_UNIQUE_ID_BYTES; /* unique_id_file */
    pos += PRC_WRITE_UNIQUE_ID_BYTES; /* unique_id_application */
    pos += PRC_WRITE_U32_BYTES;       /* filestructure_count */

    pos += PRC_WRITE_UNIQUE_ID_BYTES; /* file_info[0].unique_id */
    pos += PRC_WRITE_U32_BYTES;       /* reserved/pad field the parser skips past */
    pos += PRC_WRITE_U32_BYTES;       /* section_count */

    out->section_offset_table_pos = pos;
    pos += (size_t)PRC_WRITE_U32_BYTES * section_count; /* section_offset[] */

    out->start_offset_pos = pos;
    pos += PRC_WRITE_U32_BYTES;
    out->end_offset_pos = pos;
    pos += PRC_WRITE_U32_BYTES;
    pos += PRC_WRITE_U32_BYTES; /* file_count */

    out->total_size = pos;
}

static size_t
prc_write_main_header_size(uint32_t section_count)
{
    prc_write_main_header_layout layout;

    prc_write_main_header_compute_layout(section_count, &layout);
    return layout.total_size;
}

static void
prc_write_main_header_bytes(uint8_t *out, uint32_t section_count, const uint32_t *section_offsets,
    uint32_t start_offset, uint32_t end_offset)
{
    uint8_t *p = out;
    uint32_t i;

    p[0] = 'P'; p[1] = 'R'; p[2] = 'C';
    p += PRC_WRITE_SIGNATURE_BYTES;
    p = prc_write_le_uint32(p, 0); /* min_vers_for_read: 0 accepts any reader */
    p = prc_write_le_uint32(p, 0); /* auth_vers */
    p = prc_write_le_unique_id(p, PRC_WRITE_FILE_UID0);
    p = prc_write_le_unique_id(p, PRC_WRITE_APP_UID0);
    p = prc_write_le_uint32(p, 1); /* filestructure_count */

    /* Must equal the file-structure header's own unique_id_file
       (prc_write_file_struct_header_bytes) and the model section's far
       reference to this file structure (prc_write_model_file_to_stream) --
       see PRC_WRITE_FILE_STRUCT_UID0's doc comment in prc_write_common.h. */
    p = prc_write_le_unique_id(p, PRC_WRITE_FILE_STRUCT_UID0);
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
    const char *model_name,
    const prc_write_global_tables *tables,
    const prc_write_tree_node *root,
    const prc_write_tess_entry *tess_entries, uint32_t num_tess_entries)
{
    prc_bit_write_state schema_s, tree_s, tess_s, geom_s, model_s;
    uint8_t *schema_comp = NULL, *tree_comp = NULL, *tess_comp = NULL, *geom_comp = NULL, *model_comp = NULL;
    size_t schema_comp_len = 0, tree_comp_len = 0, tess_comp_len = 0, geom_comp_len = 0, model_comp_len = 0;
    uint32_t root_biased_index = 0;
    FILE *fid = NULL;
    int ret = PRC_ERROR_INTERNAL;
    /* See PRC_WRITE_PRC_FILE_SECTION_COUNT's doc comment: file-struct-header
       (implicit) + schema_globals + tree + tessellation + geometry. The
       (always-empty) geometry section is written even though this write
       facility never produces exact B-Rep content: some third-party PRC
       readers assume Table 6's fixed section set is always present and
       misread a later section's bytes as geometry otherwise -- see
       prc_write_geometry_section_to_stream. */
    const uint32_t section_count = PRC_WRITE_PRC_FILE_SECTION_COUNT;
    uint32_t section_offsets[PRC_WRITE_PRC_FILE_SECTION_COUNT];
    uint32_t start_offset = 0, end_offset = 0;
    size_t header_size;
    uint8_t *header_buf = NULL;

    memset(&schema_s, 0, sizeof(schema_s));
    memset(&tree_s, 0, sizeof(tree_s));
    memset(&tess_s, 0, sizeof(tess_s));
    memset(&geom_s, 0, sizeof(geom_s));
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
    if (prc_write_tree_to_stream(ctx, &tree_s, root, &root_biased_index) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &tree_s) != 0) goto cleanup;

    if (prc_bitwrite_init(ctx, &tess_s, 1024) != 0) goto cleanup;
    if (prc_write_tessellation_section_to_stream(ctx, &tess_s, tess_entries, num_tess_entries) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &tess_s) != 0) goto cleanup;

    if (prc_bitwrite_init(ctx, &geom_s, 64) != 0) goto cleanup;
    if (prc_write_geometry_section_to_stream(ctx, &geom_s) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &geom_s) != 0) goto cleanup;

    if (prc_bitwrite_init(ctx, &model_s, 256) != 0) goto cleanup;
    if (prc_write_model_file_to_stream(ctx, &model_s, model_name, root_biased_index, 1) != 0) goto cleanup;
    if (prc_bitwrite_flush(ctx, &model_s) != 0) goto cleanup;

    if (prc_write_deflate(ctx, schema_s.buf, schema_s.byte_pos, &schema_comp, &schema_comp_len) != 0) goto cleanup;
    if (prc_write_deflate(ctx, tree_s.buf, tree_s.byte_pos, &tree_comp, &tree_comp_len) != 0) goto cleanup;
    if (prc_write_deflate(ctx, tess_s.buf, tess_s.byte_pos, &tess_comp, &tess_comp_len) != 0) goto cleanup;
    if (prc_write_deflate(ctx, geom_s.buf, geom_s.byte_pos, &geom_comp, &geom_comp_len) != 0) goto cleanup;
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

    section_offsets[1] = (uint32_t)ftell(fid);
    if (fwrite(schema_comp, 1, schema_comp_len, fid) != schema_comp_len) goto io_fail;

    section_offsets[2] = (uint32_t)ftell(fid);
    if (fwrite(tree_comp, 1, tree_comp_len, fid) != tree_comp_len) goto io_fail;

    section_offsets[3] = (uint32_t)ftell(fid);
    if (fwrite(tess_comp, 1, tess_comp_len, fid) != tess_comp_len) goto io_fail;

    section_offsets[4] = (uint32_t)ftell(fid);
    if (fwrite(geom_comp, 1, geom_comp_len, fid) != geom_comp_len) goto io_fail;

    /* The model section is addressed via start_offset/end_offset directly,
       not through section_offset[] -- see prc_parse_main.c's "the model
       file is defined special" handling -- but it must still be placed
       LAST, immediately after every regular section, not merely somewhere
       else in the file. Confirmed against a real, known-good PRC stream
       (extracted from examples/cube.pdf): its start_offset/end_offset
       field values are the file's last two dwords, with end_offset ==
       the total file size. External testing showed at least one
       third-party reader relies on this contiguity -- it derives the last
       regular section's end as the model's start_offset, so placing the
       model section anywhere else produces a bogus (tiny) "end" for the
       real last regular section. */
    start_offset = (uint32_t)ftell(fid);
    if (fwrite(model_comp, 1, model_comp_len, fid) != model_comp_len) goto io_fail;
    end_offset = (uint32_t)ftell(fid);

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
    if (geom_comp != NULL) prc_free(ctx, geom_comp);
    if (model_comp != NULL) prc_free(ctx, model_comp);
    prc_bitwrite_release(ctx, &schema_s);
    prc_bitwrite_release(ctx, &tree_s);
    prc_bitwrite_release(ctx, &tess_s);
    prc_bitwrite_release(ctx, &geom_s);
    prc_bitwrite_release(ctx, &model_s);
    return ret;
}
