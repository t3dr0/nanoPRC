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

/**
 * @file json_writer.c
 * @brief Implementation of the buffered streaming JSON writer.
 */

#include "json_writer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define JSON_WRITER_DEFAULT_BUFFER (1u << 20) /* 1 MiB */

/* ----------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------- */

/* Append raw bytes to the staging buffer, flushing to the stream as needed.
   This is the single chokepoint through which all output passes, so it is
   also where the sticky error flag is set on write failure. */
static void json_writer_append(json_writer *w, const char *data, size_t len)
{
    if (w->error)
    {
        return;
    }

    while (len > 0)
    {
        size_t space = w->buffer_size - w->buffer_used;
        size_t chunk = (len < space) ? len : space;

        if (chunk > 0)
        {
            memcpy(w->buffer + w->buffer_used, data, chunk);
            w->buffer_used += chunk;
            data += chunk;
            len -= chunk;
        }

        if (w->buffer_used == w->buffer_size)
        {
            size_t written = fwrite(w->buffer, 1, w->buffer_used, w->stream);
            w->bytes_written += written;
            if (written != w->buffer_used)
            {
                w->error = 1;
                return;
            }
            w->buffer_used = 0;
        }
        else
        {
            /* Buffer absorbed everything; nothing left to do. */
            break;
        }
    }
}

static void json_writer_append_str(json_writer *w, const char *s)
{
    json_writer_append(w, s, strlen(s));
}

/* Emit the comma/structural separator required before the next element or
   member is written, based on the state of the current nesting level. Must
   be called before writing any value or key. */
static void json_writer_pre_value(json_writer *w)
{
    if (w->depth == 0)
    {
        return; /* Single root value; nothing to separate. */
    }

    json_writer_level *lvl = &w->levels[w->depth - 1];
    if (lvl->has_emitted)
    {
        json_writer_append(w, ",", 1);
    }
    lvl->has_emitted = 1;
}

/* RFC 8259 string escaping. Operates on a bounded local buffer flushed in
   chunks so arbitrarily long strings can be escaped without an auxiliary
   heap allocation. */
static void json_writer_emit_escaped(json_writer *w, const char *value)
{
    static const char hex_digits[] = "0123456789abcdef";
    char chunk[256];
    size_t chunk_len = 0;
    const unsigned char *p;

    json_writer_append(w, "\"", 1);

    for (p = (const unsigned char *)value; *p; p++)
    {
        unsigned char c = *p;

        if (chunk_len > sizeof(chunk) - 8)
        {
            json_writer_append(w, chunk, chunk_len);
            chunk_len = 0;
        }

        switch (c)
        {
        case '"':  chunk[chunk_len++] = '\\'; chunk[chunk_len++] = '"';  break;
        case '\\': chunk[chunk_len++] = '\\'; chunk[chunk_len++] = '\\'; break;
        case '\n': chunk[chunk_len++] = '\\'; chunk[chunk_len++] = 'n';  break;
        case '\r': chunk[chunk_len++] = '\\'; chunk[chunk_len++] = 'r';  break;
        case '\t': chunk[chunk_len++] = '\\'; chunk[chunk_len++] = 't';  break;
        case '\b': chunk[chunk_len++] = '\\'; chunk[chunk_len++] = 'b';  break;
        case '\f': chunk[chunk_len++] = '\\'; chunk[chunk_len++] = 'f';  break;
        default:
            if (c < 0x20)
            {
                chunk[chunk_len++] = '\\';
                chunk[chunk_len++] = 'u';
                chunk[chunk_len++] = '0';
                chunk[chunk_len++] = '0';
                chunk[chunk_len++] = hex_digits[(c >> 4) & 0xF];
                chunk[chunk_len++] = hex_digits[c & 0xF];
            }
            else
            {
                /* Bytes >= 0x20, including UTF-8 continuation bytes, pass
                   through untouched: input is assumed to already be valid
                   UTF-8, which is also valid inside a JSON string. */
                chunk[chunk_len++] = (char)c;
            }
            break;
        }
    }

    if (chunk_len > 0)
    {
        json_writer_append(w, chunk, chunk_len);
    }

    json_writer_append(w, "\"", 1);
}

/* ----------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------- */

int json_writer_init(json_writer *w, FILE *stream, size_t buffer_size)
{
    memset(w, 0, sizeof(*w));

    if (buffer_size == 0)
    {
        buffer_size = JSON_WRITER_DEFAULT_BUFFER;
    }

    w->buffer = (char *)malloc(buffer_size);
    if (w->buffer == NULL)
    {
        return -1;
    }

    w->levels = (json_writer_level *)malloc(JSON_WRITER_INITIAL_DEPTH * sizeof(json_writer_level));
    if (w->levels == NULL)
    {
        free(w->buffer);
        w->buffer = NULL;
        return -1;
    }
    w->level_capacity = JSON_WRITER_INITIAL_DEPTH;

    w->stream = stream;
    w->buffer_size = buffer_size;
    w->buffer_used = 0;
    w->depth = 0;
    w->error = 0;
    w->bytes_written = 0;

    return 0;
}

