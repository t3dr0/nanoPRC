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

#ifndef PRC_WRITE_COMMON_H
#define PRC_WRITE_COMMON_H

#include <stdint.h>

/* Internal shared declarations for the write facility (encoder). The public
   write-side types (prc_write_tolerance, prc_write_tol_resolve) live in
   prc_api.h; this header exists for internals shared between the encoder's
   translation units. */

#include "../include/prc_api.h"
#include "prc_data.h"
#include "prc_bit.h"

/* Byte sizes of the raw (non-bit-packed) little-endian fields used by the
   two file-level headers this write facility produces manually --
   prc_write_file_structure.c's file-structure header and
   prc_write_model.c's main prc_header. Shared here so both files' byte-
   layout math (and each file's own PRC_WRITE_*_SIZE constant/layout
   function) is built from the same named units instead of separately
   re-deriving "16" or "4" in each place. */
#define PRC_WRITE_U32_BYTES 4u
#define PRC_WRITE_UNIQUE_ID_BYTES 16u /* 4x uint32 */
#define PRC_WRITE_SIGNATURE_BYTES 3u  /* "PRC" */

/* Writes `v` as 4 little-endian bytes at `p`, returning p + 4. The one
   raw-byte (non-bit-packed) primitive shared by prc_write_file_structure.c
   and prc_write_model.c, exactly mirroring prc_read_32bits_unsigned's
   little-endian convention in prc_parse_main.c. */
uint8_t *prc_write_le_uint32(uint8_t *p, uint32_t v);

/* Fixed, non-zero placeholder unique IDs (Table 5/35's 4x uint32 "unique
   id" fields, used both raw in the two file-level headers and via
   prc_bitwrite_uint32 in the model section's far reference). All-zero IDs
   were confirmed the hard way to be rejected as a null/invalid sentinel by
   at least one external reader ("Empty Scene Detected" persisted after
   every graphics-visibility and reference-target fix, and cleared once
   these were made non-zero) -- these do not need to be cryptographically
   random for a single-file writer to work correctly, only non-zero and
   mutually consistent everywhere the format requires equality.

   PRC_WRITE_FILE_UID0 is independent of PRC_WRITE_FILE_STRUCT_UID0 --
   revised back from a brief attempt at making them equal, which was based
   on a single reference file (not examples/cube.pdf) whose main header's
   unique_id_file happened to equal its file structure's own identity. A
   THIRD, independently-produced real file (a different generator again)
   directly contradicts that: its main header's unique_id_file and its
   file structure's own embedded unique_id_file are different values.
   Between three real producers agreeing on nothing about this field's
   relationship to the file structure's identity beyond "non-zero", it
   looks like generator-specific noise rather than a real requirement, so
   this write facility goes back to treating it as independent. PRC_WRITE_
   APP_UID0 (authoring application identity) is likewise independent of
   both in every real file checked so far. A future revision could
   generate these per call instead of reusing fixed values. */
#define PRC_WRITE_FILE_UID0 1u
#define PRC_WRITE_APP_UID0 2u
#define PRC_WRITE_FILE_STRUCT_UID0 3u

/* Writes a 4x uint32 unique id {word0, 0, 0, 0} as raw little-endian bytes
   at `p`, returning p + 16 (PRC_WRITE_UNIQUE_ID_BYTES). */
uint8_t *prc_write_le_unique_id(uint8_t *p, uint32_t word0);

/* min_vers_for_read / auth_vers, written in both the main prc_header and
   every PRC_TYPE_ASM_FileStructure header (Tables 5/37). Confirmed the
   hard way: 0 does NOT mean "any reader accepts this" the way an earlier
   assumption in this codebase held -- the spec documents at least some
   entity fields as present only conditionally on authoring_version
   crossing a threshold, "the sole mechanism for detecting its presence;
   there is no secondary flag" (e.g. PRC_TYPE_GRAPH_DirectionalLight's
   intensity, gated on >= 8030).

   The ISO 14739 draft is explicit that 10001 is the ONLY compliant PRC
   version ("This international standard specifies the PRC version 10001.
   Documents with other PRC versions are non-compliant" -- version numbers
   are year-modulo-2000 + day-of-year, so 10001 = 2010 day 1, this
   document's own publication version) -- but a real, deployed reader
   (Adobe Acrobat) REJECTS files declaring it outright: "This 3D model
   requires a more recent version of Acrobat", blank 3D scene, empty model
   tree, even on a fully updated Acrobat install. This matches the spec's
   own described reader behavior to the letter ("if [the reader's]
   current_version is less than the file's minimal_version_for_read, the
   reader shall not continue to process the file and report an error") --
   Acrobat's PRC engine has apparently never been updated to recognize
   version 10001 as valid, so declaring the literal spec-compliant value
   makes the single most important real-world reader refuse the file
   entirely. examples/cube.pdf's 7094/23306 -- confirmed to open
   successfully in real Acrobat -- are used instead: not spec-compliant
   per the letter of the standard, but the only values actually proven to
   work in the reader that matters most. Spec-purity was tried, tested
   against real Acrobat, and lost. */
#define PRC_WRITE_MIN_VERS_FOR_READ 7094u
#define PRC_WRITE_AUTH_VERS 23306u

/* Writes ContentPRCBase/ContentPRCRefBase's `name` field (Table 31): bit
   same=1 with no following string if `name` is NULL (matching this write
   facility's long-standing "no name" default), else same=0 followed by
   `name`'s bytes. Shared by every translation unit that emits a name --
   parts, product occurrences, and the model file -- since it's the one
   piece of write-side name support to add rather than duplicate; see
   prc_api_write_node's name/part_name fields and prc_api_write_prc_file's
   model_name parameter in include/prc_api.h for the public entry points
   that supply it. */
int prc_write_name(prc_context *ctx, prc_bit_write_state *s, const char *name);

#endif
