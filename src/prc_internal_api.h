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

#ifndef PRC_API_INTERNAL_H
#define PRC_API_INTERNAL_H

#include <stdint.h>
#include "prc_api.h"

/* Forward declarations to break circular include with prc_data.h.
   prc_internal_api only needs pointers to these types here. */
typedef struct prc_base_with_graphics_s prc_base_with_graphics;
typedef struct prc_misc_entity_reference_s prc_misc_entity_reference;

typedef struct prc_internal_api_vertex_s
{
    float position[3];
} prc_internal_api_vertex;

typedef struct prc_internal_api_normal_s
{
    float normal[3];
} prc_internal_api_normal;

/* Need to see if the number of texture coords is ever greater than 1. Right now we
   will throw and error if it is */
#define MAX_NUM_TEXTURE_COORDS 1
typedef struct prc_internal_api_texture_indices_s
{
    float texture[MAX_NUM_TEXTURE_COORDS];
} prc_internal_api_texture_indices;

/* This is to allow for a faster look-up of the api vertex based
   upon the index into the normal and position of the prc data.
   Essentially an array of the same size as the prc positions
   vertices but at each position we have a linked list that
   we can go through to check for a normal vector and get the
   index into the api vertex data that has that position and normal vector AND 
   texture vertices if theu are being used */
typedef struct prc_internal_api_position_normal_pair_s prc_internal_api_position_normal_pair;

/* Define a state for the normal vector. We have three possiblities. Either it
   is already set, it is not set but we CAN set it, or it is not set and must be
   calculated. Also keep a state for the texture, color, and style of the
   vertex. */
typedef enum {
    PRC_INTERNAL_API_NORM_MUST_BE_COMPUTED = -1,
    PRC_INTERNAL_API_NORM_NOT_SET,
    PRC_INTERNAL_API_NORM_SET
} prc_internal_api_normal_state_t;

typedef enum {
    PRC_INTERNAL_API_NO_TEXTURE = -1,
    PRC_INTERNAL_API_TEXTURE_NOT_SET,
    PRC_INTERNAL_API_TEXTURE_SET
} prc_internal_api_texture_state_t;

/* Note that the prc_position_index is also used to index */
typedef enum {
    PRC_INTERNAL_API_NO_COLOR = -1,
    PRC_INTERNAL_API_COLOR_NOT_SET,
    PRC_INTERNAL_API_COLOR_SET
} prc_internal_api_color_state_t;

typedef enum {
    PRC_INTERNAL_API_NO_STYLE = -1,
    PRC_INTERNAL_API_STYLE_NOT_SET_FROM_TESS,
    PRC_INTERNAL_API_STYLE_SET_FROM_TESS,
    PRC_INTERNAL_API_STYLE_NOT_SET_FROM_REF_DATA,
    PRC_INTERNAL_API_STYLE_SET_FROM_REF_DATA
} prc_internal_api_style_state_t;

typedef enum {
    PRC_INTERNAL_API_OBJECT_TYPE_PRODUCT,
    PRC_INTERNAL_API_OBJECT_TYPE_PART,
    PRC_INTERNAL_API_OBJECT_TYPE_RI
} prc_internal_api_object_type;

/* Special note on prc_vertex_color_index which is a member variable below. So if we
   are dealing with vertex colors then every vertex has a color. We of course have
   a list of indices into the vertices. The count into the indices array is the count
   that is used to get the color in the decode_array (which is where we store the 
   vertex colors).  In searching for matching vertices we check for a match of the color
   not the indice though. */
struct prc_internal_api_position_normal_pair_s
{
    uint32_t prc_position_index;
    int32_t prc_normal_index;
    uint32_t prc_vertex_color_index; /* This is the count into the indices array */
    float vertex_color[4]; /* This is related to prc_vertex_color_index */
    uint32_t prc_texture_index[2];
    uint32_t prc_num_texture_indices; /* Usually 1. Would like to see something other than that */
    size_t api_vertex_index;
    int32_t style_index;
    int32_t face_style_file_index;
    prc_internal_api_normal_state_t normal_set;
    prc_internal_api_texture_state_t texture_set;
    prc_internal_api_color_state_t color_set;
    prc_internal_api_style_state_t style_set;
    prc_internal_api_position_normal_pair *next;
};

typedef struct prc_internal_api_position_normal_lookup_s
{
    size_t number_values;
    prc_internal_api_position_normal_pair *position_normal_pair;
} prc_internal_api_position_normal_lookup;

/* This structure is used to store the tessellation data for a given face.
   It is used to store the number of triangles, fans, and strips that are
   used to represent the face. It also stores the offsets into the vertex
   indices for the fans and strips. This is for a specific type. For example
   those entities that have a single norm, or a texture, or a multiple norm */
typedef struct prc_internal_api_tess_entities_s
{
    size_t num_triangles;
    size_t num_fans;
    size_t num_strips;
    size_t *fan_offsets;
    size_t *strip_offsets;
} prc_internal_api_tess_entities;

typedef struct prc_internal_texture_s prc_internal_texture;
struct prc_internal_texture_s
{
    /* PRC_TYPE_GRAPH_TextureApplication */
    int32_t material_generic_index;
    int32_t texture_definition_index;
    int32_t next_texture_index;
    int32_t uv_coordinates_index;
    int32_t picture_index;
    int32_t wrapping_u; /* Repeat in u */
    int32_t wrapping_v; /* Repeat in v */
    uint32_t picture_width;
    uint32_t picture_height;
    uint32_t num_elements_per_pixel;
    unsigned char *picture_data;
    uint8_t has_texture_transform;
    double texture_transform[9]; /* A 3x3 */
    prc_internal_texture *next_texture; /* We can have a pipeline of textures */
};