int json_writer_flush(json_writer *w)
{
    if (w->buffer_used > 0)
    {
        size_t written = fwrite(w->buffer, 1, w->buffer_used, w->stream);
        w->bytes_written += written;
        if (written != w->buffer_used)
        {
            w->error = 1;
        }
        w->buffer_used = 0;
    }
    fflush(w->stream);
    return w->error ? -1 : 0;
}

int json_writer_destroy(json_writer *w)
{
    int result = json_writer_flush(w);
    free(w->buffer);
    w->buffer = NULL;
    w->buffer_size = 0;
    free(w->levels);
    w->levels = NULL;
    w->level_capacity = 0;
    return result;
}

int json_writer_has_error(const json_writer *w)
{
    return w->error;
}

/* ----------------------------------------------------------------------
 * Structural emitters
 * ------------------------------------------------------------------- */

static void json_writer_push_level(json_writer *w, uint8_t is_array)
{
    if (w->depth >= w->level_capacity)
    {
        int new_capacity = w->level_capacity * 2;
        json_writer_level *new_levels = (json_writer_level *)realloc(
            w->levels, (size_t)new_capacity * sizeof(json_writer_level));
        if (new_levels == NULL)
        {
            /* Out of memory growing the nesting stack; treat as a fatal,
               sticky writer error rather than corrupting output silently
               or overflowing the existing allocation. */
            w->error = 1;
            return;
        }
        w->levels = new_levels;
        w->level_capacity = new_capacity;
    }
    w->levels[w->depth].is_array = is_array;
    w->levels[w->depth].has_emitted = 0;
    w->depth++;
}

static void json_writer_pop_level(json_writer *w)
{
    if (w->depth > 0)
    {
        w->depth--;
        /* The container we just closed was itself a value within its
           parent level (or the pending value of a key); mark the parent
           level as having emitted, so a following sibling gets a comma. */
        if (w->depth > 0)
        {
            w->levels[w->depth - 1].has_emitted = 1;
        }

        /* If the container being closed was empty, pending_value may still
           be set from the begin_object_key()/begin_array_key() call that
           opened it (no element/member inside ever consumed it). That flag
           refers to a value slot that has now been fully written (the
           empty container itself), so it must not leak forward and cause
           the *next* value anywhere in the document to wrongly skip its
           separating comma. */
        w->pending_value = 0;
    }
}

/* Forward declaration: defined below alongside the key/value emitters, but
   needed here since begin_object()/begin_array() must also respect
   pending_value (an object or array can itself be the value of a key). */
static void json_writer_pre_emit_value(json_writer *w);

void json_writer_begin_object(json_writer *w)
{
    json_writer_pre_emit_value(w);
    json_writer_append(w, "{", 1);
    json_writer_push_level(w, 0);
}

void json_writer_end_object(json_writer *w)
{
    json_writer_pop_level(w);
    json_writer_append(w, "}", 1);
}

void json_writer_begin_array(json_writer *w)
{
    json_writer_pre_emit_value(w);
    json_writer_append(w, "[", 1);
    json_writer_push_level(w, 1);
}

void json_writer_end_array(json_writer *w)
{
    json_writer_pop_level(w);
    json_writer_append(w, "]", 1);
}

void json_writer_begin_object_key(json_writer *w, const char *key)
{
    json_writer_key(w, key);
    json_writer_append(w, "{", 1);
    json_writer_push_level(w, 0);
}

void json_writer_begin_array_key(json_writer *w, const char *key)
{
    json_writer_key(w, key);
    json_writer_append(w, "[", 1);
    json_writer_push_level(w, 1);
}

/* ----------------------------------------------------------------------
 * Key / value emitters
 * ------------------------------------------------------------------- */

void json_writer_key(json_writer *w, const char *key)
{
    /* A key always needs the normal comma-before-member handling (it is
       the start of a new object member). Once the key and its colon are
       written, the single value that follows must NOT be preceded by
       another comma -- pending_value records that for the next value
       emitter call. */
    json_writer_pre_value(w);
    json_writer_emit_escaped(w, key);
    json_writer_append(w, ":", 1);
    w->pending_value = 1;
}

/* Centralized helper: every value-emitting function funnels through this
   to decide whether a leading comma is required. A value is either:
   (a) immediately following a key (w->pending_value == 1) -> no comma, the
       key already accounted for the separator; or
   (b) a bare array element / root value -> normal pre_value() comma logic,
       which also marks this level as having emitted something. */
