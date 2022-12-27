/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "NOD_socket_declarations.hh"
#include "NOD_socket_declarations_geometry.hh"

#include "BKE_node.h"
#include "BKE_node_runtime.hh"

#include "BLI_math_vector.h"

namespace blender::nodes::decl {

/**
 * \note This function only deals with declarations, not the field status of existing nodes. If the
 * field status of existing nodes was stored on the sockets, an improvement would be to check the
 * existing socket's current status instead of the declaration.
 */
static bool field_types_are_compatible(const SocketDeclaration &input,
                                       const SocketDeclaration &output)
{
  if (output.output_field_dependency().field_type() == OutputSocketFieldType::FieldSource) {
    if (input.input_field_type() == InputSocketFieldType::None) {
      return false;
    }
  }
  return true;
}

static bool sockets_can_connect(const SocketDeclaration &socket_decl,
                                const bNodeSocket &other_socket)
{
  /* Input sockets cannot connect to input sockets, outputs cannot connect to outputs. */
  if (socket_decl.in_out() == other_socket.in_out) {
    return false;
  }

  if (other_socket.runtime->declaration) {
    if (socket_decl.in_out() == SOCK_IN) {
      if (!field_types_are_compatible(socket_decl, *other_socket.runtime->declaration)) {
        return false;
      }
    }
    else {
      if (!field_types_are_compatible(*other_socket.runtime->declaration, socket_decl)) {
        return false;
      }
    }
  }

  return true;
}

static bool basic_types_can_connect(const SocketDeclaration & /*socket_decl*/,
                                    const bNodeSocket &other_socket)
{
  return ELEM(other_socket.type, SOCK_FLOAT, SOCK_INT, SOCK_BOOLEAN, SOCK_VECTOR, SOCK_RGBA);
}

static void modify_subtype_except_for_storage(bNodeSocket &socket, int new_subtype)
{
  const char *idname = nodeStaticSocketType(socket.type, new_subtype);
  BLI_strncpy(socket.idname, idname, sizeof(socket.idname));
  bNodeSocketType *socktype = nodeSocketTypeFind(idname);
  socket.typeinfo = socktype;
}

/* -------------------------------------------------------------------- */
/** \name #Float
 * \{ */

bNodeSocket &Float::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddStaticSocket(
      &ntree, &node, in_out_, SOCK_FLOAT, this->subtype, identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  bNodeSocketValueFloat &value = *(bNodeSocketValueFloat *)socket.default_value;
  value.min = this->soft_min_value;
  value.max = this->soft_max_value;
  value.value = this->default_value;
  return socket;
}

bool Float::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_FLOAT) {
    return false;
  }
  if (socket.typeinfo->subtype != this->subtype) {
    return false;
  }
  bNodeSocketValueFloat &value = *(bNodeSocketValueFloat *)socket.default_value;
  if (value.min != this->soft_min_value) {
    return false;
  }
  if (value.max != this->soft_max_value) {
    return false;
  }
  return true;
}

bool Float::can_connect(const bNodeSocket &socket) const
{
  if (!sockets_can_connect(*this, socket)) {
    return false;
  }
  return basic_types_can_connect(*this, socket);
}

bNodeSocket &Float::update_or_build(bNodeTree &ntree, bNode &node, bNodeSocket &socket) const
{
  if (socket.type != SOCK_FLOAT) {
    BLI_assert(socket.in_out == in_out_);
    return this->build(ntree, node);
  }
  if (socket.typeinfo->subtype != this->subtype) {
    modify_subtype_except_for_storage(socket, this->subtype);
  }
  this->set_common_flags(socket);
  bNodeSocketValueFloat &value = *(bNodeSocketValueFloat *)socket.default_value;
  value.min = this->soft_min_value;
  value.max = this->soft_max_value;
  value.subtype = this->subtype;
  return socket;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #Int
 * \{ */

bNodeSocket &Int::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddStaticSocket(
      &ntree, &node, in_out_, SOCK_INT, this->subtype, identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  bNodeSocketValueInt &value = *(bNodeSocketValueInt *)socket.default_value;
  value.min = this->soft_min_value;
  value.max = this->soft_max_value;
  value.value = this->default_value;
  return socket;
}

bool Int::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_INT) {
    return false;
  }
  if (socket.typeinfo->subtype != this->subtype) {
    return false;
  }
  bNodeSocketValueInt &value = *(bNodeSocketValueInt *)socket.default_value;
  if (value.min != this->soft_min_value) {
    return false;
  }
  if (value.max != this->soft_max_value) {
    return false;
  }
  return true;
}

