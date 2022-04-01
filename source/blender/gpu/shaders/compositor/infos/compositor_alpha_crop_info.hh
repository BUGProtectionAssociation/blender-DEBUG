/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(compositor_alpha_crop)
    .local_group_size(16, 16)
    .push_constant(Type::IVEC2, "lower_bound")
    .push_constant(Type::IVEC2, "upper_bound")
    .sampler(0, ImageType::FLOAT_2D, "input_image")
    .image(0, GPU_RGBA16F, Qualifier::WRITE, ImageType::FLOAT_2D, "output_image")
    .compute_source("compositor_alpha_crop.glsl")
    .do_static_compilation(true);