static void json_writer_pre_emit_value(json_writer *w)
{
    if (w->pending_value)
    {
        w->pending_value = 0;
        /* No comma needed (this value is the one immediately following a
           key), but the *current* level (which may be this very array, if
           begin_array_key() just pushed it) must still be marked as having
           emitted a value, so that a second sibling element/member in that
           same level correctly gets a comma. */
        if (w->depth > 0)
        {
            w->levels[w->depth - 1].has_emitted = 1;
        }
    }
    else
    {
        json_writer_pre_value(w);
    }
}

void json_writer_string(json_writer *w, const char *value)
{
    json_writer_pre_emit_value(w);
    if (value == NULL)
    {
        json_writer_append_str(w, "null");
        return;
    }
    json_writer_emit_escaped(w, value);
}

void json_writer_raw(json_writer *w, const char *raw_json_fragment)
{
    json_writer_pre_emit_value(w);
    json_writer_append_str(w, raw_json_fragment);
}

void json_writer_int32(json_writer *w, int32_t value)
{
    char buf[16];
    int n;
    json_writer_pre_emit_value(w);
    n = snprintf(buf, sizeof(buf), "%d", value);
    json_writer_append(w, buf, (size_t)n);
}

void json_writer_uint32(json_writer *w, uint32_t value)
{
    char buf[16];
    int n;
    json_writer_pre_emit_value(w);
    n = snprintf(buf, sizeof(buf), "%u", value);
    json_writer_append(w, buf, (size_t)n);
}

void json_writer_int64(json_writer *w, int64_t value)
{
    char buf[24];
    int n;
    json_writer_pre_emit_value(w);
    n = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    json_writer_append(w, buf, (size_t)n);
}

void json_writer_uint64(json_writer *w, uint64_t value)
{
    char buf[24];
    int n;
    json_writer_pre_emit_value(w);
    n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    json_writer_append(w, buf, (size_t)n);
}

void json_writer_size(json_writer *w, size_t value)
{
    char buf[24];
    int n;
    json_writer_pre_emit_value(w);
    n = snprintf(buf, sizeof(buf), "%zu", value);
    json_writer_append(w, buf, (size_t)n);
}

void json_writer_double(json_writer *w, double value)
{
    char buf[40];
    int n;

    json_writer_pre_emit_value(w);

    if (isnan(value) || isinf(value))
    {
        /* JSON has no representation for NaN/Infinity; clamp to 0 so the
           document always remains strictly well-formed. */
        value = 0.0;
    }

    n = snprintf(buf, sizeof(buf), "%.9g", value);
    json_writer_append(w, buf, (size_t)n);
}

void json_writer_float(json_writer *w, float value)
{
    /* Single precision needs at most ~9 significant decimal digits to
       round-trip exactly; %.9g on the promoted double achieves that while
       keeping output compact. Comma handling is delegated to the double
       path, so no separate pre_emit_value() call is needed here. */
    json_writer_double(w, (double)value);
}

void json_writer_bool(json_writer *w, int value)
{
    json_writer_pre_emit_value(w);
    json_writer_append_str(w, value ? "true" : "false");
}

void json_writer_null(json_writer *w)
{
    json_writer_pre_emit_value(w);
    json_writer_append_str(w, "null");
}

/* ----------------------------------------------------------------------
 * Convenience key+value emitters
 * ------------------------------------------------------------------- */

void json_writer_kv_string(json_writer *w, const char *key, const char *value)
{
    json_writer_key(w, key);
    json_writer_string(w, value);
}

void json_writer_kv_int32(json_writer *w, const char *key, int32_t value)
{
    json_writer_key(w, key);
    json_writer_int32(w, value);
}

void json_writer_kv_uint32(json_writer *w, const char *key, uint32_t value)
{
    json_writer_key(w, key);
    json_writer_uint32(w, value);
}

void json_writer_kv_uint64(json_writer *w, const char *key, uint64_t value)
{
    json_writer_key(w, key);
    json_writer_uint64(w, value);
}

void json_writer_kv_size(json_writer *w, const char *key, size_t value)
{
    json_writer_key(w, key);
    json_writer_size(w, value);
}

void json_writer_kv_double(json_writer *w, const char *key, double value)
{
    json_writer_key(w, key);
    json_writer_double(w, value);
}

void json_writer_kv_float(json_writer *w, const char *key, float value)
{
    json_writer_key(w, key);
    json_writer_float(w, value);
}

void json_writer_kv_bool(json_writer *w, const char *key, int value)
{
    json_writer_key(w, key);
    json_writer_bool(w, value);
}
