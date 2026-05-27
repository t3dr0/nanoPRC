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

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include "prc_parse_file_structure.h"
#include "prc_parse_common.h"
#include "prc_parse_extra_geometry.h"
#include "prc_parse_tess.h"

#include "prc_bit.h"
#include "zlib.h"
#include "debug.h"
#include "prc_vector_util.h"
#include "prc_huff.h"

static void
debug_prc_character_bitstream(prc_context *ctx, prc_bit_state *bit_state, uint8_t find, size_t byte_length)
{
    prc_bit_state temp;
    prc_bit_state restore;
    uint32_t val_read;
    size_t start_pos = (size_t) bit_state->ptr;
    size_t size = 0;
    size_t bits_read = 0;
    uint8_t bit;

    temp.ptr = bit_state->ptr;
    temp.bitmask = bit_state->bitmask;
    restore = temp;

    while (size < byte_length)
    {
        val_read = prc_bitread_uint8(ctx, bit_state);
        size = (size_t) (bit_state->ptr) - start_pos;
        if (val_read == find)
        {
            printf("Found key\n");
            printf("bytes read = %zd\n", size);
            printf("bits read = %zd\n", bits_read);
            break;
        }

        bit = prc_bitread_bit(ctx, &temp);
        bit_state->bitmask = temp.bitmask;
        bit_state->ptr = temp.ptr;
        bits_read++;
    }

    // bit_state->bitmask = restore.bitmask;
    // bit_state->ptr = restore.ptr;
}

static prc_header*
prc_new_header(prc_context *ctx)
{
    return prc_calloc(ctx, 1, sizeof(prc_header));
}

static prc_file_structure_header*
prc_new_file_struct_header(prc_context *ctx)
{
    return prc_calloc(ctx, 1, sizeof(prc_file_structure_header));
}

static prc_file_struct_desc* prc_create_file_struct_array(prc_context *ctx, size_t num)
{
    return (prc_file_struct_desc*)prc_calloc(ctx, num, sizeof(prc_file_struct_desc));
}

static uint8_t*
prc_read_32bits_unsigned(prc_context *ctx, uint8_t* data, prc_unsigned_int *value)
{
    *value = data[0] + (((uint32_t) data[1]) << 8) +
                     (((uint32_t) data[2]) << 16) +
                     (((uint32_t) data[3]) << 24);
    return data + 4;
}

static uint8_t*
prc_read_uniqueid(prc_context *ctx, uint8_t* data, prc_unique_id* value)
{
    uint8_t* ptr = data;

    ptr = prc_read_32bits_unsigned(ctx, ptr, &value->unique_id0);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &value->unique_id1);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &value->unique_id2);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &value->unique_id3);
    return ptr;
}

int
prc_uncompress(prc_context *ctx, const unsigned char *src, size_t src_len,
               unsigned char **dst)
{
    z_stream zInfo = { 0 };
    size_t des_len = 2 * src_len;
    unsigned char *dest;

    dest = (unsigned char*)prc_malloc(ctx, des_len);
    *dst = dest;
    if (dest == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Allocation error in prc_uncompress\n");
        return PRC_ERROR_MEMORY;
    }

    zInfo.total_in = (uLong) src_len;
    zInfo.avail_in = (uInt) src_len;
    zInfo.total_out = (uLong) des_len;
    zInfo.avail_out = (uInt) des_len;
    zInfo.next_out = (Bytef*) dest;
    zInfo.next_in = (Bytef*) src;
    int err, ret = -1;
    err = inflateInit(&zInfo);
    if (err == Z_OK)
    {
        do
        {
            err = inflate(&zInfo, Z_SYNC_FLUSH);
            if (err == Z_OK) {
                /* I need a second set of eyes to check this */
                des_len *= 2;
                dest = prc_realloc(ctx, dest, des_len);
                *dst = dest;
                zInfo.next_out = dest + zInfo.total_out;
                zInfo.avail_out = des_len - zInfo.total_out;
            }
            else if (err < 0)
            {
                inflateEnd(&zInfo);
                prc_free(ctx, dest);
                dest = NULL;
                *dst = dest;
                return err;
            }
        } while (err != Z_STREAM_END);
    }
    ret = inflateEnd(&zInfo);   // zlib function
    if (ret == Z_OK)
        return zInfo.total_out;
    return ret; // -1 or len of output
}

char desc[3];
prc_unsigned_int min_vers_for_read;
prc_unsigned_int auth_vers;
prc_unique_id unique_id_file;
prc_unique_id unique_id_application;
prc_unsigned_int file_count;
prc_uncomp_file* files;

