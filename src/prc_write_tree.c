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
#include "prc_write_tree.h"
#include "prc_data.h"

/* ---- small shared field-group writers, matching prc_parse_common.c ---- */

static int
prc_write_content_prc_base_empty(prc_context *ctx, prc_bit_write_state *s)
{
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* attribute_count */
    if (prc_bitwrite_bit(ctx, s, 1) != 0) return -1;     /* name.same */
    return 0;
}

/* ContentPRCRefBase + same_graphics=0 + GraphicsContent, all left at
   inert defaults except unique_id -- the one field prc_write_tree assigns
   meaningfully (sequential product unique_id, per session spec). */
static int
prc_write_base_with_graphics_default(prc_context *ctx, prc_bit_write_state *s, uint32_t unique_id)
{
    if (prc_write_content_prc_base_empty(ctx, s) != 0) return -1;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;         /* nonpersistent_id_cad */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;         /* unique_id_cad */
    if (prc_bitwrite_uint32(ctx, s, unique_id) != 0) return -1; /* unique_id */
    if (prc_bitwrite_bit(ctx, s, 0) != 0) return -1;            /* same_graphics */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;         /* biased_layer_index */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;         /* biased_index_of_line_style */
    if (prc_bitwrite_uint8(ctx, s, 0) != 0) return -1;          /* behavior_bit_field1 */
    if (prc_bitwrite_uint8(ctx, s, 0) != 0) return -1;          /* behavior_bit_field2 */
    return 0;
}

static int
prc_write_user_data_empty(prc_context *ctx, prc_bit_write_state *s)
{
    return prc_bitwrite_uint32(ctx, s, 0) == 0 ? 0 : -1; /* stream_size = 0 */
}

static int
prc_write_markup_data_empty(prc_context *ctx, prc_bit_write_state *s)
{
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_linked_items */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_leaders */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_markups */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_annotation_entities */
    return 0;
}

/* ---- iterative (explicit-stack) post-order flatten ---- */

typedef struct prc_write_tree_flat_s
{
    const prc_write_tree_node **order; /* post-order: children before parents, root last */
    uint32_t count;
    uint32_t cap;
} prc_write_tree_flat;

static int
prc_write_tree_flat_push(prc_context *ctx, prc_write_tree_flat *flat, const prc_write_tree_node *node)
{
    if (flat->count == flat->cap)
    {
        uint32_t new_cap = flat->cap == 0 ? 8u : flat->cap * 2u;
        const prc_write_tree_node **new_arr = (const prc_write_tree_node **)prc_realloc(ctx,
            (void *)flat->order, sizeof(*flat->order) * new_cap);
        if (new_arr == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_write_tree_to_stream\n");
            return -1;
        }
        flat->order = new_arr;
        flat->cap = new_cap;
    }
    flat->order[flat->count++] = node;
    return 0;
}

/* Classic two-stack iterative post-order: `scratch` accumulates a valid
   reverse-post-order (every node pushed before its children are), which is
   then reversed into `flat` to get true post-order (children before their
   parent, root last). Both stacks are explicit and heap-allocated -- no
   native recursion on caller-controlled tree depth. */
static int
prc_write_tree_flatten(prc_context *ctx, const prc_write_tree_node *root, prc_write_tree_flat *flat)
{
    prc_write_tree_flat scratch;
    uint32_t i;

    memset(&scratch, 0, sizeof(scratch));
    memset(flat, 0, sizeof(*flat));

    if (prc_write_tree_flat_push(ctx, &scratch, root) != 0)
        goto fail;

    {
        uint32_t stack_top = 1; /* scratch.order[0..stack_top) is the live stack */
        while (stack_top > 0)
        {
            const prc_write_tree_node *node;
            uint32_t k;

            stack_top--;
            node = scratch.order[stack_top];
            scratch.count = stack_top;

            if (prc_write_tree_flat_push(ctx, flat, node) != 0)
                goto fail;

            for (k = 0; k < node->num_children; k++)
            {
                if (prc_write_tree_flat_push(ctx, &scratch, node->children[k]) != 0)
                    goto fail;
            }
            stack_top = scratch.count;
        }
    }

    /* `flat` currently holds reverse-post-order; reverse in place. */
    for (i = 0; i < flat->count / 2; i++)
    {
        const prc_write_tree_node *tmp = flat->order[i];
        flat->order[i] = flat->order[flat->count - 1 - i];
        flat->order[flat->count - 1 - i] = tmp;
    }

    prc_free(ctx, (void *)scratch.order);
    return 0;

fail:
    if (scratch.order != NULL) prc_free(ctx, (void *)scratch.order);
    if (flat->order != NULL) prc_free(ctx, (void *)flat->order);
    memset(flat, 0, sizeof(*flat));
    return -1;
}

