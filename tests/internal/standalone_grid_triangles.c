/* INTERNAL DEVELOPMENT TOOL -- not part of the permanent test suite, not
   registered with CTest, no exit-code contract to keep stable.

   WHAT: Writes a fully-standalone PRC/PDF file (same prc_api_write_prc_buffer
   path as standalone_tetrahedron_triangles.c/teapot_write.c) containing a
   synthetic, tunable-size NxN grid mesh (2*(N-1)*(N-1) triangles), with real
   per-vertex normals (norm_indices != NULL, the PRC_FACETESSDATA_Triangle
   branch -- see standalone_tetrahedron_triangles.c row 15's own comment for
   why this branch matters), single face, TRIANGLES kind.

   WHY IT EXISTS: standalone_tetrahedron_triangles.c's row 15 (4 triangles,
   real per-vertex normals) works in both Acrobat and PDF-XChange, but
   reauthor_tess_pdf.c's real turbine output (8868-26540 triangles, same
   PRC_FACETESSDATA_Triangle branch) fails in Acrobat only. This tool
   isolates whether pure SCALE (clean, synthetic, non-real-world data) alone
   reproduces the failure, independent of any property specific to the real
   decoded turbine geometry (degenerate triangles, extreme coordinate
   magnitudes, duplicate positions, etc.) -- run at a few sizes spanning the
   working/failing range to bisect where (if anywhere) it breaks.

   UPDATE 2026-07-15: the blank-tree evidence matrix's row 19 found that a flat
   two-level tree (root [no part] -> leaf [real part]) is itself a cause of
   Acrobat's blank canvas/tree for OPEN-topology meshes -- inserting one
   extra empty-part intermediate level (root [no part] -> intermediate
   [empty part] -> leaf [real part]) fixed a minimal 2-triangle quad in both
   Acrobat and PDF-XChange. But applying that identical tree fix to the real
   turbine geometry (reauthor_tess_pdf.c, rows 20/21) did NOT fix it -- still
   fails in Acrobat. This tool now ALWAYS uses the row-19 three-level tree
   shape (previously it used a flat two-level tree, which is no longer a
   live variable worth re-testing on its own) so that scale/complexity can be
   bisected as a SEPARATE variable from tree shape, to find out whether the
   deep-tree fix itself breaks down somewhere between this tool's small
   synthetic scale and the turbine's real 8868-26540 triangle scale, or
   whether scale was never the differentiator and something else in the real
   decoded geometry (multiple face groups, degenerate triangles, disconnected
   components, etc.) is still unaccounted for.

   HOW: usage: standalone_grid_triangles.exe <output.prc> <N> [output.pdf]
   N is the grid side length; produces N*N positions, 2*(N-1)*(N-1)
   triangles. E.g. N=100 -> 10000 positions, 19602 triangles (close to
   turbine tess 902's scale); N=175 -> ~30000 triangles (close to tess 901). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "prc_api.h"
#include "prc_context.h"

int main(int argc, char **argv)
{
    prc_context *ctx;
    int code;
    uint32_t n, num_positions, num_triangles;
    double *positions = NULL;
    double *normals = NULL;
    uint32_t *tri_indices = NULL;
    uint32_t face_tri_counts[1];
    uint32_t x, y, t;
    const char *out_pdf = NULL;
    prc_api_write_tessellation tess;
    prc_api_write_rep_item rep_item;
    prc_api_write_node part_node, intermediate, root;
    prc_api_write_node *part_node_ptr;
    prc_api_write_node *intermediate_ptr;
    double bbox_min[3], bbox_max[3];

    if (argc < 3)
    {
        printf("usage: %s <output.prc> <N> [output.pdf]\n", argv[0]);
        return 2;
    }
    n = (uint32_t)atoi(argv[2]);
    if (n < 2) { printf("N must be >= 2\n"); return 2; }
    if (argc >= 4) out_pdf = argv[3];

    /* A gently-curved surface (a shallow sine wave), not a flat plane, so
       real per-vertex normals actually vary -- structurally closer to real
       CAD geometry than a flat grid (whose vertex normals would all be
       identical, (0,0,1), and might accidentally not exercise whatever
       per-vertex-normal-diversity path matters here) than a genuine
       curved surface: a section of a cylinder, radius R=5, spanning +/-60
       degrees around its axis and the full length along it. Every vertex's
       normal is the true radial direction, so adjacent normals differ by a
       small but non-zero, exactly-known angle -- the closest analytic
       surface to "a real curved CAD panel" while still being fully
       synthetic/clean (no real-world data quirks). */
    {
        const double radius = 5.0;
        const double half_angle_deg = 60.0;
        const double half_angle = half_angle_deg * 3.14159265358979323846 / 180.0;
        const double half_length = 5.0;

        num_positions = n * n;
        positions = (double *)malloc(sizeof(double) * 3 * num_positions);
        normals = (double *)malloc(sizeof(double) * 3 * num_positions);
        for (y = 0; y < n; y++)
        {
            for (x = 0; x < n; x++)
            {
                uint32_t i = y * n + x;
                /* x sweeps the angle around the cylinder's axis (Z), y sweeps
                   straight along the axis. */
                double theta = ((double)x / (double)(n - 1) * 2.0 - 1.0) * half_angle;
                double along = ((double)y / (double)(n - 1) * 2.0 - 1.0) * half_length;
                double nx = sin(theta), ny = cos(theta); /* radial direction, in the XY plane */

                positions[i * 3 + 0] = radius * nx;
                positions[i * 3 + 1] = radius * ny;
                positions[i * 3 + 2] = along;
                normals[i * 3 + 0] = nx;
                normals[i * 3 + 1] = ny;
                normals[i * 3 + 2] = 0.0;
            }
        }
    }

    num_triangles = 2 * (n - 1) * (n - 1);
    tri_indices = (uint32_t *)malloc(sizeof(uint32_t) * 3 * num_triangles);
    t = 0;
    for (y = 0; y + 1 < n; y++)
    {
        for (x = 0; x + 1 < n; x++)
        {
            uint32_t i00 = y * n + x, i10 = y * n + (x + 1);
            uint32_t i01 = (y + 1) * n + x, i11 = (y + 1) * n + (x + 1);

            tri_indices[t * 3 + 0] = i00; tri_indices[t * 3 + 1] = i10; tri_indices[t * 3 + 2] = i11; t++;
            tri_indices[t * 3 + 0] = i00; tri_indices[t * 3 + 1] = i11; tri_indices[t * 3 + 2] = i01; t++;
        }
    }

    face_tri_counts[0] = num_triangles;

    memset(&tess, 0, sizeof(tess));
    tess.kind = PRC_API_WRITE_TESS_KIND_TRIANGLES;
    tess.positions = positions;
    tess.num_positions = num_positions;
    tess.normals = normals;
    tess.num_normals = num_positions;
    tess.tri_indices = tri_indices;
    tess.norm_indices = tri_indices; /* real per-vertex normals, paired 1:1 with positions -- same convention as reauthor_tess_pdf.c */
    tess.num_triangles = num_triangles;
    tess.face_tri_counts = face_tri_counts;
    tess.num_faces = 1;

    memset(&rep_item, 0, sizeof(rep_item));
    rep_item.kind = PRC_API_WRITE_RI_SURFACE;
    rep_item.biased_tessellation_index = 1;
    rep_item.is_closed = 0;

    /* Cylinder section: radius 5, +/-60 degrees -> x in [-5*sin60, 5*sin60]
       ~ [-4.33, 4.33], y in [5*cos60, 5] = [2.5, 5.0], z (along axis) in
       [-5, 5]. Padded slightly. */
    bbox_min[0] = -4.5; bbox_min[1] = 2.0; bbox_min[2] = -5.5;
    bbox_max[0] = 4.5; bbox_max[1] = 5.5; bbox_max[2] = 5.5;

    memset(&part_node, 0, sizeof(part_node));
    part_node.rep_items = &rep_item;
    part_node.num_rep_items = 1;
    part_node.bbox_min[0] = bbox_min[0]; part_node.bbox_min[1] = bbox_min[1]; part_node.bbox_min[2] = bbox_min[2];
    part_node.bbox_max[0] = bbox_max[0]; part_node.bbox_max[1] = bbox_max[1]; part_node.bbox_max[2] = bbox_max[2];
    part_node.name = "grid_body";
    part_node.part_name = "grid_faces";

    /* Row-19 tree shape: root [no part] -> intermediate [empty part] ->
       leaf [real part] -- see the file header comment's 2026-07-15 update. */
    part_node_ptr = &part_node;
    memset(&intermediate, 0, sizeof(intermediate));
    intermediate.children = &part_node_ptr;
    intermediate.num_children = 1;
    intermediate.name = "grid_scene";
    intermediate.has_empty_part = 1;

    intermediate_ptr = &intermediate;
    memset(&root, 0, sizeof(root));
    root.children = &intermediate_ptr;
    root.num_children = 1;
    root.name = "grid";

    ctx = prc_api_new_context(NULL);
    if (ctx == NULL) { printf("context creation failed\n"); return 1; }

    {
        uint8_t *prc_buf = NULL;
        size_t prc_buf_size = 0;
        FILE *out;

        printf("Encoding %u positions, %u triangles (N=%u)...\n", num_positions, num_triangles, n);
        code = prc_api_write_prc_buffer(ctx, "nanoPRC", &root, &tess, 1, &prc_buf, &prc_buf_size);
        if (code != 0)
        {
            printf("prc_api_write_prc_buffer failed (code %d)\n", code);
            prc_api_print_error_stack(ctx);
            prc_api_release_context(ctx);
            return 1;
        }

        out = fopen(argv[1], "wb");
        if (out == NULL || fwrite(prc_buf, 1, prc_buf_size, out) != prc_buf_size)
        {
            printf("failed to write %s\n", argv[1]);
            prc_api_write_prc_buffer_free(ctx, prc_buf);
            prc_api_release_context(ctx);
            return 1;
        }
        fclose(out);
        printf("Wrote %s (%zu bytes)\n", argv[1], prc_buf_size);

        if (out_pdf != NULL)
        {
            prc_pdf_view_spec view;
            prc_pdf_write_options pdf_opts;

            memset(&view, 0, sizeof(view));
            view.name = "Default";
            view.eye[0] = 15.0; view.eye[1] = -15.0; view.eye[2] = 12.0;
            view.target[0] = 0.0; view.target[1] = 3.75; view.target[2] = 0.0;
            view.up[0] = 0.0; view.up[1] = 0.0; view.up[2] = 1.0;
            view.is_default = 1;

            memset(&pdf_opts, 0, sizeof(pdf_opts));
            pdf_opts.views = &view;
            pdf_opts.num_views = 1;

            code = prc_api_pdf_embed_prc(ctx, out_pdf, prc_buf, prc_buf_size, &pdf_opts);
            if (code < 0)
            {
                printf("prc_api_pdf_embed_prc failed: %d\n", code);
                prc_api_print_error_stack(ctx);
            }
            else
            {
                printf("Wrote %s (with explicit Default view)\n", out_pdf);
            }
        }

        prc_api_write_prc_buffer_free(ctx, prc_buf);
    }

    prc_api_release_context(ctx);
    free(positions);
    free(normals);
    free(tri_indices);
    return 0;
}
