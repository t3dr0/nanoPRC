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

#ifndef PRC_WRITE_FILE_STRUCTURE_H
#define PRC_WRITE_FILE_STRUCTURE_H

#include <stddef.h>
#include "prc_write_common.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_write_global.h"
#include "prc_write_wire_tess.h"

typedef enum prc_write_tess_kind_e
{
    PRC_WRITE_TESS_KIND_3D = 0,
    PRC_WRITE_TESS_KIND_WIRE = 1
} prc_write_tess_kind;

/* One entry of the file structure's tessellation-section array (Table 48,
   PRC_TYPE_ASM_FileStructureTessellation). Bridges to the existing
   per-format encoders (prc_write_tess_3d.c / prc_write_wire_tess.c) --
   exactly one of the two field groups below is read, selected by `kind`. */
typedef struct prc_write_tess_entry_s
{
    prc_write_tess_kind kind;

    /* PRC_WRITE_TESS_KIND_3D -- forwarded to prc_write_tess_3d */
    const double *positions; uint32_t num_positions;
    const double *normals; uint32_t num_normals;
    const uint32_t *tri_indices;
    const uint32_t *norm_indices;
    uint32_t num_triangles;
    const uint32_t *face_tri_counts;
    uint32_t num_faces;

    /* PRC_WRITE_TESS_KIND_WIRE -- forwarded to prc_write_wire_tess */
    const prc_write_wire_element *wire_elements;
    uint32_t num_wire_elements;
} prc_write_tess_entry;

/* Schema (Table 8, always empty -- no custom entity schema in this write
   facility) + FileStructureGlobals (Table 39) combined section content.
   prc_parse_file_schema_and_global treats these as one deflated section
   with no leading type tag on the schema half ("the schema is the first
   one and does NOT have a type"). */
int prc_write_schema_and_globals_to_stream(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_global_tables *tables);

/* PRC_TYPE_ASM_FileStructureTessellation (Table 48) section content. */
int prc_write_tessellation_section_to_stream(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_tess_entry *entries, uint32_t num_entries);

/* Deflate-compresses `src` (zlib, default level, single deflateInit/deflate
   .../Z_FINISH/deflateEnd call sized via deflateBound) into a caller-owned
   (prc_free) buffer. Returns 0 on success. */
int prc_write_deflate(prc_context *ctx, const uint8_t *src, size_t src_len,
    uint8_t **out, size_t *out_len);

/* Fixed-size uncompressed prc_file_structure_header (Table 37, the content
   at a file structure's section_offset[0]): "PRC" + min_vers_for_read +
   auth_vers + 2 unique ids (16 bytes each) + file_count. file_count (the
   embedded-uncompressed-file / raster-image table) is always written 0 --
   no embedded images in this session's scope. Writes exactly
   PRC_WRITE_FILE_STRUCT_HEADER_SIZE raw (non-bit-packed, little-endian)
   bytes to `out`. */
#define PRC_WRITE_FILE_STRUCT_HEADER_SIZE 47u
void prc_write_file_struct_header_bytes(uint8_t *out, uint32_t min_vers_for_read, uint32_t auth_vers);

#endif