static uint32_t
prc_write_tree_find_index(const prc_write_tree_flat *flat, const prc_write_tree_node *node)
{
    uint32_t i;
    for (i = 0; i < flat->count; i++)
        if (flat->order[i] == node)
            return i;
    return 0; /* unreachable if `node` really is one of this tree's children */
}

/* ---- per-entity writers ---- */

static int
prc_write_rep_item_entry(prc_context *ctx, prc_bit_write_state *s, const prc_write_rep_item *ri)
{
    uint32_t tag = (ri->kind == PRC_WRITE_RI_WIRE) ? PRC_TYPE_RI_PolyWire : PRC_TYPE_RI_PolyBrepModel;

    if (prc_bitwrite_uint32(ctx, s, tag) != 0) return -1;
    if (prc_write_base_with_graphics_default(ctx, s, 0) != 0) return -1;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;                          /* biased_index_local_coordinate_system */
    if (prc_bitwrite_uint32(ctx, s, ri->biased_tessellation_index) != 0) return -1; /* biased_index_tessellation */

    if (ri->kind == PRC_WRITE_RI_SURFACE)
    {
        if (prc_bitwrite_bit(ctx, s, ri->is_closed) != 0) return -1;
    }
    if (prc_write_user_data_empty(ctx, s) != 0) return -1;
    return 0;
}

static int
prc_write_part(prc_context *ctx, prc_bit_write_state *s, const prc_write_tree_node *node)
{
    uint32_t k;

    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_PartDefinition) != 0) return -1;
    if (prc_write_base_with_graphics_default(ctx, s, 0) != 0) return -1;

    if (prc_bitwrite_double(ctx, s, node->bbox_min[0]) != 0) return -1;
    if (prc_bitwrite_double(ctx, s, node->bbox_min[1]) != 0) return -1;
    if (prc_bitwrite_double(ctx, s, node->bbox_min[2]) != 0) return -1;
    if (prc_bitwrite_double(ctx, s, node->bbox_max[0]) != 0) return -1;
    if (prc_bitwrite_double(ctx, s, node->bbox_max[1]) != 0) return -1;
    if (prc_bitwrite_double(ctx, s, node->bbox_max[2]) != 0) return -1;

    if (prc_bitwrite_uint32(ctx, s, node->num_rep_items) != 0) return -1;
    for (k = 0; k < node->num_rep_items; k++)
        if (prc_write_rep_item_entry(ctx, s, &node->rep_items[k]) != 0) return -1;

    if (prc_write_markup_data_empty(ctx, s) != 0) return -1;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_views */
    if (prc_write_user_data_empty(ctx, s) != 0) return -1;
    return 0;
}

