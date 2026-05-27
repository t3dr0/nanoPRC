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

#include <stdlib.h>
#include <string.h>
#include "../include/prc_context.h"
#include <stdio.h>

/* Enable this define to turn on memory guard debugging (detect buffer overruns) */
#define PRC_MEMORY_GUARDS 1

/* Track a specific pointer to detect when it gets corrupted */
static void *g_tracked_pointer = NULL;
static size_t g_tracked_size = 0;

#ifdef PRC_MEMORY_GUARDS

/* Memory guard pattern for detecting buffer overruns */
#define PRC_MEMORY_GUARD_SIZE 32  /* Increased from 16 to catch more overruns */
#define PRC_MEMORY_GUARD_PATTERN 0xDEADBEEF

typedef struct prc_guarded_block_s {
    size_t size;
    uint32_t front_guard[PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t)];
    /* User data follows */
    /* uint32_t back_guard[PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t)] follows user data */
} prc_guarded_block;

static void prc_init_guards(prc_guarded_block *block, size_t user_size)
{
    uint32_t *back_guard;
    size_t i;

    block->size = user_size;

    /* Initialize front guard */
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
        block->front_guard[i] = PRC_MEMORY_GUARD_PATTERN;

    /* Initialize back guard (after user data) */
    back_guard = (uint32_t *)((unsigned char *)(block + 1) + user_size);
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
        back_guard[i] = PRC_MEMORY_GUARD_PATTERN;
}

static int prc_check_guards(const char *location, void *user_ptr)
{
    prc_guarded_block *block;
    uint32_t *back_guard;
    size_t i;
    int corrupted = 0;

    if (user_ptr == NULL)
        return 0;

    block = ((prc_guarded_block *)user_ptr) - 1;
    back_guard = (uint32_t *)((unsigned char *)user_ptr + block->size);

    /* Check front guard */
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
    {
        if (block->front_guard[i] != PRC_MEMORY_GUARD_PATTERN)
        {
            printf("*** HEAP CORRUPTION DETECTED at %s ***\n", location);
            printf("    Front guard corrupted at offset %zu (ptr=%p, size=%zu)\n",
                i * sizeof(uint32_t), user_ptr, block->size);
            printf("    Expected: 0x%08X, Got: 0x%08X\n",
                PRC_MEMORY_GUARD_PATTERN, block->front_guard[i]);
            corrupted = 1;
        }
    }

    /* Check back guard */
    for (i = 0; i < PRC_MEMORY_GUARD_SIZE / sizeof(uint32_t); i++)
    {
        if (back_guard[i] != PRC_MEMORY_GUARD_PATTERN)
        {
            printf("*** HEAP CORRUPTION DETECTED at %s ***\n", location);
            printf("    Back guard corrupted at offset %zu (ptr=%p, size=%zu)\n",
                i * sizeof(uint32_t), user_ptr, block->size);
            printf("    Expected: 0x%08X, Got: 0x%08X\n",
                PRC_MEMORY_GUARD_PATTERN, back_guard[i]);
            printf("    This indicates a buffer OVERRUN of %zu bytes!\n",
                i * sizeof(uint32_t));

            /* Dump the corrupted data */
            printf("    Corrupted data (showing 32 bytes from corruption point):\n    ");
            unsigned char *corrupt_ptr = (unsigned char *)&back_guard[i];
            for (size_t j = 0; j < 32 && j < 64; j++)
            {
                printf("%02X ", corrupt_ptr[j]);
                if ((j + 1) % 16 == 0) printf("\n    ");
            }
            printf("\n");

            corrupted = 1;
        }
    }

    if (corrupted)
    {
        /* Trigger breakpoint to catch the issue */
#ifdef _WIN32
        __debugbreak();
#else
        __builtin_trap();
#endif
    }

    return corrupted;
}

/* Check ALL allocated blocks for corruption - expensive but thorough */
static void prc_check_all_guards(prc_context *ctx, const char *location)
{
#if PRC_DEBUG_MEMORY
    size_t i;
    printf("[Guard Check] Checking all %zu allocations at %s\n",
        ctx->current_memory_index, location);

    for (i = 0; i < ctx->current_memory_index; i++)
    {
        if (ctx->debug_memory[i].is_free == 0)
        {
            prc_check_guards(location, ctx->debug_memory[i].data);
        }
    }
#endif
}