static uint8_t*
prc_parse_uncomp_file(prc_context *ctx, uint8_t *ptr, prc_uncomp_block *file)
{
    uint32_t k;
    int code;

    ptr = prc_read_32bits_unsigned(ctx, ptr, &file->block_size);

    if (file->block_size > 0)
    {
        file->block = prc_calloc(ctx, file->block_size, sizeof(uint8_t));
        if (file->block == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate file->block\n");
            return NULL;
        }
        memcpy(file->block, ptr, file->block_size);
        ptr += file->block_size;
        /* We don't know the data type until later when we read the PRC_TYPE_GRAPH_Picture data */
    }
    return ptr;
}

prc_file_structure_header*
prc_parse_filestruct_header(prc_context *ctx, uint8_t* buff)
{
    prc_file_structure_header* header;
    uint8_t* ptr;
    uint32_t k;
    int code;

    if (buff[0] != 'P' && buff[1] != 'R' && buff[2] != 'C')
    {
        return NULL;
    }

    header = prc_new_file_struct_header(ctx);
    if (header == NULL)
    {
        return NULL;
    }

    memcpy(&header->desc, buff, 3);
    ptr = buff + 3;
    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->min_vers_for_read);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->auth_vers);
    ptr = prc_read_uniqueid(ctx, ptr, &header->unique_id_file);
    ptr = prc_read_uniqueid(ctx, ptr, &header->unique_id_application);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->file_count);

    /* This is where the texture images are stored! Spec is wrong on the type of these */
    if (header->file_count > 0)
    {
        header->files = prc_calloc(ctx, header->file_count, sizeof(prc_uncomp_block));
        if (header->files == NULL)
        {
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate header->files\n");
            return NULL;
        }
        for (k = 0; k < header->file_count; k++)
        {
            ptr = prc_parse_uncomp_file(ctx, ptr, &header->files[k]);
            if (ptr == NULL)
            {
                prc_error(ctx, PRC_ERROR_MEMORY, "Failed in prc_parse_uncomp_file\n");
                return NULL;
            }
        }
    }
    return header;
}

prc_header*
prc_parse_main_header(prc_context *ctx, uint8_t *buff)
{
    prc_header* header;
    size_t k, j;
    uint8_t* ptr;

    if (buff[0] != 'P' && buff[1] != 'R' && buff[2] != 'C')
    {
        return NULL;
    }

    header = prc_new_header(ctx);
    if (header == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate main header\n");
        return NULL;
    }

    memcpy(&header->desc, buff, 3);
    ptr = buff + 3;
    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->min_vers_for_read);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->auth_vers);
    ptr = prc_read_uniqueid(ctx, ptr, &header->unique_id_file);
    ptr = prc_read_uniqueid(ctx, ptr, &header->unique_id_application);
    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->filestructure_count);
    
    if (header->filestructure_count > 0)
    {
        header->file_info = prc_create_file_struct_array(ctx, header->filestructure_count);
        if (header->file_info == NULL)
        {
            prc_free(ctx, header);
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate header->file_info\n");
            return NULL;
        }

        for (k = 0; k < header->filestructure_count; k++)
        {
            ptr = prc_read_uniqueid(ctx, ptr, &header->file_info[k].unique_id);
            ptr += 4;
            ptr = prc_read_32bits_unsigned(ctx, ptr, &header->file_info[k].section_count);

            if (header->file_info[k].section_count > 0)
            {
                header->file_info[k].section_offset =
                    (prc_unsigned_int*)prc_calloc(ctx, header->file_info[k].section_count,
                    sizeof(prc_unsigned_int));
                if (header->file_info[k].section_offset == NULL)
                {
                    prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate section_offset\n");
                    prc_free(ctx, header->file_info);
                    prc_free(ctx, header);
                    return NULL;
                }
                for (j = 0; j < header->file_info[k].section_count; j++)
                {
                    ptr = prc_read_32bits_unsigned(ctx, ptr, &header->file_info[k].section_offset[j]);
                }
            }
        }
        ptr = prc_read_32bits_unsigned(ctx, ptr, &header->start_offset);
        ptr = prc_read_32bits_unsigned(ctx, ptr, &header->end_offset);
        ptr = prc_read_32bits_unsigned(ctx, ptr, &header->file_count);
        if (header->file_count > 0)
        {
            /* We are not going to worry about the multiple file case just yet */
            prc_free(ctx, header->file_info);
            prc_free(ctx, header);
            prc_error(ctx, PRC_ERROR_PARSE, "Multi-file case not supported\n");
            return NULL;
        }
    }
    return header;
}

static void
prc_dump_file(const char* filename, uint8_t* buff, size_t size)
{
    FILE* fid = fopen(filename, "wb");
    if (fid == NULL)
        return;
    fwrite(buff, 1, size, fid);
    fclose(fid);
}

