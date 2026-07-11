/* INTERNAL DEVELOPMENT TOOL -- not part of the permanent test suite, not
   registered with CTest, no exit-code contract to keep stable.

   WHAT: The "field-preserving re-encoder". Takes a REAL, complete,
   independently-produced raw .prc file's tessellation section
   (specifically its first PRC_TYPE_TESS_3D_Compressed entry, tess_count
   must be 1), fully DECODES it with nanoPRC's own parser, then RE-
   ENCODES the exact same decoded values through nanoPRC's own writer
   (prc_write_compress_tess_to_stream) -- reusing the real production
   encoder, just fed parsed-from-real data instead of a fresh mesh
   traversal. Everything else in the file (schema, tree, geometry,
   extra_geometry, model-file) is left byte-for-byte untouched real
   content.

   HOW: usage: reencode_real_tess.exe <real_input.prc> <output.prc>
   [real_inflated.bin reencoded_inflated.bin]. The optional trailing pair of
   paths dumps the two INFLATED (pre-deflate) bitstreams -- the real
   section's and our re-encoded one's -- so they can be diffed bit-for-bit
   directly; comparing the deflated/compressed bytes instead would be
   meaningless since zlib doesn't guarantee byte-identical output for
   logically identical input. No deliberate mutation is applied -- this is a "zero mutation" fidelity
   test: same is_calculated/has_faces/tolerance/origin/point_array/
   edge_status_array/triangle_face_array/points_is_reference_array/
   point_reference_array/must_recalculate_normals/normal data/
   is_face_planar values in, same values re-encoded out through our own
   bit-packing code. Prints the parsed field values so a human can sanity-
   check them against the input before trusting the re-encoded output.

   WHY IT EXISTS / WHAT IT FOUND: if this write facility's bit-level
   encoding process cannot faithfully reproduce a working file's
   tessellation record even when fed 100% real, correct values, that
   proves a bug in HOW compressed tessellation is bit-packed, independent
   of WHICH values are chosen -- something no amount of field-value
   comparison against spec text could reveal on its own. Using this tool:
   (1) found and led to fixing a real production bug -- prc_write_
   compress_tess_to_stream always hardcoded is_face_planar=0 for every
   face with no way to express genuine per-face planarity, which
   corrupted normal reconstruction for any real file with actually-planar
   faces (see the is_face_planar parameter this tool exercises, and the
   commit that added it). (2) After that fix, the zero-mutation re-
   encoded output STILL shows a blank model tree in Adobe Acrobat, while
   opening correctly in nanoPRC's own reader, the independent ground-
   truth reader, AND PDF-XChange. This is the most decisive finding of
   the whole investigation: the bug is confirmed to be in the bit-level
   encoding mechanics themselves (not value selection), and Acrobat
   specifically is more sensitive to it than every other reader tried.
   See the "Acrobat blank-tree investigation" project notes for the full
   picture and the next planned step (verifying the encoder's gate-
   emission traversal order against the oracle document's exact
   pseudocode -- the one remaining unverified piece).

   LIMITATIONS: Only handles single-file-structure, tess_count==1,
   unnamed-wrapper inputs (aborts with a clear message otherwise). No
   mutation capability yet -- despite the tool's original design intent
   (see project notes), only the zero-mutation baseline was ever
   implemented/tested; adding a flag to flip one specific field before
   re-encoding, now that the baseline itself is confirmed to round-trip
   structurally, is natural follow-up work for whoever picks this back
   up. The section-level splice mechanics (raw main-header parsing,
   offset-table patching) are duplicated from the splice_*.c tools rather
   than shared -- see those files for the same caveats (single-file-
   structure only, etc). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "prc_data.h"
#include "prc_api.h"
#include "prc_context.h"
#include "prc_bit.h"
#include "prc_parse_tess.h"
#include "prc_parse_common.h"
#include "prc_write_compress_tess.h"
#include "prc_write_file_structure.h"
#include "prc_write_common.h"
#include "zlib.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    uint32_t section_count;
    uint32_t *section_offsets;
    uint32_t start_offset, end_offset;
    uint32_t i;
    size_t header_prefix_len;
    size_t section_off_table_byte_pos;
    uint8_t *tess_inflated = NULL;
    size_t dest_len_saved = 0;
    int code;
    prc_bit_state bit_state;
    uint32_t wrapper_tag, attribute_count;
    prc_name wrapper_name;
    uint32_t tess_count;
    uint32_t entry_tag;
    prc_tess_3d_compressed *parsed = NULL;
    prc_encode_traversal_result trav;
    double crease_angle_degrees = 0.0;
    uint8_t *new_tess_comp = NULL;
    size_t new_tess_comp_len = 0;
    prc_bit_write_state wrapper_s;
    uint8_t *wrapper_comp = NULL;
    size_t wrapper_comp_len = 0;
    uint32_t k;
    uint32_t *new_section_offsets;
    uint32_t new_start_offset, new_end_offset;
    size_t new_total_size;
    uint8_t *out_buf;
    FILE *out_fid;
    int32_t *triangle_face_array_signed = NULL;

    if (argc < 3)
    {
        printf("usage: %s <real_input.prc> <output.prc> [real_inflated.bin reencoded_inflated.bin]\n", argv[0]);
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
    section_off_table_byte_pos = (size_t)(p - buf) + 4;
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

    ctx = prc_api_new_context(NULL);
    if (ctx == NULL) { printf("context creation failed\n"); return 1; }

    /* Inflate the real tessellation section (section index 3: offsets[3]..offsets[4]) */
    {
        uLongf dest_len;
        uint32_t comp_len = section_offsets[4] - section_offsets[3];
        /* generous upper bound; grow if needed */
        dest_len = comp_len * 40u + 4096u;
        tess_inflated = (uint8_t *)malloc(dest_len);
        for (;;)
        {
            uLongf try_len = dest_len;
            int zret = uncompress(tess_inflated, &try_len, buf + section_offsets[3], comp_len);
            if (zret == Z_OK) { dest_len = try_len; break; }
            if (zret == Z_BUF_ERROR) { dest_len *= 2; free(tess_inflated); tess_inflated = (uint8_t *)malloc(dest_len); continue; }
            printf("zlib uncompress failed: %d\n", zret);
            return 1;
        }
        printf("Inflated tessellation section: %lu bytes\n", (unsigned long)dest_len);
        dest_len_saved = (size_t)dest_len;

        prc_init_bit_state(ctx, &bit_state, tess_inflated, (size_t)dest_len);
    }

    /* Section wrapper: tag + ContentPRCBase (attribute_count/attributes + name) + tess_count.
       Uses the real parser (handles attribute_count > 0 correctly, unlike a
       raw attribute_count read followed directly by a name-bit read, which
       would desync if any attribute entries were actually present). */
    wrapper_tag = prc_bitread_uint32(ctx, &bit_state);
    if (wrapper_tag != PRC_TYPE_ASM_FileStructureTessellation)
    {
        printf("unexpected wrapper tag %u\n", wrapper_tag);
        return 1;
    }
    {
        prc_content_prc_base wrapper_base;
        memset(&wrapper_base, 0, sizeof(wrapper_base));
        code = prc_parse_content_prc_base(ctx, &bit_state, &wrapper_base);
        if (code < 0)
        {
            printf("prc_parse_content_prc_base failed: code=%d\n", code);
            prc_api_print_error_stack(ctx);
            return 1;
        }
        attribute_count = wrapper_base.attribute_data.attribute_count;
        wrapper_name = wrapper_base.name;
    }
    tess_count = prc_bitread_uint32(ctx, &bit_state);
    printf("Wrapper: attribute_count=%u name.same=%u tess_count=%u\n", attribute_count, wrapper_name.same, tess_count);
    if (tess_count != 1)
    {
        printf("this tool only handles tess_count==1 files (got %u)\n", tess_count);
        return 1;
    }

    entry_tag = prc_bitread_uint32(ctx, &bit_state);
    if (entry_tag != PRC_TYPE_TESS_3D_Compressed)
    {
        printf("first tessellation entry is not COMPRESSED (tag=%u) -- not handled by this tool\n", entry_tag);
        return 1;
    }

    code = prc_parse_tess_3d_compressed(ctx, &bit_state, &parsed, 0);
    if (code < 0)
    {
        printf("prc_parse_tess_3d_compressed failed: code=%d\n", code);
        prc_api_print_error_stack(ctx);
        return 1;
    }

    printf("Parsed real tessellation: is_calculated=%u has_faces=%u tolerance=%.17g origin=(%f,%f,%f)\n",
        parsed->is_calculated, parsed->has_faces, parsed->tolerance,
        parsed->origin_array.x, parsed->origin_array.y, parsed->origin_array.z);
    printf("  point_array_size=%u edge_status_array_size=%u triangle_face_array_size=%u face_number=%u\n",
        parsed->point_array_size, parsed->edge_status_array_size, parsed->triangle_face_array_size, parsed->face_number);
    printf("  reference_array_size=%u point_reference_array_size=%u must_recalculate_normals=%u\n",
        parsed->reference_array_size, parsed->point_reference_array_size, parsed->must_recalculate_normals);
    if (parsed->must_recalculate_normals)
        printf("  crease_angle(rad)=%.17g\n", parsed->crease_angle);
    else
        printf("  normal_angle_number_of_bits=%u normal_binary_data_size=%u normal_angle_array_size=%u\n",
            parsed->normal_angle_number_of_bits, parsed->normal_binary_data_size, parsed->normal_angle_array_size);
    printf("  is_point_color=%u point_color_array_size=%u\n",
        parsed->is_point_color, parsed->point_color_array_size);
    printf("  is_multiple_line_attribute=%u line_attribute_array_size=%u\n",
        parsed->is_multiple_line_attribute, parsed->line_attribute_array_size);
    printf("  no_texture=%u has_behaviors=%u behaviors_array_size=%u\n",
        parsed->no_texture, parsed->has_behaviors, parsed->behaviors_array_size);

    /* Build the traversal-result struct from parsed values -- no mutation. */
    memset(&trav, 0, sizeof(trav));
    trav.point_array = parsed->point_array;
    trav.point_array_size = parsed->point_array_size;
    /* Our writer pads edge_status_array to 3*T itself; feed it exactly T
       (== triangle_face_array_size) real entries, matching how it's
       normally called from a fresh traversal. */
    trav.edge_status_array = parsed->edge_status_array;
    trav.edge_status_array_size = parsed->triangle_face_array_size;

    triangle_face_array_signed = (int32_t *)malloc(sizeof(int32_t) * (parsed->triangle_face_array_size > 0 ? parsed->triangle_face_array_size : 1));
    for (k = 0; k < parsed->triangle_face_array_size; k++)
        triangle_face_array_signed[k] = (int32_t)parsed->triangle_face_array[k];
    trav.triangle_face_array = triangle_face_array_signed;
    trav.triangle_face_array_size = parsed->triangle_face_array_size;

    trav.points_is_reference_array = parsed->points_is_reference_array;
    trav.points_is_reference_array_size = parsed->reference_array_size;
    trav.point_reference_array = parsed->point_reference_array;
    trav.point_reference_array_size = parsed->point_reference_array_size;
    trav.origin[0] = parsed->origin_array.x;
    trav.origin[1] = parsed->origin_array.y;
    trav.origin[2] = parsed->origin_array.z;

    if (parsed->must_recalculate_normals)
        crease_angle_degrees = parsed->crease_angle * 180.0 / M_PI;

    /* Write the section wrapper AND the entry body into ONE continuous
       bitstream (matching prc_write_tessellation_section_to_stream's own
       structure exactly) -- NOT into two separately-flushed bit_write_
       states spliced together afterward. The wrapper's own bits
       (32+32+1+32 = 97) are not byte-aligned, so flushing the entry body
       to a byte boundary on its own and then byte-copying it into the
       wrapper stream shifts every subsequent bit by 1 and corrupts
       everything after the splice point -- confirmed the hard way (this
       tool's first version did exactly that and produced a file that
       failed to parse at all, even through our own reader). */
    memset(&wrapper_s, 0, sizeof(wrapper_s));
    if (prc_bitwrite_init(ctx, &wrapper_s, 4096) != 0) { printf("wrapper init failed\n"); return 1; }
    if (prc_bitwrite_uint32(ctx, &wrapper_s, PRC_TYPE_ASM_FileStructureTessellation) != 0) { printf("wrapper tag failed\n"); return 1; }
    if (prc_bitwrite_uint32(ctx, &wrapper_s, 0) != 0) { printf("wrapper attr failed\n"); return 1; } /* attribute_count=0 */
    if (prc_bitwrite_bit(ctx, &wrapper_s, 1) != 0) { printf("wrapper name failed\n"); return 1; }    /* name.same=1 */
    if (prc_bitwrite_uint32(ctx, &wrapper_s, 1) != 0) { printf("wrapper tess_count failed\n"); return 1; } /* tess_count=1 */
    if (prc_bitwrite_uint32(ctx, &wrapper_s, PRC_TYPE_TESS_3D_Compressed) != 0) { printf("entry tag failed\n"); return 1; }
    code = prc_write_compress_tess_to_stream(ctx, &wrapper_s, &trav, parsed->tolerance,
        parsed->must_recalculate_normals ? parsed->normal_is_reversed : NULL,
        crease_angle_degrees,
        parsed->must_recalculate_normals ? NULL : parsed->normal_angle_array,
        parsed->must_recalculate_normals ? 0 : parsed->normal_angle_array_size,
        parsed->must_recalculate_normals ? NULL : (const uint8_t *)parsed->normal_binary_data,
        parsed->must_recalculate_normals ? 0 : parsed->normal_binary_data_size,
        parsed->must_recalculate_normals,
        parsed->must_recalculate_normals ? NULL : (const uint8_t *)parsed->is_face_planar);
    if (code != 0)
    {
        printf("prc_write_compress_tess_to_stream failed: code=%d\n", code);
        prc_api_print_error_stack(ctx);
        return 1;
    }
    if (prc_bitwrite_flush(ctx, &wrapper_s) != 0) { printf("wrapper flush failed\n"); return 1; }

    /* Dump both INFLATED (pre-deflate) bitstreams so a bit-level diff can be
       done directly -- deflate output can legitimately differ byte-for-byte
       between two zlib invocations of logically identical data (different
       compression-level defaults, etc), so comparing compressed bytes would
       be meaningless. The uncompressed wrapper bitstream is the real
       ground truth for whether our bit-packing matches the real file's. */
    if (argc >= 5)
    {
        FILE *dbg_fid;
        dbg_fid = fopen(argv[3], "wb");
        if (dbg_fid != NULL) { fwrite(tess_inflated, 1, dest_len_saved, dbg_fid); fclose(dbg_fid); }
        dbg_fid = fopen(argv[4], "wb");
        if (dbg_fid != NULL) { fwrite(wrapper_s.buf, 1, wrapper_s.byte_pos, dbg_fid); fclose(dbg_fid); }
        printf("Dumped inflated bitstreams: real=%s (%lu bytes) reencoded=%s (%zu bytes)\n",
            argv[3], (unsigned long)dest_len_saved, argv[4], wrapper_s.byte_pos);
    }

    if (prc_write_deflate(ctx, wrapper_s.buf, wrapper_s.byte_pos, &wrapper_comp, &wrapper_comp_len) != 0)
    {
        printf("deflate failed\n");
        return 1;
    }
    printf("Re-encoded tessellation section: %zu bytes compressed (real was %u bytes)\n",
        wrapper_comp_len, section_offsets[4] - section_offsets[3]);

    /* Splice into the original file: sections [0..3) unchanged, section 3
       (tessellation) replaced, sections [4..end) unchanged content, shifted position. */
    new_section_offsets = (uint32_t *)malloc(sizeof(uint32_t) * section_count);
    for (i = 0; i <= 3; i++) new_section_offsets[i] = section_offsets[i];
    new_section_offsets[4] = section_offsets[3] + (uint32_t)wrapper_comp_len;
    {
        int32_t delta = (int32_t)new_section_offsets[4] - (int32_t)section_offsets[4];
        for (i = 5; i < section_count; i++)
            new_section_offsets[i] = (uint32_t)((int32_t)section_offsets[i] + delta);
        new_start_offset = (uint32_t)((int32_t)start_offset + delta);
        new_end_offset = (uint32_t)((int32_t)end_offset + delta);
    }
    new_total_size = (size_t)new_end_offset;

    out_buf = (uint8_t *)malloc(new_total_size);
    memcpy(out_buf, buf, header_prefix_len);
    {
        uint8_t *q = out_buf + section_off_table_byte_pos;
        size_t real_trailer_pos = section_off_table_byte_pos + (size_t)4 * section_count + 4 + 4;
        for (i = 0; i < section_count; i++) { write_le_u32(q, new_section_offsets[i]); q += 4; }
        write_le_u32(q, new_start_offset); q += 4;
        write_le_u32(q, new_end_offset); q += 4;
        memcpy(q, buf + real_trailer_pos, 4);
    }
    memcpy(out_buf + new_section_offsets[0], buf + section_offsets[0], section_offsets[3] - section_offsets[0]);
    memcpy(out_buf + new_section_offsets[3], wrapper_comp, wrapper_comp_len);
    memcpy(out_buf + new_section_offsets[4], buf + section_offsets[4], end_offset - section_offsets[4]);

    out_fid = fopen(argv[2], "wb");
    if (out_fid == NULL) { printf("failed to open output %s\n", argv[2]); return 1; }
    fwrite(out_buf, 1, new_total_size, out_fid);
    fclose(out_fid);

    printf("Wrote %s (%zu bytes)\n", argv[2], new_total_size);

    free(triangle_face_array_signed);
    prc_free(ctx, wrapper_comp);
    prc_bitwrite_release(ctx, &wrapper_s);
    prc_api_release_context(ctx);
    free(section_offsets);
    free(new_section_offsets);
    free(out_buf);
    free(buf);
    free(tess_inflated);
    return 0;
}
