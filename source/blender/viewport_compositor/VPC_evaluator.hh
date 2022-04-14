/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

#pragma once

#include <memory>

#include "BLI_vector.hh"

#include "DNA_node_types.h"

#include "NOD_derived_node_tree.hh"

#include "VPC_compile_state.hh"
#include "VPC_context.hh"
#include "VPC_gpu_material_operation.hh"
#include "VPC_node_operation.hh"
#include "VPC_operation.hh"

namespace blender::viewport_compositor {

using namespace nodes::derived_node_tree_types;

/* ------------------------------------------------------------------------------------------------
 * Evaluator
 *
 * The evaluator is the main class of the compositor. It is constructed from a compositor node
 * tree and a context. Upon calling the evaluate method, the evaluator will check if the node tree
 * is already compiled into an operations stream, and if it is, it will go over it and evaluate the
 * operations in order. It is then the responsibility of the caller to call the reset method when
 * the node tree changes to invalidate the operations stream. A reset is also required if the
 * resources used by the node tree change in structure, for instances, a change in the dimensions
 * of an image used by the node tree. This is necessary because the evaluator compiles the node
 * tree into an operations stream that is specifically optimized for the structure of the resources
 * used by the node tree.
 *
 * Otherwise, if the node tree is not yet compiled, the evaluator will compile it into an
 * operations stream, evaluating the operations in the process. It should be noted that operations
 * are evaluated as soon as they are compiled, as opposed to compiling the whole operations stream
 * and then evaluating it in a separate step. This is important because, as mentioned before, the
 * operations stream is optimized specifically for the structure of the resources used by the node
 * tree, which is only known after the operations are evaluated. In other words, the evaluator uses
 * the evaluated results of previously compiled operations to compile the operations that follow
 * them in an optimized manner.
 *
 * Compilation starts by computing an optimized node execution schedule by calling the
 * compute_schedule function, see the discussion in VPC_scheduler.hh for more details. For the node
 * tree shown below, the execution schedule is denoted by the node numbers. The compiler then goes
 * over the execution schedule in order and compiles each node into either a Node Operation or a
 * GPU Material Operation, depending on the node type, see the is_gpu_material_node function. A GPU
 * material operation is constructed from a group of nodes forming a contiguous subset of the node
 * execution schedule. For instance, in the node tree shown below, nodes 3 and 4 are compiled
 * together into a GPU material operation and node 5 is compiled into its own GPU material
 * operation, both of which are contiguous subsets of the node execution schedule. This process is
 * described in details in the following section.
 *
 *                             GPU Material 1                     GPU Material 2
 *                   +-----------------------------------+     +------------------+
 * .------------.    |  .------------.  .------------.   |     |  .------------.  |  .------------.
 * |   Node 1   |    |  |   Node 3   |  |   Node 4   |   |     |  |   Node 5   |  |  |   Node 6   |
 * |            |----|--|            |--|            |---|-----|--|            |--|--|            |
 * |            |  .-|--|            |  |            |   |  .--|--|            |  |  |            |
 * '------------'  | |  '------------'  '------------'   |  |  |  '------------'  |  '------------'
 *                 | +-----------------------------------+  |  +------------------+
 * .------------.  |                                        |
 * |   Node 2   |  |                                        |
 * |            |--'----------------------------------------'
 * |            |
 * '------------'
 *
 * For non GPU material nodes, the compilation process is straight forward, the compiler
 * instantiates a node operation from the node, map its inputs to the results of the outputs they
 * are linked to, and evaluates the operations. However, for GPU material nodes, since a group of
 * nodes can be compiled together into a GPU material operation, the compilation process is a bit
 * involved. The compiler uses an instance of the Compile State class to keep track of the
 * compilation process. The compiler state stores the so called "GPU material compile group", which
 * is the current group of nodes that will eventually be compiled together into a GPU material
 * operation. While going over the schedule, the compiler adds the GPU material nodes to the
 * compile group until it decides that the compile group is complete and should be compiled. This
 * is typically decided when the current node is not compatible with the group and can't be added
 * to it, only then it compiles the compile group into a GPU material operation and resets the it
 * to ready it to track the next potential group of nodes that will form a GPU material. This
 * decision is made based on various criteria in the should_compile_gpu_material_compile_group
 * function. See the discussion in VPC_compile_state.hh for more details of those criteria, but
 * perhaps the most evident of which is whether the node is actually a GPU material node, if it
 * isn't, then it evidently can't be added to the group and the group is should be compiled.
 *
 * For the node tree above, the compilation process is as follows. The compiler goes over the
 * node execution schedule in order considering each node. Nodes 1 and 2 are not GPU material
 * operations so they are compiled into node operations and added to the operations stream. The
 * current compile group is empty, so it is not compiled. Node 3 is a GPU material node, and since
 * the compile group is currently empty, it is unconditionally added to it. Node 4 is a GPU
 * material node, it was decided---for the sake of the demonstration---that it is compatible with
 * the compile group and can be added to it. Node 5 is a GPU material node, but it was
 * decided---for the sake of the demonstration---that it is not compatible with the compile group,
 * so the compile group is considered complete and is compiled first, adding the first GPU material
 * operation to the operations stream and resetting the compile group. Node 5 is then added to the
 * now empty compile group similar to node 3. Node 6 is not a GPU material node, so the compile
 * group is considered complete and is compiled first, adding the first GPU material operation to
 * the operations stream and resetting the compile group. Finally, node 6 is compiled into a node
 * operation similar to nodes 1 and 2 and added to the operations stream. */
class Evaluator {
 private:
  /* A reference to the compositor context. */
  Context &context_;
  /* A reference to the compositor node tree. */
  bNodeTree &node_tree_;
  /* The derived and reference node trees representing the compositor node tree. Those are
   * initialized when the node tree is compiled and freed when the evaluator resets. */
  NodeTreeRefMap node_tree_reference_map_;
  std::unique_ptr<DerivedNodeTree> derived_node_tree_;
  /* The compiled operations stream. This contains ordered pointers to the operations that were
   * compiled. This is initialized when the node tree is compiled and freed when the evaluator
   * resets. The is_compiled_ member indicates whether the operation stream can be used or needs to
   * be compiled first. Note that the operations stream can be empty even when compiled, this can
   * happen when the node tree is empty or invalid for instance. */
  Vector<std::unique_ptr<Operation>> operations_stream_;
  /* True if the node tree is already compiled into an operations stream that can be evaluated
   * directly. False if the node tree is not compiled yet and needs to be compiled. */
  bool is_compiled_ = false;

