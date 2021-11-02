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
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "node_composite_util.hh"

/* **************** Bright and Contrast  ******************** */

namespace blender::nodes {

static void cmp_node_brightcontrast_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Color>("Image").default_value({1.0f, 1.0f, 1.0f, 1.0f});
  b.add_input<decl::Float>("Bright").min(-100.0f).max(100.0f);
  b.add_input<decl::Float>("Contrast").min(-100.0f).max(100.0f);
  b.add_output<decl::Color>("Image");
}

}  // namespace blender::nodes

static void node_composit_init_brightcontrast(bNodeTree *UNUSED(ntree), bNode *node)
{
  node->custom1 = 1;
}

static int node_composite_gpu_brightcontrast(GPUMaterial *mat,
                                             bNode *node,
                                             bNodeExecData *UNUSED(execdata),
                                             GPUNodeStack *in,
                                             GPUNodeStack *out)
{
  float use_premultiply = node->custom1 ? 1.0f : 0.0f;

  return GPU_stack_link(
      mat, node, "node_composite_bright_contrast", in, out, GPU_constant(&use_premultiply));
}

void register_node_type_cmp_brightcontrast(void)
{
  static bNodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_BRIGHTCONTRAST, "Bright/Contrast", NODE_CLASS_OP_COLOR, 0);
  ntype.declare = blender::nodes::cmp_node_brightcontrast_declare;
  node_type_init(&ntype, node_composit_init_brightcontrast);
  node_type_gpu(&ntype, node_composite_gpu_brightcontrast);

  nodeRegisterType(&ntype);
}
