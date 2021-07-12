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
 * Copyright 2016, Blender Foundation.
 */

/** \file
 * \ingroup draw
 */

#pragma once

#include "GPU_batch.h"
#include "MEM_guardedalloc.h"

/* Common */
// #define DRW_DEBUG_MESH_CACHE_REQUEST

#ifdef DRW_DEBUG_MESH_CACHE_REQUEST
#  define DRW_ADD_FLAG_FROM_VBO_REQUEST(flag, vbo, value) \
    (flag |= DRW_vbo_requested(vbo) ? (printf("  VBO requested " #vbo "\n") ? value : value) : 0)
#  define DRW_ADD_FLAG_FROM_IBO_REQUEST(flag, ibo, value) \
    (flag |= DRW_ibo_requested(ibo) ? (printf("  IBO requested " #ibo "\n") ? value : value) : 0)
#else
#  define DRW_ADD_FLAG_FROM_VBO_REQUEST(flag, vbo, value) \
    (flag |= DRW_vbo_requested(vbo) ? (value) : 0)
#  define DRW_ADD_FLAG_FROM_IBO_REQUEST(flag, ibo, value) \
    (flag |= DRW_ibo_requested(ibo) ? (value) : 0)
#endif

BLI_INLINE GPUBatch *DRW_batch_request(GPUBatch **batch)
{
  /* XXX TODO(fclem): We are writing to batch cache here. Need to make this thread safe. */
  if (*batch == NULL) {
    *batch = GPU_batch_calloc();
  }
  return *batch;
}

BLI_INLINE bool DRW_batch_requested(GPUBatch *batch, GPUPrimType prim_type)
{
  /* Batch has been requested if it has been created but not initialized. */
  if (batch != NULL && batch->verts[0] == NULL) {
    /* HACK. We init without a valid VBO and let the first vbo binding
     * fill verts[0]. */
    GPU_batch_init_ex(batch, prim_type, (GPUVertBuf *)1, NULL, (eGPUBatchFlag)0);
    batch->verts[0] = NULL;
    return true;
  }
  return false;
}

BLI_INLINE void DRW_ibo_request(GPUBatch *batch, GPUIndexBuf **ibo)
{
  if (*ibo == NULL) {
    *ibo = GPU_indexbuf_calloc();
  }
  if (batch != NULL) {
    GPU_batch_elembuf_set(batch, *ibo, false);
  }
}

BLI_INLINE bool DRW_ibo_requested(GPUIndexBuf *ibo)
{
  /* TODO: do not rely on data uploaded. This prevents multithreading.
   * (need access to a gl context) */
  return (ibo != NULL && !GPU_indexbuf_is_init(ibo));
}

BLI_INLINE void DRW_vbo_request(GPUBatch *batch, GPUVertBuf **vbo)
{
  if (*vbo == NULL) {
    *vbo = GPU_vertbuf_calloc();
  }
  if (batch != NULL) {
    /* HACK we set vbos that may not yet be valid. */
    GPU_batch_vertbuf_add(batch, *vbo);
  }
}

BLI_INLINE bool DRW_vbo_requested(GPUVertBuf *vbo)
{
  return (vbo != NULL && (GPU_vertbuf_get_status(vbo) & GPU_VERTBUF_INIT) == 0);
}
