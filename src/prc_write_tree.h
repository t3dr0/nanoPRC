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

#ifndef PRC_WRITE_TREE_H
#define PRC_WRITE_TREE_H

#include "prc_write_common.h"
#include "prc_data.h"
#include "prc_bit.h"

/* Encoder for PRC_TYPE_ASM_FileStructureTree (Table 47): the product/part
   assembly tree, the exact inverse of prc_parse_file_tree (which dispatches
   to prc_parse_parts / prc_parse_product_occurrence in prc_parse_tree.c).

   A representation item wraps one tessellation-section entry: PRC_WRITE_RI_
   SURFACE writes PRC_TYPE_RI_PolyBrepModel (used for tessellated surface
   geometry -- NOT exact B-Rep, which is out of scope), PRC_WRITE_RI_WIRE
   writes PRC_TYPE_RI_PolyWire (line/polyline geometry). */
typedef enum prc_write_ri_kind_e
{
    PRC_WRITE_RI_SURFACE = 0,
    PRC_WRITE_RI_WIRE = 1
} prc_write_ri_kind;

typedef struct prc_write_rep_item_s
{
    prc_write_ri_kind kind;
    uint32_t biased_tessellation_index; /* 1-based index into the file's
                                            tessellation-section array */
    uint8_t  is_closed;                 /* PRC_WRITE_RI_SURFACE only */
} prc_write_rep_item;

/* One node of the caller-supplied product/part tree. A node with
   num_rep_items > 0 owns a part definition (with its own bounding box);
   every node -- with or without a part -- becomes one product occurrence.
   The tree is walked iteratively (explicit heap work stack): depth is
   caller-controlled input, so no native recursion is used. */
typedef struct prc_write_tree_node_s
{
    const prc_write_rep_item *rep_items;
    uint32_t num_rep_items;
    double bbox_min[3];
    double bbox_max[3];

    uint8_t has_transform;
    uint8_t is_identity;   /* ignored if has_transform == 0; when 1, no
                              transform is written at all (has_transform is
                              forced to 0 on the wire -- identity needs none) */
    double  transform[16]; /* column-major 4x4 (PRC_TYPE_MISC_GeneralTransformation),
                               used only if has_transform && !is_identity */

    struct prc_write_tree_node_s * const *children;
    uint32_t num_children;
} prc_write_tree_node;

/* Encodes the whole tree rooted at `root` into the current file structure's
   Table 47 section content (tag + parts[] + products[] + the trailing
   PRC_TYPE_ASM_FileStructure internal-data record). Nodes are flattened in
   post-order (children before parents) since PRC_TYPE_ASM_ProductOccurrence
   child references are plain 0-based indices into the shared products[]
   array and the root -- consumed by prc_api_prep_model_tree as simply the
   LAST entry of that array -- must therefore be written last. Product
   unique_id values (ContentPRCRefBase.unique_id) are assigned sequentially
   in that same post-order, starting at 1.

   *root_unique_id_out (if non-NULL) receives the root's assigned unique_id,
   which prc_write_model.c's ASM_ModelFile root reference needs. */
int prc_write_tree_to_stream(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_tree_node *root, uint32_t *root_unique_id_out);

#endif
