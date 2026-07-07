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

#ifndef PRC_WRITE_MODEL_H
#define PRC_WRITE_MODEL_H

#include "prc_write_common.h"
#include "prc_data.h"
#include "prc_bit.h"
#include "prc_write_global.h"
#include "prc_write_tree.h"
#include "prc_write_file_structure.h"

/* PRC_TYPE_ASM_ModelFile (Table 36) section content: the "model" section,
   addressed via the main header's start_offset/end_offset pair rather than
   the per-file-structure section_offset[] table. root_unique_id is the
   ContentPRCRefBase.unique_id assigned by prc_write_tree_to_stream to the
   tree's root product occurrence (its *root_unique_id_out); this codebase's
   own reader (prc_api_prep_model_tree) does not actually consult this
   reference -- it just takes the last product of the last file structure's
   products[] array -- but other PRC consumers (e.g. Adobe Reader) do, so it
   is written faithfully. file_struct_count is always 1 in this write
   facility (single-file-structure PRC, no cross-file references). */
int prc_write_model_file_to_stream(prc_context *ctx, prc_bit_write_state *s,
    uint32_t root_unique_id, uint32_t file_struct_count);

/* Top-level orchestration: writes a complete, single-file-structure .prc
   file to `filename`. Encodes globals/tree/tessellation independently,
   deflates each (plus the ASM_ModelFile section), then writes the file in
   one forward pass -- main header (placeholder offsets) -> file-structure
   header -> model section -> schema+globals section -> tree section ->
   tessellation section -- and finally seeks back to offset 0 to rewrite the
   main header now that every section's real offset is known. That single
   fseek+fwrite is the only backward seek in the whole write path. */
int prc_write_prc_file(prc_context *ctx, const char *filename,
    const prc_write_global_tables *tables,
    const prc_write_tree_node *root,
    const prc_write_tess_entry *tess_entries, uint32_t num_tess_entries);

#endif