prc_data*
prc_open_contents(prc_context *ctx, const char* infile)
{
    FILE* fid;
    uint8_t* buff;
    uint8_t *buff2;
    size_t size;
    size_t size_read;
    prc_header* header;
    uint32_t k, j;
    uint8_t* ptr;
    prc_filestructure* file_struct = NULL;
    int code;
    uint8_t *ptr_raw, *ptr_raw2;
    prc_bit_state bit_state;
    uint32_t type;
    prc_data *output;
    uint32_t prc_size;
    prc_pdf_view_array *views = NULL;
    uint32_t number_views = 0;

    output = (prc_data *)prc_calloc(ctx, 1, sizeof(prc_data));
    if (output == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate prc_data\n");
        return NULL;
    }

    /* Not ideal but just read in all the data for now */
    fid = fopen(infile, "rb");
    if (fid == NULL)
    {
        prc_error(ctx, PRC_ERROR_IO, "Failed to open file\n");
        return NULL;
    }

    fseek(fid, 0L, SEEK_END);
    size = ftell(fid);
    fseek(fid, 0L, SEEK_SET);

    buff = prc_malloc(ctx, size);
    if (buff == NULL)
    {
        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate main buffer\n");
        return NULL;
    }

    size_read = fread(buff, 1, size, fid);
    fclose(fid);

    /* Determine if this is a PDF or a PRC file */ 
    if (buff[0] != 'P' && buff[1] != 'R' && buff[2] != 'C')
    {
        /* Not a PRC file. Check if it is a PDF file */
        if (buff[0] != 0x25 && buff[1] != 0x50 && buff[2] != 0x44 && buff[3] != 0x46)
        {
            prc_free(ctx, buff);
            prc_error(ctx, PRC_ERROR_PARSE, "Not a PRC or PDF file\n");
            return NULL;
        }

        /* OK we need to extract the PRC stream from the PDF file */
        code = pdf_extract_prc(ctx, buff, size, &buff2, &prc_size, &views,
                               &number_views);
        if (code < 0)
        {
            prc_free(ctx, buff);
            prc_error(ctx, code, "Failed in prc_extract_prc\n");
            return NULL;
        }
        prc_free(ctx, buff);
        buff = buff2;
        size = prc_size;

        if (views != NULL)
        {
            output->views = views;
            output->view_count = number_views;
        }
    }

    /* For debug */
    // prc_dump_file("debug.prc", buff, size);

    /* The header */
    header = prc_parse_main_header(ctx, buff);
    if (header == NULL)
    {
        prc_free(ctx, buff);
        prc_error(ctx, PRC_ERROR_PARSE, "Failed in prc_parse_main_header\n");
        return NULL;
    }

    ctx->source_file_version = header->auth_vers;

    /* Table 6 Filestructure */
    if (header->filestructure_count > 0)
    {
        file_struct = (prc_filestructure*)prc_calloc(ctx, header->filestructure_count,
                                                     sizeof(prc_filestructure));
        if (file_struct == NULL)
        {
            prc_free(ctx, buff);
            prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate prc_filestructure\n");
            return NULL;
        }

        /* Parse the file structure details */
        for (k = 0; k < header->filestructure_count; k++)
        {
            ctx->current_file_index = k;
            if (header->file_info[k].section_count > 0)
            {
                /* First the header which is not deflated (required) */
                ptr = &buff[header->file_info[k].section_offset[0]];
                file_struct[k].header = prc_parse_filestruct_header(ctx, ptr);

                if (file_struct[k].header->min_vers_for_read > ctx->internal.reader_version)
                {
                    prc_error(ctx, PRC_FILE_VERS, "File version not supported\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }

                /* The model file is defined special */
                ptr = &buff[header->start_offset];
                code = prc_uncompress(ctx, ptr, header->end_offset - header->start_offset, &ptr_raw);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed in prc_uncompress\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }
                ptr_raw2 = (uint8_t*)prc_malloc(ctx, code);
                if (ptr_raw2 == NULL)
                {
                    prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate ptr_raw2\n");
                    prc_free(ctx, ptr_raw);
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }

                memcpy(ptr_raw2, ptr_raw, code);
                prc_free(ctx, ptr_raw);
                file_struct[k].model_unzipped = ptr_raw2;
                file_struct[k].model_size = code;

                code = prc_parse_model_file(ctx, &file_struct[k], header->filestructure_count, k);
                if (code < 0)
                {
                    prc_error(ctx, code, "Failed in prc_parse_model_file\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }

                /* All other sections are deflated but can be independently read from the offset */
                for (j = 1; j < header->file_info[k].section_count; j++)
                {
                    ptr = &buff[header->file_info[k].section_offset[j]];
                    code = prc_uncompress(ctx, ptr, size - header->file_info[k].section_offset[j], &ptr_raw);
                    if (code < 0)
                    {
                        prc_error(ctx, code, "Failed in prc_uncompress\n");
                        prc_free(ctx, file_struct);
                        prc_free(ctx, buff);
                        prc_release_header(ctx, header);
                        return NULL;
                    }
                    ptr_raw2 = (uint8_t*)prc_malloc(ctx, code);
                    if (ptr_raw2 == NULL)
                    {
                        prc_error(ctx, PRC_ERROR_MEMORY, "Failed to allocate ptr_raw2\n");
                        prc_free(ctx, ptr_raw);
                        prc_free(ctx, file_struct);
                        prc_free(ctx, buff);
                        prc_release_header(ctx, header);
                        return NULL;
                    }

                    memcpy(ptr_raw2, ptr_raw, code);
                    prc_free(ctx, ptr_raw);

                    /* Section has been inflated. Now initialize the bit state */
                    prc_init_bit_state(ctx, &bit_state, ptr_raw2, code);

                    /* Note that schema is the first one and does NOT have a type.
                       This I feel is just one of many issues in this spec */
                    if (j == 1)
                    {
                        /* Schema case (required) */
                        file_struct[k].schema_globals_unzipped = ptr_raw2;
                        file_struct[k].schema_globals_size = code;
                    }
                    else
                    {
                        /* The items are supposed to be in the order that they 
                           are given in Table 6. I have a table that has a tag
                           type encoded as 30651 Very odd
                           as 306 is PRC_TYPE_ASM_FileStructureGeometry and that
                           is the position that this section is.  Does one 
                           throw an error or move on. */
                        type = prc_bitread_uint32(ctx, &bit_state);

                        switch (type)
                        {
                        case PRC_TYPE_ASM:
                        case PRC_TYPE_ASM_ProductOccurrence:
                        case PRC_TYPE_ASM_PartDefinition:
                        case PRC_TYPE_ASM_Filter:
                        case PRC_TYPE_ASM_ModelFile:
                        case PRC_TYPE_ASM_FileStructure:
                            fprintf(stderr, "Unimplemented assembly type\n");
                            break;

                        case PRC_TYPE_ASM_FileStructureTree:
                            file_struct[k].tree_unzipped = ptr_raw2;
                            file_struct[k].tree_size = code;
                            break;
                        case PRC_TYPE_ASM_FileStructureTessellation:
                            file_struct[k].tessellation_unzipped = ptr_raw2;
                            file_struct[k].tessellation_size = code;
                            break;
                        case PRC_TYPE_ASM_FileStructureGeometry:
                            file_struct[k].geometry_unzipped = ptr_raw2;
                            file_struct[k].geometry_size = code;
                            break;
                        case PRC_TYPE_ASM_FileStructureExtraGeometry:
                            file_struct[k].extra_geometry_unzipped = ptr_raw2;
                            file_struct[k].extra_geometry_size = code;
                            break;
                        case PRC_TYPE_ROOTBaseNoReference:
                            fprintf(stderr, "PRC_TYPE_ROOT_PRCBaseNoReference type\n");
                            break;
                        default:
                            fprintf(stderr, "Unknown assembly type\n");
                            break;
                        }
                    }
                }
            }

            /* Now parse the unzipped sections */
            if (file_struct[k].schema_globals_unzipped != NULL)
            {
                code = prc_parse_file_schema_and_global(ctx, &file_struct[k]);
                if (code < 0)
                {
                    prc_error(ctx, code, "Error in prc_parse_file_schema_and_global\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }
            }
            if (file_struct[k].tessellation_unzipped != NULL)
            {
                uint8_t debug_tess = 0;
                //if (k == 17)
                //{
                //    debug_tess = 1;
                //}
                code = prc_parse_file_tessellation(ctx, &file_struct[k], debug_tess);
                if (code < 0)
                {
                    prc_error(ctx, code, "Error in prc_parse_file_tessellation\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }
            }
            if (file_struct[k].tree_unzipped != NULL)
            {
                code = prc_parse_file_tree(ctx, &file_struct[k]);
                if (code < 0)
                {
                    prc_error(ctx, code, "Error in prc_parse_file_tree\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }
            }
            if (file_struct[k].geometry_unzipped != NULL)
            {
                /* This is the exact geometry data */
                code = prc_parse_file_geometry(ctx, &file_struct[k]);
                if (code < 0)
                {
                    prc_error(ctx, code, "Error in prc_parse_file_geometry\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }
            }
            if (file_struct[k].extra_geometry_unzipped != NULL)
            {
                code = prc_parse_file_extra_geometry(ctx, &file_struct[k]);
                if (code < 0)
                {
                    prc_error(ctx, code, "Error in prc_parse_file_extra_geometry\n");
                    prc_free(ctx, file_struct);
                    prc_free(ctx, buff);
                    prc_release_header(ctx, header);
                    return NULL;
                }
            }
        } /* Done with all the files */
    }

    prc_free(ctx, buff);
    output->header = header;
    output->file_structure_count = header->filestructure_count;
    output->file_struct = file_struct;

    return output;
}