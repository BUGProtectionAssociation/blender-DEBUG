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
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup gpu
 */

#pragma once

#include "MEM_guardedalloc.h"

#include "gpu_storage_buffer_private.hh"

#include "glew-mx.h"

namespace blender {
namespace gpu {

/**
 * Implementation of Storage Buffers using OpenGL.
 */
class GLStorageBuf : public StorageBuf {
 private:
  /** Slot to which this UBO is currently bound. -1 if not bound. */
  int slot_ = -1;
  /** OpenGL Object handle. */
  GLuint ssbo_id_ = 0;
  /** Usage type. */
  GPUUsageType usage_;

 public:
  GLStorageBuf(size_t size, GPUUsageType usage, const char *name);
  ~GLStorageBuf();

  void update(const void *data) override;
  void bind(int slot) override;
  void unbind() override;

 private:
  void init();

  MEM_CXX_CLASS_ALLOC_FUNCS("GLStorageBuf");
};

}  // namespace gpu
}  // namespace blender