bool Int::can_connect(const bNodeSocket &socket) const
{
  if (!sockets_can_connect(*this, socket)) {
    return false;
  }
  return basic_types_can_connect(*this, socket);
}

bNodeSocket &Int::update_or_build(bNodeTree &ntree, bNode &node, bNodeSocket &socket) const
{
  if (socket.type != SOCK_INT) {
    BLI_assert(socket.in_out == in_out_);
    return this->build(ntree, node);
  }
  if (socket.typeinfo->subtype != this->subtype) {
    modify_subtype_except_for_storage(socket, this->subtype);
  }
  this->set_common_flags(socket);
  bNodeSocketValueInt &value = *(bNodeSocketValueInt *)socket.default_value;
  value.min = this->soft_min_value;
  value.max = this->soft_max_value;
  value.subtype = this->subtype;
  return socket;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #Vector
 * \{ */

bNodeSocket &Vector::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddStaticSocket(
      &ntree, &node, in_out_, SOCK_VECTOR, this->subtype, identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  bNodeSocketValueVector &value = *(bNodeSocketValueVector *)socket.default_value;
  copy_v3_v3(value.value, this->default_value);
  value.min = this->soft_min_value;
  value.max = this->soft_max_value;
  return socket;
}

bool Vector::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_VECTOR) {
    return false;
  }
  if (socket.typeinfo->subtype != this->subtype) {
    return false;
  }
  return true;
}

bool Vector::can_connect(const bNodeSocket &socket) const
{
  if (!sockets_can_connect(*this, socket)) {
    return false;
  }
  return basic_types_can_connect(*this, socket);
}

bNodeSocket &Vector::update_or_build(bNodeTree &ntree, bNode &node, bNodeSocket &socket) const
{
  if (socket.type != SOCK_VECTOR) {
    BLI_assert(socket.in_out == in_out_);
    return this->build(ntree, node);
  }
  if (socket.typeinfo->subtype != this->subtype) {
    modify_subtype_except_for_storage(socket, this->subtype);
  }
  this->set_common_flags(socket);
  bNodeSocketValueVector &value = *(bNodeSocketValueVector *)socket.default_value;
  value.subtype = this->subtype;
  STRNCPY(socket.name, name_.c_str());
  return socket;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #Bool
 * \{ */

bNodeSocket &Bool::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddStaticSocket(
      &ntree, &node, in_out_, SOCK_BOOLEAN, PROP_NONE, identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  bNodeSocketValueBoolean &value = *(bNodeSocketValueBoolean *)socket.default_value;
  value.value = this->default_value;
  return socket;
}

bool Bool::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_BOOLEAN) {
    return false;
  }
  return true;
}

bool Bool::can_connect(const bNodeSocket &socket) const
{
  if (!sockets_can_connect(*this, socket)) {
    return false;
  }
  return basic_types_can_connect(*this, socket);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #Color
 * \{ */

bNodeSocket &Color::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddStaticSocket(
      &ntree, &node, in_out_, SOCK_RGBA, PROP_NONE, identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  bNodeSocketValueRGBA &value = *(bNodeSocketValueRGBA *)socket.default_value;
  copy_v4_v4(value.value, this->default_value);
  return socket;
}

bool Color::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    if (socket.name != name_) {
      return false;
    }
    if (socket.identifier != identifier_) {
      return false;
    }
  }
  if (socket.type != SOCK_RGBA) {
    return false;
  }
  return true;
}

