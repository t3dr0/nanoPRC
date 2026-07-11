/* INTERNAL DEVELOPMENT TOOL -- not part of the permanent test suite, not
   registered with CTest, no exit-code contract to keep stable.

   WHAT: Takes a REAL, independently-produced, complete raw .prc file and
   rebuilds it keeping the file-struct-header, schema/globals,
   tessellation, geometry, extra_geometry, and model-file sections byte-
   for-byte identical, but REGENERATES the tree section using this write
   facility's own encoder (prc_write_tree_to_stream) -- a minimal
   root+part tree whose one representation item references the real
   file's first tessellation entry (biased_index_tessellation=1) and
   carries no style reference (default_biased_style_index=0).

   HOW: usage: splice_tree_section.exe <real_input.prc> <output.prc>. The
   regenerated tree always has exactly 2 products (root last, biased
   index 2); this ONLY resolves correctly if the real input file's own
   model-file root_index also happens to be 2 (true of both reference
   files this was tested against, by coincidence of their own tree
   shape -- NOT guaranteed for an arbitrary input; check with scan_prc's
   DEBUG2 trace first).

   *** KNOWN INVALID FOR SCHEMA-EXTENDED FILES *** -- this tool's central
   premise (splice only the tree, keep the real schema) turned out to be
   methodologically broken for real files whose schema declares an
   extension for PRC_TYPE_ROOTBaseWithGraphics (entity_type=2 in the
   schema table -- common in real producer files that attach custom
   per-entity attributes, e.g. Title/Author/Description metadata). When
   that extension is declared, EVERY base-with-graphics record in the
   tree (every Part/Product/RepresentationItem) is expected to carry
   extra schema-defined data that this write facility's tree encoder
   never emits, causing an immediate bitstream desync -- NOT a genuine
   "tree encoding is wrong" result, just an invalid cross-generator
   splice. See splice_schema_tree_section.c for the fix (swap schema+tree
   together as a matched pair instead). Do not draw conclusions from a
   failure with this specific tool alone; confirm via the paired version
   first.

   WHY IT EXISTS: Superseded by splice_schema_tree_section.c once the
   schema-extension problem above was discovered. Kept for reference/
   history; prefer the paired tool for any new work. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prc_api.h"
#include "prc_context.h"
#include "prc_write_model.h"
#include "prc_write_tree.h"
#include "prc_write_common.h"

static uint32_t read_le_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void write_le_u32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)(v&0xff); p[1]=(uint8_t)((v>>8)&0xff); p[2]=(uint8_t)((v>>16)&0xff); p[3]=(uint8_t)((v>>24)&0xff); }

int main(int argc, char **argv)
{
    prc_context *ctx;
    FILE *fid;
    long fsize;
    uint8_t *buf;
    size_t read_size;
    const uint8_t *p;
    uint32_t section_count, root_biased_index;
    uint32_t *section_offsets;
    uint32_t start_offset, end_offset;
    uint32_t i;
    size_t header_prefix_len; /* everything up to and including section_count */
    prc_bit_write_state tree_s;
    uint8_t *tree_comp = NULL;
    size_t tree_comp_len = 0;
    uint32_t *new_section_offsets;
    uint32_t new_start_offset, new_end_offset;
    size_t new_total_size;
    uint8_t *out_buf;
    size_t section_off_table_byte_pos;
    FILE *out_fid;
    prc_api_write_rep_item rep_item;
    prc_api_write_node part_node, root;
    prc_api_write_node *part_node_ptr;

    if (argc < 3)
    {
        printf("usage: %s <real_input.prc> <output.prc>\n", argv[0]);
        return 2;
    }

    fid = fopen(argv[1], "rb");
    if (fid == NULL) { printf("failed to open %s\n", argv[1]); return 1; }
    fseek(fid, 0L, SEEK_END);
    fsize = ftell(fid);
    fseek(fid, 0L, SEEK_SET);
    buf = (uint8_t *)malloc((size_t)fsize);
    read_size = fread(buf, 1, (size_t)fsize, fid);
    fclose(fid);
    if (read_size != (size_t)fsize) { printf("short read\n"); return 1; }

    if (memcmp(buf, "PRC", 3) != 0) { printf("not a raw PRC file\n"); return 1; }

    p = buf + 3 + 4 + 4 + 16 + 16;
    if (read_le_u32(p) != 1) { printf("only single-file-structure inputs supported\n"); return 1; }
    p += 4;
    p += 16; /* fs_uid */
    p += 4;  /* reserved */
    section_off_table_byte_pos = (size_t)(p - buf) + 4; /* after section_count dword */
    section_count = read_le_u32(p); p += 4;
    header_prefix_len = (size_t)(p - buf);

    section_offsets = (uint32_t *)malloc(sizeof(uint32_t) * section_count);
    for (i = 0; i < section_count; i++) { section_offsets[i] = read_le_u32(p); p += 4; }
    start_offset = read_le_u32(p); p += 4;
    end_offset = read_le_u32(p); p += 4;

    if (section_count < 4) { printf("unexpected section_count=%u\n", section_count); return 1; }
    printf("Real file: section_offsets=[");
    for (i = 0; i < section_count; i++) printf("%u ", section_offsets[i]);
    printf("] start=%u end=%u\n", start_offset, end_offset);
    /* Section layout (0-indexed, matches prc_write_prc_buffer's own
       section_offsets[]): [0]=file_struct_header start, [1]=schema start
       (=file_struct_header end), [2]=tree start (=schema end), [3]=tess
       start (=tree end), [4]=geometry start, [5]=extra_geometry start,
       start_offset=model-file start (=extra_geometry end). */
    printf("Real tree section: %u bytes (offset %u..%u)\n", section_offsets[3] - section_offsets[2], section_offsets[2], section_offsets[3]);

    ctx = prc_api_new_context(NULL);
    if (ctx == NULL) { printf("context creation failed\n"); return 1; }

    /* Minimal root+part tree: 1 rep item, referencing real tessellation
       entry 1 (biased/1-based), no style reference. */
    memset(&rep_item, 0, sizeof(rep_item));
    rep_item.kind = PRC_API_WRITE_RI_SURFACE;
    rep_item.biased_tessellation_index = 1;
    rep_item.is_closed = 0;

    memset(&part_node, 0, sizeof(part_node));
    part_node.rep_items = &rep_item;
    part_node.num_rep_items = 1;
    part_node.bbox_min[0] = part_node.bbox_min[1] = part_node.bbox_min[2] = -1.2345678;
    part_node.bbox_max[0] = part_node.bbox_max[1] = part_node.bbox_max[2] = 1.2345678;
    part_node.name = NULL;
    part_node.part_name = NULL;

    part_node_ptr = &part_node;
    memset(&root, 0, sizeof(root));
    root.children = &part_node_ptr;
    root.num_children = 1;
    root.name = NULL;

    memset(&tree_s, 0, sizeof(tree_s));
    if (prc_bitwrite_init(ctx, &tree_s, 256) != 0) { printf("bitwrite_init failed\n"); return 1; }
    if (prc_write_tree_to_stream(ctx, &tree_s, &root, &root_biased_index, 1) != 0)
    {
        printf("prc_write_tree_to_stream failed\n");
        prc_api_print_error_stack(ctx);
        return 1;
    }
    if (prc_bitwrite_flush(ctx, &tree_s) != 0) { printf("bitwrite_flush failed\n"); return 1; }
    if (prc_write_deflate(ctx, tree_s.buf, tree_s.byte_pos, &tree_comp, &tree_comp_len) != 0)
    {
        printf("prc_write_deflate failed\n");
        return 1;
    }
    printf("Generated replacement tree section: %zu bytes compressed, root_biased_index=%u\n", tree_comp_len, root_biased_index);

    /* Rebuild section_offsets: [0]=header_prefix (same), [1]=same (schema
       start, unchanged), [2]=same (tree start, unchanged -- schema's END
       doesn't move), [3] = section_offsets[2] + our tree_comp_len (new
       tree section end / tess section start), then every later section
       shifts by the same delta but keeps its own real bytes. */
    new_section_offsets = (uint32_t *)malloc(sizeof(uint32_t) * section_count);
    new_section_offsets[0] = section_offsets[0];
    new_section_offsets[1] = section_offsets[1];
    new_section_offsets[2] = section_offsets[2];
    new_section_offsets[3] = section_offsets[2] + (uint32_t)tree_comp_len;
    {
        int32_t delta = (int32_t)new_section_offsets[3] - (int32_t)section_offsets[3];
        for (i = 4; i < section_count; i++)
            new_section_offsets[i] = (uint32_t)((int32_t)section_offsets[i] + delta);
        new_start_offset = (uint32_t)((int32_t)start_offset + delta);
        new_end_offset = (uint32_t)((int32_t)end_offset + delta);
    }
    new_total_size = (size_t)new_end_offset;

    out_buf = (uint8_t *)malloc(new_total_size);
    /* header prefix (signature..section_count) unchanged */
    memcpy(out_buf, buf, header_prefix_len);
    /* section_offsets[] table + start_offset + end_offset: patched */
    {
        uint8_t *q = out_buf + section_off_table_byte_pos;
        size_t real_trailer_pos = section_off_table_byte_pos + (size_t)4 * section_count + 4 + 4;
        for (i = 0; i < section_count; i++) { write_le_u32(q, new_section_offsets[i]); q += 4; }
        write_le_u32(q, new_start_offset); q += 4;
        write_le_u32(q, new_end_offset); q += 4;
        /* trailing file_count dword (multi-file support flag), unchanged -- must be
           copied explicitly, it isn't covered by header_prefix_len or the offset patch above. */
        memcpy(q, buf + real_trailer_pos, 4);
    }
    /* file-struct-header + schema (sections 0 and 1), unchanged content,
       unchanged position. */
    memcpy(out_buf + new_section_offsets[0], buf + section_offsets[0], section_offsets[2] - section_offsets[0]);
    /* our new tree section, at the real tree section's start */
    memcpy(out_buf + new_section_offsets[2], tree_comp, tree_comp_len);
    /* every remaining real section (tessellation, geometry, extra_geometry, model-file), unchanged content, shifted position */
    memcpy(out_buf + new_section_offsets[3], buf + section_offsets[3], end_offset - section_offsets[3]);

    out_fid = fopen(argv[2], "wb");
    if (out_fid == NULL) { printf("failed to open output %s\n", argv[2]); return 1; }
    fwrite(out_buf, 1, new_total_size, out_fid);
    fclose(out_fid);

    printf("Wrote %s (%zu bytes)\n", argv[2], new_total_size);

    prc_free(ctx, tree_comp);
    prc_bitwrite_release(ctx, &tree_s);
    prc_api_release_context(ctx);
    free(section_offsets);
    free(new_section_offsets);
    free(out_buf);
    free(buf);
    return 0;
}
