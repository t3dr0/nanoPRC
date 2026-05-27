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

#ifndef PRC_PARSE_FILE_STRUCTURE_H
#define PRC_PARSE_FILE_STRUCTURE_H

#include "prc_data.h"

int prc_parse_file_extra_geometry(prc_context *ctx, prc_filestructure *file_struct);
int prc_parse_file_tessellation(prc_context *ctx, prc_filestructure *file_struct, uint8_t debug_tess);
int prc_parse_file_geometry(prc_context *ctx, prc_filestructure *file_struct);
int prc_parse_file_schema_and_global(prc_context *ctx, prc_filestructure *file_struct);
int prc_parse_file_tree(prc_context *ctx, prc_filestructure *file_struct);
int prc_parse_model_file(prc_context *ctx, prc_filestructure *file_struct, uint32_t file_struct_count, uint32_t index);
#endif