bool Color::can_connect(const bNodeSocket &socket) const
{
  if (!sockets_can_connect(*this, socket)) {
    return false;
  }
  return basic_types_can_connect(*this, socket);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #String
 * \{ */

bNodeSocket &String::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddStaticSocket(
      &ntree, &node, in_out_, SOCK_STRING, PROP_NONE, identifier_.c_str(), name_.c_str());
  STRNCPY(((bNodeSocketValueString *)socket.default_value)->value, this->default_value.c_str());
  this->set_common_flags(socket);
  return socket;
}

bool String::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_STRING) {
    return false;
  }
  return true;
}

bool String::can_connect(const bNodeSocket &socket) const
{
  return sockets_can_connect(*this, socket) && socket.type == SOCK_STRING;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #IDSocketDeclaration
 * \{ */

bNodeSocket &IDSocketDeclaration::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddSocket(
      &ntree, &node, in_out_, this->idname, identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  return socket;
}

bool IDSocketDeclaration::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (!STREQ(socket.idname, this->idname)) {
    return false;
  }
  return true;
}

bool IDSocketDeclaration::can_connect(const bNodeSocket &socket) const
{
  return sockets_can_connect(*this, socket) && STREQ(socket.idname, this->idname);
}

bNodeSocket &IDSocketDeclaration::update_or_build(bNodeTree &ntree,
                                                  bNode &node,
                                                  bNodeSocket &socket) const
{
  if (StringRef(socket.idname) != this->idname) {
    BLI_assert(socket.in_out == in_out_);
    return this->build(ntree, node);
  }
  this->set_common_flags(socket);
  return socket;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #Geometry
 * \{ */

bNodeSocket &Geometry::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddSocket(
      &ntree, &node, in_out_, "NodeSocketGeometry", identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  return socket;
}

bool Geometry::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_GEOMETRY) {
    return false;
  }
  return true;
}

bool Geometry::can_connect(const bNodeSocket &socket) const
{
  return sockets_can_connect(*this, socket) && socket.type == SOCK_GEOMETRY;
}

Span<GeometryComponentType> Geometry::supported_types() const
{
  return supported_types_;
}

bool Geometry::only_realized_data() const
{
  return only_realized_data_;
}

bool Geometry::only_instances() const
{
  return only_instances_;
}

GeometryBuilder &GeometryBuilder::supported_type(GeometryComponentType supported_type)
{
  decl_->supported_types_ = {supported_type};
  return *this;
}

GeometryBuilder &GeometryBuilder::supported_type(
    blender::Vector<GeometryComponentType> supported_types)
{
  decl_->supported_types_ = std::move(supported_types);
  return *this;
}

GeometryBuilder &GeometryBuilder::only_realized_data(bool value)
{
  decl_->only_realized_data_ = value;
  return *this;
}

GeometryBuilder &GeometryBuilder::only_instances(bool value)
{
  decl_->only_instances_ = value;
  return *this;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name #Shader
 * \{ */

bNodeSocket &Shader::build(bNodeTree &ntree, bNode &node) const
{
  bNodeSocket &socket = *nodeAddSocket(
      &ntree, &node, in_out_, "NodeSocketShader", identifier_.c_str(), name_.c_str());
  this->set_common_flags(socket);
  return socket;
}

bool Shader::matches(const bNodeSocket &socket) const
{
  if (!this->matches_common_data(socket)) {
    return false;
  }
  if (socket.type != SOCK_SHADER) {
    return false;
  }
  return true;
}

bool Shader::can_connect(const bNodeSocket &socket) const
{
  if (!sockets_can_connect(*this, socket)) {
    return false;
  }
  /* Basic types can convert to shaders, but not the other way around. */
  if (in_out_ == SOCK_IN) {
    return ELEM(
        socket.type, SOCK_VECTOR, SOCK_RGBA, SOCK_FLOAT, SOCK_INT, SOCK_BOOLEAN, SOCK_SHADER);
  }
  return socket.type == SOCK_SHADER;
}

/** \} */

}  // namespace blender::nodes::decl