#endif /* PRC_MEMORY_GUARDS */

void *
prc_malloc(prc_context *ctx, size_t size)
{
    void *p;

    if (size == 0)
        return NULL;

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *block;

        /* Allocate extra space for guards */
        block = (prc_guarded_block *)ctx->hooks.malloc(ctx->hooks.opaque,
            sizeof(prc_guarded_block) + size + PRC_MEMORY_GUARD_SIZE);

        if (block == NULL)
            return NULL;

        prc_init_guards(block, size);
        p = (void *)(block + 1);
    }
#else
    p = ctx->hooks.malloc(ctx->hooks.opaque, size);
#endif

#if PRC_DEBUG_MEMORY
    if (ctx->current_memory_index >= ctx->debug_memory_size)
    {
#ifdef PRC_MEMORY_GUARDS
        ctx->hooks.free(ctx->hooks.opaque, ((prc_guarded_block *)p) - 1);
#else
        prc_free(ctx, p);
#endif
        return NULL;
    }
    if (p != NULL)
    {
        ctx->debug_memory[ctx->current_memory_index].data = p;
        ctx->debug_memory[ctx->current_memory_index].index = ctx->current_memory_index;
        ctx->debug_memory[ctx->current_memory_index].is_free = 0;
        ctx->current_memory_index++;
    }
#endif
    return p;
}

void *
prc_calloc(prc_context *ctx, size_t count, size_t size)
{
    void *p;
    size_t total_size;

    if (count == 0 || size == 0)
        return NULL;

    if (count > SIZE_MAX / size)
        return NULL;

    total_size = size * count;

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *block;

        /* Allocate extra space for guards */
        block = (prc_guarded_block *)ctx->hooks.malloc(ctx->hooks.opaque,
            sizeof(prc_guarded_block) + total_size + PRC_MEMORY_GUARD_SIZE);

        if (block == NULL)
            return NULL;

        prc_init_guards(block, total_size);
        p = (void *)(block + 1);
        memset(p, 0, total_size);
    }
#else
    p = ctx->hooks.malloc(ctx->hooks.opaque, total_size);
    if (p == NULL)
        return NULL;
    memset(p, 0, total_size);
#endif

#if PRC_DEBUG_MEMORY
    if (ctx->current_memory_index >= ctx->debug_memory_size)
    {
#ifdef PRC_MEMORY_GUARDS
        ctx->hooks.free(ctx->hooks.opaque, ((prc_guarded_block *)p) - 1);
#else
        prc_free(ctx, p);
#endif
        return NULL;
    }
    ctx->debug_memory[ctx->current_memory_index].data = p;
    ctx->debug_memory[ctx->current_memory_index].index = ctx->current_memory_index;
    ctx->debug_memory[ctx->current_memory_index].is_free = 0;
    ctx->current_memory_index++;
#endif
    return p;
}

void
prc_free(prc_context *ctx, void *p)
{
    if (p == NULL)
        return;

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *block;

        /* Check tracked pointer before any free */
        if (g_tracked_pointer != NULL)
        {
            prc_check_guards("prc_free (tracked check)", g_tracked_pointer);
        }

        /* Add pointer tracking to detect double-free */
        block = ((prc_guarded_block *)p) - 1;

        /* Check if this looks like already-freed memory BEFORE guard check */
        if (block->front_guard[0] == 0xDDDDDDDD ||
            block->size > 0x100000000ULL) /* Sanity check on size */
        {
            printf("*** DOUBLE-FREE DETECTED ***\n");
            printf("    Pointer: %p\n", p);
            printf("    Front guard: 0x%08X (expected 0xDEADBEEF)\n", block->front_guard[0]);
            printf("    Size: %zu (likely garbage)\n", block->size);
            printf("    This pointer was already freed!\n");
#ifdef _WIN32
            __debugbreak();
#else
            __builtin_trap();
#endif
            return; /* Don't attempt to free again */
        }

        /* Check guards before freeing */
        prc_check_guards("prc_free", p);

#if PRC_DEBUG_MEMORY
        {
            size_t i;
            int found = 0;

            /* Search newest-to-oldest so pointer reuse by the allocator does
               not match an older freed record before the current live one. */
            for (i = ctx->current_memory_index; i > 0; i--)
            {
                size_t idx = i - 1;
                if (ctx->debug_memory[idx].data != p)
                    continue;

                found = 1;
                if (ctx->debug_memory[idx].is_free == 0)
                {
                    ctx->debug_memory[idx].is_free = 1;
                }
                else
                {
                    printf("*** DOUBLE-FREE DETECTED (PRC_DEBUG_MEMORY) ***\n");
                    printf("    Pointer %p at debug index %zu already freed!\n", p, idx);
#ifdef _WIN32
                    __debugbreak();
#else
                    __builtin_trap();
#endif
                    return;
                }

                break;
            }

            if (!found)
            {
                printf("*** FREE OF UNTRACKED POINTER (PRC_DEBUG_MEMORY) ***\n");
                printf("    Pointer %p was not found in debug allocation table.\n", p);
            }
        }
#endif
        ctx->hooks.free(ctx->hooks.opaque, block);

        if (p == g_tracked_pointer)
        {
            printf("[TRACKING] Freed vertex_to_original: ptr=%p\n", p);
            g_tracked_pointer = NULL;
        }
    }
