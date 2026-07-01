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

/* Emit a newline followed by (depth) tab characters for indentation.
   Only called when pretty-printing is enabled and we are not inside a
   compact array level.  We emit tabs directly from a small static string
   in chunks to avoid per-character append() overhead on deeply nested
   documents. */
static void json_writer_indent(json_writer *w, int depth)
{
    static const char tabs[] =
        "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"  /* 16 */
        "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"  /* 32 */
        "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"  /* 48 */
        "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"; /* 64 */
    int remaining = depth;

    json_writer_append(w, "\n", 1);
    while (remaining > 0)
    {
        int chunk = remaining < 64 ? remaining : 64;
        json_writer_append(w, tabs, (size_t)chunk);
        remaining -= chunk;
    }
}

/* Return 1 if the current (innermost) nesting level is a compact array,
   or if any enclosing level is compact and has not yet been closed.
   In practice, compact mode is not nested (json_export only opens compact
   arrays for flat scalar lists), but we check all open levels for safety
   so that a compact array nested inside another compact array also
   suppresses pretty-printing. */
static int json_writer_in_compact(const json_writer *w)
{
    int i;
    for (i = w->depth - 1; i >= 0; i--)
    {
        if (w->levels[i].is_compact)
        {
            return 1;
        }
    }
    return 0;
}

/* Emit the comma/structural separator required before the next element or
   member is written, based on the state of the current nesting level. Must
   be called before writing any value or key.  In pretty-print mode this
   also inserts a newline+indent after the comma (or, for the first element,
   before it), unless the current container is a compact array. */
static void json_writer_pre_value(json_writer *w)
{
    if (w->depth == 0)
    {
        return; /* Single root value; nothing to separate. */
    }

    {
        json_writer_level *lvl = &w->levels[w->depth - 1];
        int pretty_here = w->pretty && !json_writer_in_compact(w);

        if (lvl->has_emitted)
        {
            json_writer_append(w, ",", 1);
            if (pretty_here)
            {
                json_writer_indent(w, w->depth);
            }
            else if (w->pretty && lvl->is_compact)
            {
                /* Inside a compact array: separate scalars with a single
                   space for readability without per-element line breaks. */
                json_writer_append(w, " ", 1);
            }
        }
        else if (pretty_here)
        {
            /* First element of a non-compact container: newline+indent
               to place it on its own line inside the opening bracket. */
            json_writer_indent(w, w->depth);
        }

        lvl->has_emitted = 1;
    }
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

int json_writer_init(json_writer *w, FILE *stream, size_t buffer_size, int pretty)
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
    w->pretty = pretty ? 1 : 0;

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
    /* Zero the whole level entry so no field (is_compact, has_emitted, ...)
       carries over from a previous container that occupied this depth slot. */
    memset(&w->levels[w->depth], 0, sizeof(w->levels[w->depth]));
    w->levels[w->depth].is_array = is_array;
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
    /* Before closing, emit newline+indent at the parent depth so the
       closing brace lands on its own line, aligned with the opening one.
       Only when pretty-printing and the object had at least one member
       (empty objects stay on one line: {}). */
    if (w->pretty && w->depth > 0 && w->levels[w->depth - 1].has_emitted
        && !json_writer_in_compact(w))
    {
        json_writer_indent(w, w->depth - 1);
    }
    json_writer_pop_level(w);
    json_writer_append(w, "}", 1);
}

void json_writer_begin_array(json_writer *w)
{
    json_writer_pre_emit_value(w);
    json_writer_append(w, "[", 1);
    json_writer_push_level(w, 1);
    /* is_compact defaults to 0 (normal array). */
}

void json_writer_end_array(json_writer *w)
{
    /* Emit closing newline+indent for non-compact arrays that had content.
       Compact arrays close with ] immediately after the last element. */
    if (w->pretty && w->depth > 0 && w->levels[w->depth - 1].has_emitted
        && !w->levels[w->depth - 1].is_compact
        && !json_writer_in_compact(w))
    {
        json_writer_indent(w, w->depth - 1);
    }
    json_writer_pop_level(w);
    json_writer_append(w, "]", 1);
}

/* Consume the pending-value flag set by json_writer_key() and ensure the
   parent level (at w->depth - 1, *before* push_level has been called) is
   marked as having emitted a value, so that the *next* sibling member in
   the parent object will correctly receive a comma. This must be called
   for every begin_{object,array}_key() variant, before push_level(). */
static void json_writer_consume_pending(json_writer *w)
{
    w->pending_value = 0;
    /* Mark the parent level as having emitted a member.  The index is
       w->depth - 1 because push_level() has not run yet. */
    if (w->depth > 0)
    {
        w->levels[w->depth - 1].has_emitted = 1;
    }
}

void json_writer_begin_object_key(json_writer *w, const char *key)
{
    json_writer_key(w, key);
    /* Consume the pending-value slot: the opening '{' satisfies it.
       We do this before push_level so the *new* level starts fresh
       (has_emitted = 0, pending_value = 0), giving the first member of
       the new object its own newline+indent line in pretty-print mode. */
    json_writer_consume_pending(w);
    json_writer_append(w, "{", 1);
    json_writer_push_level(w, 0);
}

void json_writer_begin_array_key(json_writer *w, const char *key)
{
    json_writer_key(w, key);
    /* Same rationale: the '[' satisfies the key's pending-value slot,
       and the new level starts clean so its first element receives the
       newline+indent it deserves in pretty-print mode. */
    json_writer_consume_pending(w);
    json_writer_append(w, "[", 1);
    json_writer_push_level(w, 1);
    /* is_compact defaults to 0 (normal, pretty-printed array). */
}

void json_writer_begin_array_compact(json_writer *w)
{
    json_writer_pre_emit_value(w);
    json_writer_append(w, "[", 1);
    json_writer_push_level(w, 1);
    if (w->depth > 0)
    {
        /* Mark this level as compact: its elements are written space-
           separated on a single line, not one per line. */
        w->levels[w->depth - 1].is_compact = 1;
    }
}

void json_writer_begin_array_compact_key(json_writer *w, const char *key)
{
    json_writer_key(w, key);
    /* Consume the pending-value slot. For compact arrays the new level
       is immediately flagged is_compact, and pre_value() will see that
       flag and suppress per-element newlines without needing
       pending_value to intervene. */
    json_writer_consume_pending(w);
    json_writer_append(w, "[", 1);
    json_writer_push_level(w, 1);
    if (w->depth > 0)
    {
        w->levels[w->depth - 1].is_compact = 1;
    }
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
    /* In pretty mode, a single space after the colon mirrors the common
       convention used by most JSON pretty-printers (e.g. Python's json
       module with indent=, jq). */
    if (w->pretty)
    {
        json_writer_append(w, ": ", 2);
    }
    else
    {
        json_writer_append(w, ":", 1);
    }
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
