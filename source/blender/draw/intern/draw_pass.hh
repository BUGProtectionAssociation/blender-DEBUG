/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. */

#pragma once

/** \file
 * \ingroup draw
 *
 * Passes record draw commands. There exists different pass types for different purpose but they
 * only change in resource load (memory & CPU usage). They can be swapped without any functional
 * change.
 *
 * `PassMain`:
 * Should be used on heavy load passes such as ones that may contain scene objects. Draw call
 * submission is optimized for large number of draw calls. But has a significant overhead per
 * #Pass. Use many #PassSub along with a main #Pass to reduce the overhead and allow groupings of
 * commands.
 *
 * `Pass(Main|Simple)::Sub`:
 * A lightweight #Pass that lives inside a main #Pass. It can only be created from #Pass.sub()
 * and is auto managed. This mean it can be created, filled and thrown away. A #PassSub reference
 * is valid until the next #Pass.init() of the parent pass. Commands recorded inside a #PassSub are
 * inserted inside the parent #Pass where the sub have been created durring submission.
 *
 * `PassSimple`:
 * Does not have the overhead of #PassMain but does not have the culling and batching optimization.
 *
 * NOTE: A pass can be recorded once and resubmitted any number of time. This can be a good
 * optimization for passes that are always the same for each frame. The only thing to be aware of
 * is the life time of external resources. If a pass contains draw-calls with non default
 * ResourceHandle (not 0) or a reference to any non static resources (GPUBatch, PushConstant ref,
 * ResourceBind ref) it will have to be re-recorded if any of these reference becomes invalid.
 */

#include "BKE_image.h"
#include "BLI_vector.hh"
#include "DRW_gpu_wrapper.hh"
#include "GPU_debug.h"
#include "GPU_material.h"

#include "draw_command.hh"
#include "draw_handle.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_shader_shared.h"
#include "draw_state.h"

#include "intern/gpu_codegen.h"

namespace blender::draw {

using namespace blender::draw;
using namespace blender::draw::command;

class Manager;

/* -------------------------------------------------------------------- */
/** \name Pass API
 * \{ */

namespace detail {

/**
 * Public API of a draw pass.
 */
template<
    /** Type of command buffer used to create the draw calls. */
    typename DrawCommandBufType>
class PassBase {
  friend Manager;

 protected:
  /** Highest level of the command stream. Split command stream in different command types. */
  Vector<command::Header> headers_;
  /** Commands referenced by headers (which contains their types). */
  Vector<command::Undetermined> commands_;
  /* Reference to draw commands buffer. Either own or from parent pass. */
  DrawCommandBufType &draw_commands_buf_;
  /* Reference to sub-pass commands buffer. Either own or from parent pass. */
  Vector<PassBase<DrawCommandBufType>, 0> &sub_passes_;
  /** Currently bound shader. Used for interface queries. */
  GPUShader *shader_;

 public:
  const char *debug_name;

  PassBase(const char *name,
           DrawCommandBufType &draw_command_buf,
           Vector<PassBase<DrawCommandBufType>, 0> &sub_passes,
           GPUShader *shader = nullptr)
      : draw_commands_buf_(draw_command_buf),
        sub_passes_(sub_passes),
        shader_(shader),
        debug_name(name){};

  /**
   * Reset the pass command pool.
   * NOTE: Implemented in derived class. Not a virtual function to avoid indirection. Here only for
   * API readability listing.
   */
  void init();

  /**
   * Create a sub-pass inside this pass.
   */
  PassBase<DrawCommandBufType> &sub(const char *name);

  /**
   * Changes the fixed function pipeline state.
   * Starts as DRW_STATE_NO_DRAW at the start of a Pass submission.
   * SubPass inherit previous pass state.
   *
   * IMPORTANT: This does not set the stencil mask/reference values. Add a call to state_stencil()
   * to ensure correct behavior of stencil aware draws.
   */
  void state_set(DRWState state);