typedef struct prc_internal_graph_style_s prc_internal_graph_style;
struct prc_internal_graph_style_s
{
    double line_width;
    uint8_t is_vpicture;
    uint32_t biased_pattern_index;
    uint8_t is_material;
    uint32_t biased_color_index;
    uint8_t is_transparency;
    uint8_t transparency;
    uint32_t face_style_index;
    uint32_t face_style_file_index;
    float tint[4];

    prc_internal_texture texture;

    /* PRC_TYPE_GRAPH_Material */
    int32_t ambient_index;
    int32_t diffuse_index;
    int32_t emissive_index;
    int32_t specular_index;
    double shininess;
    double ambient_alpha;
    double diffuse_alpha;
    double emissive_alpha;
    double specular_alpha;
    uint32_t material_type;
    float ambient_color[3];
    float diffuse_color[3];
    float emissive_color[3];
    float specular_color[3];
};

/* In PRC a face can contain n triangles, m fans and p strips
   This structure gives a list of vertex indices for the num_triangles
   which are the first entries num_triangles*3 in the vertex_indice_list.
   If there are fans, num_fans > 0 and pairs of values in fan_offsets will
   give the offset into vertex_indices and how many points are used in vertex_indices.
   Same logic is used for strips.  Also note that while each face has one style,
   the compressed tessellation is forced by the API to have a single face but
   in reality can have multiple faces. Those are packed into the the
   number_of_styles, and *style information and used during the creation of the
   triangle vertices that we hand back */
typedef struct prc_internal_api_face_s
{
    size_t capacity;
    size_t num_indices;
    size_t num_texture_coords;
    uint32_t *vertex_indices;
    prc_internal_api_tess_entities single_norm;
    prc_internal_api_tess_entities multi_norm;
    prc_internal_api_tess_entities texture_single_norm;
    prc_internal_api_tess_entities texture_multi_norm;
    size_t number_of_styles;
    prc_internal_graph_style *style;
} prc_internal_api_face;

/* We have a similar set up for the 3D wire */
typedef struct prc_internal_api_wire_s
{
    size_t capacity;
    size_t num_indices;
    uint32_t *vertex_indices;
    prc_internal_graph_style style;
    prc_api_material material;
    prc_api_graphic_type_t type;
} prc_internal_api_wire;

typedef enum {
    PRC_INTERNAL_API_MULTINORM = 0,
    PRC_INTERNAL_API_SINGLENORM,
    PRC_INTERNAL_API_SINGLENORM_TEXTURE,
    PRC_INTERNAL_API_MULTINORM_TEXTURE,
    PRC_INTERNAL_API_MAX
} prc_internal_api_entity_t;

typedef struct prc_api_object_style_s prc_api_object_style;

typedef struct prc_api_child_reserve_s
{
    uint32_t num_products;
    uint32_t num_parts;
    uint32_t num_markups;
    prc_api_product *products;
    prc_api_part *parts;
    prc_api_markup *markups;
    uint32_t markup_index;
    uint32_t product_index;
    uint32_t part_index;
    prc_api_object_style *style_tree_root;

    /* Preallocated pool of prc_api_object_style nodes.
       Caller should initialize the pool after computing the totals
       (num_products + num_parts + num_markups) by calling
       prc_internal_api_style_pool_init(...). */
    prc_api_object_style *style_pool;
    uint32_t style_pool_capacity; /* total entries allocated in style_pool */
    uint32_t style_pool_index;    /* next free slot index (0 .. style_pool_capacity-1) */
} prc_api_child_reserve;

/* This is a rework of the part style. We will capture all the product
   entity reference information here which will have information about
   the styles for each of the faces potentially. We will capture a tree
   of styles starting from the base product going through the product prototypes
   all the way to the part and the representation item (RI). RI have
   tessellation data associated with them. When we process the RI, we
   will want to be able to traverse back up the tree to get the style
   information for the face that we are processing. Products can have multiple
   entity references which specify styles for different RIs. They can
   also specify styles for specific faces of those RIs. We want to build 
   a tree of object styles that we can traverse forward AND backward. We will
   build it in the forward direction as we go from the base product through
   the product prototypes to a part and finally a RI (which is actually a part).*/

/* Define the structure so we can self reference in the tree forward and backwards
   We only store the style index and the file index and the array of prc_misc_entity_reference
   items that may have been defined for that product. */

/* This will be much cleaner and enable us to decode things later when we
   do the drawing of the tessellation */

struct prc_api_object_style_s
{
    prc_internal_api_object_type object_type;
    uint32_t brep_type;
    uint32_t index_of_topological_index;  /* Used only with Brep RI */
    uint32_t index_of_body;               /* Used only with Brep RI */
    uint32_t file_index;  /* File index of product/part/RI */
    prc_base_with_graphics *base_with_graphics;
    uint32_t num_entity_references;
    prc_misc_entity_reference *entity_references;
    prc_api_object_style *parent;
    prc_api_object_style **children;
    uint32_t num_children;
};

#endif