 public:
  /* Construct an evaluator from a compositor node tree and a context. */
  Evaluator(Context &context, bNodeTree &node_tree);

  /* Evaluate the compositor node tree. If the node tree is already compiled into an operations
   * stream, that stream will be evaluated directly. Otherwise, the node tree will be compiled and
   * evaluated. */
  void evaluate();

  /* Invalidate the operations stream that was compiled for the node tree. This should be called
   * when the node tree changes or the structure of any of the resources used by it changes. By
   * structure, we mean things like the dimensions of the used images, while changes to their
   * contents do not necessitate a reset. */
  void reset();

 private:
  /* Compile the given node tree into an operations stream and evaluate it. */
  void compile_and_evaluate();

  /* Compile the given node into a node operation, map each input to the result of the output
   * linked to it, update the compile state, add the newly created operation to the operations
   * stream, and evaluate the operation. */
  void compile_and_evaluate_node(DNode node, CompileState &compile_state);

  /* Map each input of the node operation to the result of the output linked to it. Unlinked inputs
   * are mapped to the result of a newly created Input Single Value Operation, which is added to
   * the operations stream and evaluated. Since this method might add operations to the operations
   * stream, the actual node operation should only be added to the stream once this method is
   * called. */
  void map_node_operation_inputs_to_their_results(DNode node,
                                                  NodeOperation *operation,
                                                  CompileState &compile_state);

  /* Compile the GPU material compile group into a GPU material operation, map each input of the
   * operation to the result of the output linked to it, update the compile state, add the newly
   * created operation to the operations stream, evaluate the operation, and finally reset the GPU
   * material compile group. */
  void compile_and_evaluate_gpu_material_compile_group(CompileState &compile_state);

  /* Map each input of the GPU material operation to the result of the output linked to it. */
  void map_gpu_material_operation_inputs_to_their_results(GPUMaterialOperation *operation,
                                                          CompileState &compile_state);
};

}  // namespace blender::viewport_compositor