  /**
   * Clear the current frame-buffer.
   */
  void clear_color(float4 color);
  void clear_depth(float depth);
  void clear_stencil(uint8_t stencil);
  void clear_depth_stencil(float depth, uint8_t stencil);
  void clear_color_depth_stencil(float4 color, float depth, uint8_t stencil);

  /**
   * Reminders:
   * - (compare_mask & reference) is what is tested against (compare_mask & stencil_value)
   *   stencil_value being the value stored in the stencil buffer.
   * - (write-mask & reference) is what gets written if the test condition is fulfilled.
   */
  void state_stencil(uint8_t write_mask, uint8_t reference, uint8_t compare_mask);

  /**
   * Bind a shader. Any following bind() or push_constant() call will use its interface.
   */
  void shader_set(GPUShader *shader);

  /**
   * Bind a material shader along with its associated resources. Any following bind() or
   * push_constant() call will use its interface.
   * IMPORTANT: Assumes material is compiled and can be used (no compilation error).
   */
  void material_set(Manager &manager, GPUMaterial *material);

  /**
   * Record a draw call.
   * NOTE: Setting the count or first to -1 will use the values from the batch.
   * NOTE: An instance or vertex count of 0 will discard the draw call. It will not be recorded.
   */
  void draw(GPUBatch *batch,
            uint instance_len = -1,
            uint vertex_len = -1,
            uint vertex_first = -1,
            ResourceHandle handle = {0});

  /**
   * Shorter version for the common case.
   * NOTE: Implemented in derived class. Not a virtual function to avoid indirection.
   */
  void draw(GPUBatch *batch, ResourceHandle handle);

  /**
   * Record a procedural draw call. Geometry is **NOT** source from a GPUBatch.
   * NOTE: An instance or vertex count of 0 will discard the draw call. It will not be recorded.
   */
  void draw_procedural(GPUPrimType primitive,
                       uint instance_len,
                       uint vertex_len,
                       uint vertex_first = -1,
                       ResourceHandle handle = {0});

  /**
   * Indirect variants.
   * NOTE: If needed, the resource id need to also be set accordingly in the DrawCommand.
   */
  void draw_indirect(GPUBatch *batch,
                     StorageBuffer<DrawCommand> &indirect_buffer,
                     ResourceHandle handle = {0});
  void draw_procedural_indirect(GPUPrimType primitive,
                                StorageBuffer<DrawCommand> &indirect_buffer,
                                ResourceHandle handle = {0});

  /**
   * Record a compute dispatch call.
   */
  void dispatch(int3 group_len);
  void dispatch(int3 *group_len);
  void dispatch(StorageBuffer<DispatchCommand> &indirect_buffer);

  /**
   * Record a barrier call to synchronize arbitrary load/store operation between draw calls.
   */
  void barrier(eGPUBarrier type);

  /**
   * Bind a shader resource.
   *
   * Reference versions are to be used when the resource might be resize / realloc or even change
   * between the time it is referenced and the time it is dereferenced for drawing.
   *
   * IMPORTANT: Will keep a reference to the data and dereference it upon drawing. Make sure data
   * still alive until pass submission.
   */
  void bind(const char *name, GPUStorageBuf *buffer);
  void bind(const char *name, GPUUniformBuf *buffer);
  void bind(const char *name, draw::Image *image);
  void bind(const char *name, GPUTexture *texture, eGPUSamplerState state = GPU_SAMPLER_MAX);
  void bind(const char *name, GPUStorageBuf **buffer);
  void bind(const char *name, GPUUniformBuf **buffer);
  void bind(const char *name, draw::Image **image);
  void bind(const char *name, GPUTexture **texture, eGPUSamplerState state = GPU_SAMPLER_MAX);
  void bind(int slot, GPUStorageBuf *buffer);
  void bind(int slot, GPUUniformBuf *buffer);
  void bind(int slot, draw::Image *image);
  void bind(int slot, GPUTexture *texture, eGPUSamplerState state = GPU_SAMPLER_MAX);
  void bind(int slot, GPUStorageBuf **buffer);
  void bind(int slot, GPUUniformBuf **buffer);
  void bind(int slot, draw::Image **image);
  void bind(int slot, GPUTexture **texture, eGPUSamplerState state = GPU_SAMPLER_MAX);