#else
#if PRC_DEBUG_MEMORY
    {
        size_t i;
        for (i = ctx->current_memory_index; i > 0; i--)
        {
            size_t idx = i - 1;
            if (ctx->debug_memory[idx].data == p)
            {
                if (ctx->debug_memory[idx].is_free == 0)
                    ctx->debug_memory[idx].is_free = 1;
                break;
            }
        }
    }
#endif
    ctx->hooks.free(ctx->hooks.opaque, p);
#endif
}

void *
prc_realloc(prc_context *ctx, void *p, size_t size)
{
    void *q;

    if (size == 0)
    {
        prc_free(ctx, p);
        return NULL;
    }

    if (p == NULL)
        return prc_malloc(ctx, size);

#ifdef PRC_MEMORY_GUARDS
    {
        prc_guarded_block *old_block, *new_block;

        /* Check tracked pointer before any realloc */
        if (g_tracked_pointer != NULL && g_tracked_pointer != p)
        {
            prc_check_guards("prc_realloc (tracked check)", g_tracked_pointer);
        }

        /* CRITICAL: Check guards BEFORE realloc */
        if (prc_check_guards("prc_realloc (before)", p))
        {
            printf("*** CORRUPTION DETECTED BEFORE REALLOC ***\n");
            printf("    Pointer being realloced: %p\n", p);
            printf("    Requested new size: %zu\n", size);
            printf("    This corruption happened BEFORE this realloc call!\n");
        }

        old_block = ((prc_guarded_block *)p) - 1;

        if (p == g_tracked_pointer)
        {
            printf("[TRACKING] Reallocing vertex_to_original: old_ptr=%p, old_size=%zu, new_size=%zu\n",
                p, old_block->size, size);
        }

        /* Allocate new block with guards */
        new_block = (prc_guarded_block *)ctx->hooks.realloc(ctx->hooks.opaque, old_block,
            sizeof(prc_guarded_block) + size + PRC_MEMORY_GUARD_SIZE);

        if (new_block == NULL)
            return NULL;

        q = (void *)(new_block + 1);

        /* Re-initialize guards with new size */
        prc_init_guards(new_block, size);

        if (p == g_tracked_pointer)
        {
            printf("[TRACKING] Realloced vertex_to_original: new_ptr=%p\n", q);
            g_tracked_pointer = q;
            g_tracked_size = size;
        }
    }
#else
    q = ctx->hooks.realloc(ctx->hooks.opaque, p, size);
#endif

#if PRC_DEBUG_MEMORY
    if (q != NULL)
    {
        size_t i;
        for (i = 0; i < ctx->current_memory_index; i++)
        {
            if (ctx->debug_memory[i].data == p && ctx->debug_memory[i].is_free == 0)
            {
                ctx->debug_memory[i].data = q;
                break;
            }
        }
    }
#endif
    return q;
}

/* Default methods if not set externally */
static void *
prc_malloc_default(void *opaque, size_t size)
{
    return malloc(size);
}

static void *
prc_realloc_default(void *opaque, void *old, size_t size)
{
    return realloc(old, size);
}

static void
prc_free_default(void *opaque, void *ptr)
{
    free(ptr);
}

prc_hooks prc_hooks_default =
{
    NULL,
    prc_malloc_default,
    prc_realloc_default,
    prc_free_default
};