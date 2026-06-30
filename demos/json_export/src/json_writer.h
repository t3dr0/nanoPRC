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

#ifndef JSON_WRITER_H
#define JSON_WRITER_H

/**
 * @file json_writer.h
 * @brief Minimal, single-pass, buffered streaming JSON writer.
 *
 * This writer is purpose-built for emitting very large, well-formed JSON
 * documents (potentially many gigabytes) directly to a FILE stream without
 * ever materializing the document in memory. It maintains a small internal
 * container-state stack (object/array, "first element" tracking) so that
 * commas and structural punctuation are emitted correctly, and it owns a
 * fixed-size output buffer that is flushed to the underlying stream once it
 * fills, amortizing the cost of write(2)/fwrite(3) calls.
 *
 * The writer intentionally favors very low, constant memory overhead: the
 * nesting-state stack is a fixed-capacity array of bytes (one per nesting
 * level), not a dynamic structure, because the JSON shapes produced by the
 * json_export tool have a known, bounded maximum nesting depth.
 *
 * All string output is escaped per RFC 8259. Floating point numbers are
 * written with a caller-controlled precision sufficient for round-tripping
 * single-precision tessellation data without bloating file size.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

/** Initial capacity of the dynamically-grown nesting level stack. Growing
 *  is geometric (doubling), so this only affects the number of early
 *  reallocations for typical documents; it is not a hard limit. */
#define JSON_WRITER_INITIAL_DEPTH 64

/** Internal per-level container state. */
typedef struct json_writer_level_s
{
    uint8_t is_array;     /* 1 if this level is a JSON array, 0 if object   */
    uint8_t has_emitted;  /* 1 once at least one element/member was written */
} json_writer_level;

/** Streaming JSON writer instance. Treat as opaque; all access is through
 *  the json_writer_* functions below. */
typedef struct json_writer_s
{
    FILE *stream;                 /* Destination stream (already open)      */
    char *buffer;                 /* Output staging buffer                  */
    size_t buffer_size;           /* Allocated capacity of buffer           */
    size_t buffer_used;           /* Bytes currently staged in buffer       */
    json_writer_level *levels;    /* Heap-allocated nesting-state stack,    */
                                   /* grown geometrically as depth requires  */
    int depth;                    /* Current nesting depth (0 = root)       */
    int level_capacity;           /* Allocated capacity of levels[]         */
    int error;                    /* Sticky error flag; 0 == ok             */
    uint64_t bytes_written;       /* Total bytes flushed to stream so far   */
    int pending_value;            /* 1 immediately after json_writer_key()  */
} json_writer;

/**
 * @brief Initialize a JSON writer over an already-open output stream.
 *
 * @param w Writer instance to initialize.
 * @param stream Destination stream; caller retains ownership (must close it).
 * @param buffer_size Size in bytes of the internal staging buffer. A value
 *        of 0 selects a sensible default (1 MiB).
 * @return 0 on success, -1 on allocation failure.
 */
int json_writer_init(json_writer *w, FILE *stream, size_t buffer_size);

/**
 * @brief Flush any staged bytes to the underlying stream and release the
 *        internal buffer. Safe to call multiple times.
 *
 * @param w Writer instance.
 * @return 0 on success, -1 if a write error occurred.
 */
int json_writer_destroy(json_writer *w);

/**
 * @brief Force a flush of staged bytes to the stream without releasing
 *        the buffer.
 */
int json_writer_flush(json_writer *w);

/**
 * @brief Check whether the writer has encountered an unrecoverable error
 *        (allocation failure or short write).
 *
 * @return Non-zero if an error has occurred.
 */
int json_writer_has_error(const json_writer *w);

/* ---- Structural emitters ---------------------------------------------- */

void json_writer_begin_object(json_writer *w);
void json_writer_end_object(json_writer *w);
void json_writer_begin_array(json_writer *w);
void json_writer_end_array(json_writer *w);

/* Begin a named object/array as the value of a key within the *current*
 * (innermost) object. Equivalent to json_writer_key() followed by
 * json_writer_begin_object()/json_writer_begin_array(), provided as a
 * convenience to keep call sites compact and readable. */
void json_writer_begin_object_key(json_writer *w, const char *key);
void json_writer_begin_array_key(json_writer *w, const char *key);

/* ---- Key / value emitters ---------------------------------------------- */

/** Emit an object member key (only valid directly inside an object). */
void json_writer_key(json_writer *w, const char *key);

/** Emit a JSON string value (escaped). May be used as an array element or,
 *  after json_writer_key(), as an object member value. NULL is emitted as
 *  JSON null. */
void json_writer_string(json_writer *w, const char *value);

/** Emit a pre-escaped raw string value, e.g. literal JSON fragments such as
 *  numbers formatted by the caller. Use sparingly and only with trusted,
 *  already-valid JSON text. */
void json_writer_raw(json_writer *w, const char *raw_json_fragment);

void json_writer_int32(json_writer *w, int32_t value);
void json_writer_uint32(json_writer *w, uint32_t value);
void json_writer_int64(json_writer *w, int64_t value);
void json_writer_uint64(json_writer *w, uint64_t value);
void json_writer_size(json_writer *w, size_t value);

/** Emit a double-precision number. NaN/Inf are emitted as 0 (JSON has no
 *  representation for them) to guarantee well-formed output. */
void json_writer_double(json_writer *w, double value);

/** Emit a single-precision float (promoted to double for formatting). */
void json_writer_float(json_writer *w, float value);

void json_writer_bool(json_writer *w, int value);
void json_writer_null(json_writer *w);

/* ---- Convenience key+value pairs (reduce call-site boilerplate) -------- */

void json_writer_kv_string(json_writer *w, const char *key, const char *value);
void json_writer_kv_int32(json_writer *w, const char *key, int32_t value);
void json_writer_kv_uint32(json_writer *w, const char *key, uint32_t value);
void json_writer_kv_uint64(json_writer *w, const char *key, uint64_t value);
void json_writer_kv_size(json_writer *w, const char *key, size_t value);
void json_writer_kv_double(json_writer *w, const char *key, double value);
void json_writer_kv_float(json_writer *w, const char *key, float value);
void json_writer_kv_bool(json_writer *w, const char *key, int value);

#endif /* JSON_WRITER_H */
