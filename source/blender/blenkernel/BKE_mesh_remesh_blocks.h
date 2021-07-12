/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2019 by Blender Foundation
 * All rights reserved.
 */

#pragma once

/** \file
 * \ingroup bke
 */

#ifdef __cplusplus
extern "C" {
#endif

struct Mesh;

typedef enum eRemeshBlocksMode {
  /* blocky */
  REMESH_BLOCKS_CENTROID = 0,
  /* smooth */
  REMESH_BLOCKS_MASS_POINT = 1,
  /* keeps sharp edges */
  REMESH_BLOCKS_SHARP_FEATURES = 2,
} eRemeshBlocksMode;


struct Mesh *BKE_mesh_remesh_blocks_to_mesh_nomain(struct Mesh *mesh,
                                            const char remesh_flag,
                                            const char remesh_mode,
                                            const float threshold,
                                            const int hermite_num,
                                            const float scale,
                                            const int depth);

#ifdef __cplusplus
}
#endif