  /**
   * Update a shader constant.
   *
   * Reference versions are to be used when the resource might change between the time it is
   * referenced and the time it is dereferenced for drawing.
   *
   * IMPORTANT: Will keep a reference to the data and dereference it upon drawing. Make sure data
   * still alive until pass submission.
   *
   * NOTE: bool reference version is expected to take bool1 reference which is aliased to int.
   */
  void push_constant(const char *name, const float &data);
  void push_constant(const char *name, const float2 &data);
  void push_constant(const char *name, const float3 &data);
  void push_constant(const char *name, const float4 &data);
  void push_constant(const char *name, const int &data);
  void push_constant(const char *name, const int2 &data);
  void push_constant(const char *name, const int3 &data);
  void push_constant(const char *name, const int4 &data);
  void push_constant(const char *name, const bool &data);
  void push_constant(const char *name, const float4x4 &data);
  void push_constant(const char *name, const float *data, int array_len = 1);
  void push_constant(const char *name, const float2 *data, int array_len = 1);
  void push_constant(const char *name, const float3 *data, int array_len = 1);
  void push_constant(const char *name, const float4 *data, int array_len = 1);
  void push_constant(const char *name, const int *data, int array_len = 1);
  void push_constant(const char *name, const int2 *data, int array_len = 1);
  void push_constant(const char *name, const int3 *data, int array_len = 1);
  void push_constant(const char *name, const int4 *data, int array_len = 1);
  void push_constant(const char *name, const float4x4 *data);

  /**
   * Turn the pass into a string for inspection.
   */
  std::string serialize(std::string line_prefix = "") const;

  friend std::ostream &operator<<(std::ostream &stream, const PassBase &pass)
  {
    return stream << pass.serialize();
  }

 protected:
  /**
   * Internal Helpers
   */

  int push_constant_offset(const char *name);

  void clear(eGPUFrameBufferBits planes, float4 color, float depth, uint8_t stencil);

  GPUBatch *procedural_batch_get(GPUPrimType primitive);

  /**
   * Return a new command recorded with the given type.
   */
  command::Undetermined &create_command(command::Type type);

