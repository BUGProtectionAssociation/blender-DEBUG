/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2022 Blender Foundation. All rights reserved. */

#pragma once

#include "BLI_string_ref.hh"

#include "NOD_derived_node_tree.hh"

#include "VPC_context.hh"
#include "VPC_operation.hh"
#include "VPC_result.hh"

namespace blender::viewport_compositor {

using namespace nodes::derived_node_tree_types;

/* An input single value operation is an operation that outputs a single value result whose value
 * is the value of an unlinked input socket. This is typically used to initialize the values of
 * unlinked node input sockets. */
class InputSingleValueOperation : public Operation {
 private:
  /* The identifier of the output. */
  static const StringRef output_identifier_;
  /* The input socket whose value the operation will set to its result. */
  DInputSocket input_socket_;

 public:
  InputSingleValueOperation(Context &context, DInputSocket input_socket);

  /* Allocate a single value result and set its value to the default value of the input socket. */
  void execute() override;

  /* Get a reference to the output result of the operation, this essentially calls the super
   * get_result with the output identifier of the operation. */
  Result &get_result();

 private:
  /* Populate the result of the operation, this essentially calls the super populate_result method
   * with the output identifier of the operation. */
  void populate_result(Result result);
};

}  // namespace blender::viewport_compositor
