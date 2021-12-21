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
 */
#pragma once

#include <pxr/pxr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdShade/material.h>

#include <string>

struct bNode;
struct bNodeTree;
struct Material;
struct USDExportParams;

namespace blender::io::usd {

struct USDExporterContext;

void create_usd_preview_surface_material(const USDExporterContext &usd_export_context,
                                         Material *material,
                                         pxr::UsdShadeMaterial &usd_material,
                                         const std::string &default_uv = "");

void create_usd_viewport_material(const USDExporterContext &usd_export_context,
                                  Material *material,
                                  pxr::UsdShadeMaterial &usd_material);

void export_texture(bNode *node, const pxr::UsdStageRefPtr stage, bool allow_overwrite = false);

std::string get_node_tex_image_filepath(bNode *node,
                                        const pxr::UsdStageRefPtr stage,
                                        const USDExportParams &export_params);

std::string get_texture_filepath(const std::string &tex_filepath,
                                 const pxr::UsdStageRefPtr stage,
                                 const USDExportParams &export_params);

}  // Namespace blender::io::usd