  void submit(command::RecordingState &state) const;
};

template<typename DrawCommandBufType> class Pass : public detail::PassBase<DrawCommandBufType> {
 public:
  using Sub = detail::PassBase<DrawCommandBuf>;

 private:
  /** Sub-passes referenced by headers. */
  Vector<detail::PassBase<DrawCommandBufType>, 0> sub_passes_main_;
  /** Draws are recorded as indirect draws for compatibility with the multi-draw pipeline. */
  DrawCommandBufType draw_commands_buf_main_;

 public:
  Pass(const char *name)
      : detail::PassBase<DrawCommandBufType>(name, draw_commands_buf_main_, sub_passes_main_){};

  void init()
  {
    this->headers_.clear();
    this->commands_.clear();
    this->sub_passes_.clear();
    this->draw_commands_buf_.clear();
  }
};  // namespace blender::draw

}  // namespace detail

/** \} */

/* -------------------------------------------------------------------- */
/** \name Pass types
 * \{ */

/**
 * Normal pass type. No visibility or draw-call optimisation.
 */
// using PassSimple = detail::Pass<DrawCommandBuf>;

/**
 * Main pass type.
 * Optimized for many draw calls and sub-pass.
 *
 * IMPORTANT: To be used only for passes containing lots of draw calls since it has a potentially
 * high overhead due to batching and culling optimizations.
 */
// using PassMain = detail::Pass<DrawMultiBuf>;

/** \} */

namespace detail {

/* -------------------------------------------------------------------- */
/** \name PassBase Implementation
 * \{ */

template<class T> inline command::Undetermined &PassBase<T>::create_command(command::Type type)
{
  int64_t index = commands_.append_and_get_index({});
  headers_.append({type, static_cast<uint>(index)});
  return commands_[index];
}

template<class T>
inline void PassBase<T>::clear(eGPUFrameBufferBits planes,
                               float4 color,
                               float depth,
                               uint8_t stencil)
{
  create_command(command::Type::Clear).clear = {(uint8_t)planes, stencil, depth, color};
}

template<class T> inline GPUBatch *PassBase<T>::procedural_batch_get(GPUPrimType primitive)
{
  switch (primitive) {
    case GPU_PRIM_POINTS:
      return drw_cache_procedural_points_get();
    case GPU_PRIM_LINES:
      return drw_cache_procedural_lines_get();
    case GPU_PRIM_TRIS:
      return drw_cache_procedural_triangles_get();
    case GPU_PRIM_TRI_STRIP:
      return drw_cache_procedural_triangle_strips_get();
    default:
      /* Add new one as needed. */
      BLI_assert_unreachable();
      return nullptr;
  }
}

template<class T> inline PassBase<T> &PassBase<T>::sub(const char *name)
{
  int64_t index = sub_passes_.append_and_get_index(
      PassBase(name, draw_commands_buf_, sub_passes_, shader_));
  headers_.append({command::Type::SubPass, static_cast<uint>(index)});
  return sub_passes_[index];
}

template<class T> void PassBase<T>::submit(command::RecordingState &state) const
{
  GPU_debug_group_begin(debug_name);

  for (const command::Header &header : headers_) {
    switch (header.type) {
      default:
      case Type::None:
        break;
      case Type::SubPass:
        sub_passes_[header.index].submit(state);
        break;
      case command::Type::ShaderBind:
        commands_[header.index].shader_bind.execute(state);
        break;
      case command::Type::ResourceBind:
        commands_[header.index].resource_bind.execute();
        break;
      case command::Type::PushConstant:
        commands_[header.index].push_constant.execute(state);
        break;
      case command::Type::Draw:
        commands_[header.index].draw.execute(state);
        break;
      case command::Type::DrawMulti:
        commands_[header.index].draw_multi.execute(state);
        break;
      case command::Type::DrawIndirect:
        commands_[header.index].draw_indirect.execute(state);
        break;
      case command::Type::Dispatch:
        commands_[header.index].dispatch.execute(state);
        break;
      case command::Type::DispatchIndirect:
        commands_[header.index].dispatch_indirect.execute(state);
        break;
      case command::Type::Barrier:
        commands_[header.index].barrier.execute();
        break;
      case command::Type::Clear:
        commands_[header.index].clear.execute();
        break;
      case command::Type::StateSet:
        commands_[header.index].state_set.execute(state);
        break;
      case command::Type::StencilSet:
        commands_[header.index].stencil_set.execute();
        break;
    }
  }

  GPU_debug_group_end();
}

template<class T> std::string PassBase<T>::serialize(std::string line_prefix) const
{
  std::stringstream ss;
  ss << line_prefix << "." << debug_name << std::endl;
  line_prefix += "  ";
  for (const command::Header &header : headers_) {
    switch (header.type) {
      default:
      case Type::None:
        break;
      case Type::SubPass:
        ss << sub_passes_[header.index].serialize(line_prefix);
        break;
      case Type::ShaderBind:
        ss << line_prefix << commands_[header.index].shader_bind.serialize() << std::endl;
        break;
      case Type::ResourceBind:
        ss << line_prefix << commands_[header.index].resource_bind.serialize() << std::endl;
        break;
      case Type::PushConstant:
        ss << line_prefix << commands_[header.index].push_constant.serialize() << std::endl;
        break;
      case Type::Draw:
        ss << line_prefix << commands_[header.index].draw.serialize() << std::endl;
        break;
      case Type::DrawMulti:
        ss << commands_[header.index].draw_multi.serialize(line_prefix);
        break;
      case Type::DrawIndirect:
        ss << line_prefix << commands_[header.index].draw_indirect.serialize() << std::endl;
        break;
      case Type::Dispatch:
        ss << line_prefix << commands_[header.index].dispatch.serialize() << std::endl;
        break;
      case Type::DispatchIndirect:
        ss << line_prefix << commands_[header.index].dispatch_indirect.serialize() << std::endl;
        break;
      case Type::Barrier:
        ss << line_prefix << commands_[header.index].barrier.serialize() << std::endl;
        break;
      case Type::Clear:
        ss << line_prefix << commands_[header.index].clear.serialize() << std::endl;
        break;
      case Type::StateSet:
        ss << line_prefix << commands_[header.index].state_set.serialize() << std::endl;
        break;
      case Type::StencilSet:
        ss << line_prefix << commands_[header.index].stencil_set.serialize() << std::endl;
        break;
    }
  }
  return ss.str();
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Draw calls
 * \{ */

template<class T>
inline void PassBase<T>::draw(
    GPUBatch *batch, uint instance_len, uint vertex_len, uint vertex_first, ResourceHandle handle)
{
  if (instance_len == 0 || vertex_len == 0) {
    return;
  }
  BLI_assert(shader_);
  draw_commands_buf_.append_draw(
      headers_, commands_, batch, instance_len, vertex_len, vertex_first, handle);
}

template<class T> inline void PassBase<T>::draw(GPUBatch *batch, ResourceHandle handle)
{
  draw(batch, -1, -1, -1, handle);
}

template<class T>
inline void PassBase<T>::draw_procedural(GPUPrimType primitive,
                                         uint instance_len,
                                         uint vertex_len,
                                         uint vertex_first,
                                         ResourceHandle handle)
{
  draw(procedural_batch_get(primitive), instance_len, vertex_len, vertex_first, handle);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Indirect draw calls
 * \{ */

template<class T>
inline void PassBase<T>::draw_indirect(GPUBatch *batch,
                                       StorageBuffer<DrawCommand> &indirect_buffer,
                                       ResourceHandle handle)
{
  BLI_assert(shader_);
  create_command(Type::DrawIndirect).draw_indirect = {batch, &indirect_buffer, handle};
}

template<class T>
inline void PassBase<T>::draw_procedural_indirect(GPUPrimType primitive,
                                                  StorageBuffer<DrawCommand> &indirect_buffer,
                                                  ResourceHandle handle)
{
  draw_indirect(procedural_batch_get(primitive), indirect_buffer, handle);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Compute Dispatch Implementation
 * \{ */

template<class T> inline void PassBase<T>::dispatch(int3 group_len)
{
  BLI_assert(shader_);
  create_command(Type::Dispatch).dispatch = {group_len};
}

template<class T> inline void PassBase<T>::dispatch(int3 *group_len)
{
  BLI_assert(shader_);
  create_command(Type::Dispatch).dispatch = {group_len};
}

template<class T>
inline void PassBase<T>::dispatch(StorageBuffer<DispatchCommand> &indirect_buffer)
{
  BLI_assert(shader_);
  create_command(Type::DispatchIndirect).dispatch_indirect = {&indirect_buffer};
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Clear Implementation
 * \{ */

template<class T> inline void PassBase<T>::clear_color(float4 color)
{
  clear(GPU_COLOR_BIT, color, 0.0f, 0);
}

template<class T> inline void PassBase<T>::clear_depth(float depth)
{
  clear(GPU_DEPTH_BIT, float4(0.0f), depth, 0);
}

template<class T> inline void PassBase<T>::clear_stencil(uint8_t stencil)
{
  clear(GPU_STENCIL_BIT, float4(0.0f), 0.0f, stencil);
}

template<class T> inline void PassBase<T>::clear_depth_stencil(float depth, uint8_t stencil)
{
  clear(GPU_DEPTH_BIT | GPU_STENCIL_BIT, float4(0.0f), depth, stencil);
}

template<class T>
inline void PassBase<T>::clear_color_depth_stencil(float4 color, float depth, uint8_t stencil)
{
  clear(GPU_DEPTH_BIT | GPU_STENCIL_BIT | GPU_COLOR_BIT, color, depth, stencil);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Barrier Implementation
 * \{ */

template<class T> inline void PassBase<T>::barrier(eGPUBarrier type)
{
  create_command(Type::Barrier).barrier = {type};
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name State Implementation
 * \{ */

template<class T> inline void PassBase<T>::state_set(DRWState state)
{
  create_command(Type::StateSet).state_set = {state};
}

template<class T>
inline void PassBase<T>::state_stencil(uint8_t write_mask, uint8_t reference, uint8_t compare_mask)
{
  create_command(Type::StencilSet).stencil_set = {write_mask, reference, compare_mask};
}

template<class T> inline void PassBase<T>::shader_set(GPUShader *shader)
{
  shader_ = shader;
  create_command(Type::ShaderBind).shader_bind = {shader};
}

template<class T> inline void PassBase<T>::material_set(Manager &manager, GPUMaterial *material)
{
  GPUPass *gpupass = GPU_material_get_pass(material);
  shader_set(GPU_pass_shader_get(gpupass));

  /* Bind all textures needed by the material. */
  ListBase textures = GPU_material_textures(material);
  for (GPUMaterialTexture *tex : ListBaseWrapper<GPUMaterialTexture>(textures)) {
    if (tex->ima) {
      /* Image */
      ImageUser *iuser = tex->iuser_available ? &tex->iuser : nullptr;
      if (tex->tiled_mapping_name[0]) {
        GPUTexture *tiles = BKE_image_get_gpu_tiles(tex->ima, iuser, nullptr);
        manager.acquire_texture(tiles);
        bind(tex->sampler_name, tiles, (eGPUSamplerState)tex->sampler_state);

        GPUTexture *tile_map = BKE_image_get_gpu_tilemap(tex->ima, iuser, nullptr);
        manager.acquire_texture(tile_map);
        bind(tex->tiled_mapping_name, tile_map, (eGPUSamplerState)tex->sampler_state);
      }
      else {
        GPUTexture *texture = BKE_image_get_gpu_texture(tex->ima, iuser, nullptr);
        manager.acquire_texture(texture);
        bind(tex->sampler_name, texture, (eGPUSamplerState)tex->sampler_state);
      }
    }
    else if (tex->colorband) {
      /* Color Ramp */
      bind(tex->sampler_name, *tex->colorband);
    }
  }

  GPUUniformBuf *ubo = GPU_material_uniform_buffer_get(material);
  if (ubo != nullptr) {
    bind(GPU_UBO_BLOCK_NAME, ubo);
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Resource bind Implementation
 * \{ */

template<class T> inline int PassBase<T>::push_constant_offset(const char *name)
{
  return GPU_shader_get_uniform(shader_, name);
}

template<class T> inline void PassBase<T>::bind(const char *name, GPUStorageBuf *buffer)
{
  bind(GPU_shader_get_ssbo(shader_, name), buffer);
}

template<class T> inline void PassBase<T>::bind(const char *name, GPUUniformBuf *buffer)
{
  bind(GPU_shader_get_uniform_block_binding(shader_, name), buffer);
}

template<class T>
inline void PassBase<T>::bind(const char *name, GPUTexture *texture, eGPUSamplerState state)
{
  bind(GPU_shader_get_texture_binding(shader_, name), texture, state);
}

template<class T> inline void PassBase<T>::bind(const char *name, draw::Image *image)
{
  bind(GPU_shader_get_texture_binding(shader_, name), image);
}

template<class T> inline void PassBase<T>::bind(int slot, GPUStorageBuf *buffer)
{
  create_command(Type::ResourceBind).resource_bind = {slot, buffer};
}

template<class T> inline void PassBase<T>::bind(int slot, GPUUniformBuf *buffer)
{
  create_command(Type::ResourceBind).resource_bind = {slot, buffer};
}

template<class T>
inline void PassBase<T>::bind(int slot, GPUTexture *texture, eGPUSamplerState state)
{
  create_command(Type::ResourceBind).resource_bind = {slot, texture, state};
}

template<class T> inline void PassBase<T>::bind(int slot, draw::Image *image)
{
  create_command(Type::ResourceBind).resource_bind = {slot, image};
}

template<class T> inline void PassBase<T>::bind(const char *name, GPUStorageBuf **buffer)
{
  bind(GPU_shader_get_ssbo(shader_, name), buffer);
}

template<class T> inline void PassBase<T>::bind(const char *name, GPUUniformBuf **buffer)
{
  bind(GPU_shader_get_uniform_block_binding(shader_, name), buffer);
}

template<class T>
inline void PassBase<T>::bind(const char *name, GPUTexture **texture, eGPUSamplerState state)
{
  bind(GPU_shader_get_texture_binding(shader_, name), texture, state);
}

template<class T> inline void PassBase<T>::bind(const char *name, draw::Image **image)
{
  bind(GPU_shader_get_texture_binding(shader_, name), image);
}

template<class T> inline void PassBase<T>::bind(int slot, GPUStorageBuf **buffer)
{

  create_command(Type::ResourceBind).resource_bind = {slot, buffer};
}

template<class T> inline void PassBase<T>::bind(int slot, GPUUniformBuf **buffer)
{
  create_command(Type::ResourceBind).resource_bind = {slot, buffer};
}

template<class T>
inline void PassBase<T>::bind(int slot, GPUTexture **texture, eGPUSamplerState state)
{
  create_command(Type::ResourceBind).resource_bind = {slot, texture, state};
}

template<class T> inline void PassBase<T>::bind(int slot, draw::Image **image)
{
  create_command(Type::ResourceBind).resource_bind = {slot, image};
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Push Constant Implementation
 * \{ */

template<class T> inline void PassBase<T>::push_constant(const char *name, const float &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const float2 &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const float3 &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const float4 &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const int &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const int2 &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const int3 &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const int4 &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const bool &data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const float *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const float2 *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const float3 *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const float4 *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const int *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const int2 *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const int3 *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T>
inline void PassBase<T>::push_constant(const char *name, const int4 *data, int array_len)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data, array_len};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const float4x4 *data)
{
  create_command(Type::PushConstant).push_constant = {push_constant_offset(name), data};
}

template<class T> inline void PassBase<T>::push_constant(const char *name, const float4x4 &data)
{
  /* WORKAROUND: Push 3 consecutive commands to hold the 64 bytes of the float4x4.
   * This assumes that all commands are always stored in flat array of memory. */
  Undetermined commands[3];

  PushConstant &cmd = commands[0].push_constant;
  cmd.location = push_constant_offset(name);
  cmd.array_len = 1;
  cmd.comp_len = 16;
  cmd.type = PushConstant::Type::FloatValue;
  /* Copy overrides the next 2 commands. We append them as Type::None to not evaluate them. */
  *reinterpret_cast<float4x4 *>(&cmd.float4_value) = data;

  create_command(Type::PushConstant) = commands[0];
  create_command(Type::None) = commands[1];
  create_command(Type::None) = commands[2];
}

/** \} */

}  // namespace detail

}  // namespace blender::draw
