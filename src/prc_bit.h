/* Copyright (C) 2023-2026 CascadiaVoxel LLC

	nano_prc is free software: you can redistribute it and/or modify it under
	the terms of the GNU Affero General Public License as published by the
	Free Software Foundation, either version 3 of the License, or (at your
	option) any later version.

	nano_prc is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
	License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with nano_prc. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef PRC_BIT_H
#define PRC_BIT_H

typedef struct prc_bit_state_s prc_bit_state;
struct prc_bit_state_s
{
	uint8_t *ptr;
	uint8_t bitmask;
	int64_t bit_count;
    int64_t bit_position;
	prc_name *curr_name;
	prc_graphics_content graphics_content;
};

typedef struct prc_huff_node_s prc_huff_node;
struct prc_huff_node_s
{
	prc_huff_node *left;
	prc_huff_node *right;
	uint32_t value;
	uint32_t code;
	uint32_t code_length;
	uint8_t is_leaf;
	prc_huff_node* next;
	prc_huff_node *prev;
	prc_huff_node *parent;
	int freq;
};

union float_uint
{
	float float_val;
	unsigned int uint_val;
};

void prc_debug_view_stream_bounding_box_search(prc_context *ctx, prc_bit_state *state);
void prc_debug_view_stream_on_byte_boundary(prc_context *ctx, prc_bit_state *state);
void prc_debug_stream(prc_context *ctx, prc_bit_state *state);
void prc_init_bit_state(prc_context* ctx, prc_bit_state* state, uint8_t* ptr, size_t byte_count);
uint8_t prc_bitread_bit(prc_context* ctx, prc_bit_state* state);
uint8_t prc_bitread_uint8(prc_context* ctx, prc_bit_state* state);
uint32_t prc_bitread_uint32(prc_context* ctx, prc_bit_state* state);
int32_t prc_bitread_int32(prc_context* ctx, prc_bit_state* state);
double prc_bitread_double(prc_context* ctx, prc_bit_state* state);
int prc_bitread_string(prc_context* ctx, prc_bit_state* state, prc_string *data);
float prc_bitread_float(prc_context* ctx, prc_bit_state* state);
uint8_t* prc_bitread_character_array(prc_context* ctx, prc_bit_state* state, uint32_t* data_size, uint8_t bit_number, uint8_t has_comp_bit, uint32_t known_count);
uint32_t prc_bitread_uint_variable_bit(prc_context* ctx, prc_bit_state* state, uint32_t bit_length);
int32_t prc_bitread_int_variable_bit(prc_context* ctx, prc_bit_state* state, uint32_t bit_length);
uint32_t* prc_bitread_compressed_integer_array(prc_context* ctx, prc_bit_state* state, uint32_t* data_size);
int32_t* prc_bitread_compressed_indice_array(prc_context* ctx, prc_bit_state* state, uint32_t* data_size, uint8_t has_comp_bit, uint32_t known_count);
int32_t prc_bitread_uncompressed_uint32(prc_context* ctx, prc_bit_state* state);
uint32_t prc_bitread_huff_data(prc_context* ctx, prc_bit_state* state, uint32_t num_bits);
void prc_init_huff_bit_state(prc_context* ctx, prc_bit_state* state, uint8_t* ptr);
void prc_bitread_rewind(prc_context *ctx, prc_bit_state *state, uint8_t backup_bits);
int32_t *prc_bitread_short_array(prc_context *ctx, prc_bit_state *state, uint32_t *data_size, uint8_t has_comp_bit, uint8_t number_of_bits);
uint32_t prc_bitread_number_of_bits_then_unsigned_int(prc_context* ctx, prc_bit_state* state);
int prc_bitread_compressed_entity_type(prc_context *ctx, prc_bit_state *state, uint8_t *is_curve, uint32_t *entity_type);
double prc_bitread_double_with_variable_bit_number(prc_context *ctx, prc_bit_state *state, unsigned int number_of_bits, double tolerance);

#endif
