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

#include "prc_write_common.h"

/* Floor is 1e-7 mm (0.1 nm), NOT FLT_MIN: coordinates get divided by the
   tolerance to form int32 grid indices, and dividing any physical coordinate
   by FLT_MIN (~1.17e-38) would produce ~8.5e38 and overflow the grid index.
   1e-7 keeps even a 10 km model within int32 range with margin. */
#define PRC_WRITE_TOL_FLOOR_MM 1e-7

double
prc_write_tol_resolve(prc_context *ctx, prc_write_tolerance tol, double bbox_diagonal)
{
    double tolerance_mm;

    if (tol.mode == PRC_WRITE_TOL_RELATIVE)
    {
        if (bbox_diagonal <= 0.0)
        {
            prc_error(ctx, PRC_ERROR_INTERNAL, "prc_write_tol_resolve: relative tolerance requires a positive bbox diagonal\n");
            return PRC_WRITE_TOL_FLOOR_MM;
        }
        tolerance_mm = tol.value * bbox_diagonal;
    }
    else
    {
        tolerance_mm = tol.value;
    }

    if (tolerance_mm < PRC_WRITE_TOL_FLOOR_MM)
        tolerance_mm = PRC_WRITE_TOL_FLOOR_MM;
    return tolerance_mm;
}