static int
prc_write_product(prc_context *ctx, prc_bit_write_state *s, const prc_write_tree_node *node,
    uint32_t unique_id, uint32_t biased_part_index, const prc_write_tree_flat *flat)
{
    uint32_t k;
    uint8_t write_transform = (uint8_t)(node->has_transform && !node->is_identity);

    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_ProductOccurrence) != 0) return -1;
    if (prc_write_base_with_graphics_default(ctx, s, unique_id) != 0) return -1;

    /* ReferencesOfProductOccurrence */
    if (prc_bitwrite_uint32(ctx, s, biased_part_index) != 0) return -1; /* biased_index_part */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;                 /* biased_index_prototype */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1;                 /* biased_index_external_data */
    if (prc_bitwrite_uint32(ctx, s, node->num_children) != 0) return -1;
    for (k = 0; k < node->num_children; k++)
    {
        uint32_t child_index = prc_write_tree_find_index(flat, node->children[k]);
        if (prc_bitwrite_uint32(ctx, s, child_index) != 0) return -1;
    }

    if (prc_bitwrite_uint8(ctx, s, 0) != 0) return -1; /* product_behavior */

    /* ProductInformation: mirrors the parser's own field order exactly,
       including its apparent bit-then-overwriting-uint8 quirk on
       product_information_flags. */
    if (prc_bitwrite_bit(ctx, s, 0) != 0) return -1;
    if (prc_bitwrite_double(ctx, s, 1.0) != 0) return -1; /* unit */
    if (prc_bitwrite_uint8(ctx, s, 0) != 0) return -1;    /* product_information_flags */
    if (prc_bitwrite_int32(ctx, s, 0) != 0) return -1;    /* product_load_status */

    if (prc_bitwrite_bit(ctx, s, write_transform) != 0) return -1;
    if (write_transform)
    {
        int k4;
        if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_MISC_GeneralTransformation) != 0) return -1;
        for (k4 = 0; k4 < 16; k4++)
            if (prc_bitwrite_double(ctx, s, node->transform[k4]) != 0) return -1;
    }

    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* entity_ref_count */
    if (prc_write_markup_data_empty(ctx, s) != 0) return -1;
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_views */
    if (prc_bitwrite_bit(ctx, s, 0) != 0) return -1;    /* has_filter */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_display_filters */
    if (prc_bitwrite_uint32(ctx, s, 0) != 0) return -1; /* number_of_scene_parameters */
    if (prc_write_user_data_empty(ctx, s) != 0) return -1;
    return 0;
}

int
prc_write_tree_to_stream(prc_context *ctx, prc_bit_write_state *s,
    const prc_write_tree_node *root, uint32_t *root_unique_id_out)
{
    prc_write_tree_flat flat;
    uint32_t i;
    uint32_t parts_count = 0;
    int ret = PRC_ERROR_INTERNAL;

    if (ctx == NULL || s == NULL || root == NULL)
    {
        prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tree_to_stream: invalid arguments\n");
        return PRC_ERROR_INTERNAL;
    }

    if (prc_write_tree_flatten(ctx, root, &flat) != 0)
        return PRC_ERROR_MEMORY;

    for (i = 0; i < flat.count; i++)
        if (flat.order[i]->num_rep_items > 0)
            parts_count++;

    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_FileStructureTree) != 0) goto fail;
    if (prc_write_content_prc_base_empty(ctx, s) != 0) goto fail;

    if (prc_bitwrite_uint32(ctx, s, parts_count) != 0) goto fail;
    for (i = 0; i < flat.count; i++)
        if (flat.order[i]->num_rep_items > 0)
            if (prc_write_part(ctx, s, flat.order[i]) != 0) goto fail;

    if (prc_bitwrite_uint32(ctx, s, flat.count) != 0) goto fail;
    {
        uint32_t part_cursor = 0;
        for (i = 0; i < flat.count; i++)
        {
            uint32_t biased_part_index = 0;
            if (flat.order[i]->num_rep_items > 0)
            {
                part_cursor++;
                biased_part_index = part_cursor;
            }
            if (prc_write_product(ctx, s, flat.order[i], i + 1, biased_part_index, &flat) != 0)
                goto fail;
        }
    }

    /* Table 38 PRC_TYPE_ASM_FileStructure internal data */
    if (prc_bitwrite_uint32(ctx, s, PRC_TYPE_ASM_FileStructure) != 0) goto fail;
    if (prc_write_content_prc_base_empty(ctx, s) != 0) goto fail;
    if (prc_bitwrite_uint32(ctx, s, flat.count + 1) != 0) goto fail; /* next_available_index */
    if (prc_bitwrite_uint32(ctx, s, flat.count - 1) != 0) goto fail; /* index_product_occurrence: the root */

    if (prc_write_user_data_empty(ctx, s) != 0) goto fail;

    if (root_unique_id_out != NULL)
        *root_unique_id_out = flat.count; /* root is last: unique_id == count */

    ret = 0;
    goto cleanup;

fail:
    ret = s->error ? PRC_ERROR_MEMORY : PRC_ERROR_INTERNAL;

cleanup:
    if (flat.order != NULL) prc_free(ctx, (void *)flat.order);
    return ret;
}
