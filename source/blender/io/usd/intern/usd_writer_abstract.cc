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
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */
#include "usd_writer_abstract.h"
#include "usd_hierarchy_iterator.h"
#include "usd_writer_material.h"

#include <pxr/base/tf/stringUtils.h>

#include "BLI_assert.h"

extern "C" {
#include "BKE_anim_data.h"
#include "BKE_key.h"

#include "BLI_utildefines.h"

#include "DNA_modifier_types.h"
}

/* TfToken objects are not cheap to construct, so we do it once. */
namespace usdtokens {
/* Materials */
static const pxr::TfToken diffuse_color("diffuseColor", pxr::TfToken::Immortal);
static const pxr::TfToken metallic("metallic", pxr::TfToken::Immortal);
static const pxr::TfToken preview_shader("previewShader", pxr::TfToken::Immortal);
static const pxr::TfToken preview_surface("UsdPreviewSurface", pxr::TfToken::Immortal);
static const pxr::TfToken roughness("roughness", pxr::TfToken::Immortal);
static const pxr::TfToken surface("surface", pxr::TfToken::Immortal);
static const pxr::TfToken blenderName("userProperties:blenderName", pxr::TfToken::Immortal);
}  // namespace usdtokens

namespace {

template<typename VECT>
bool set_vec_attrib(const pxr::UsdPrim &prim,
                    const IDProperty *prop,
                    const pxr::TfToken &prop_token,
                    const pxr::SdfValueTypeName &type_name,
                    const pxr::UsdTimeCode &timecode)
{
  if (!prim || !prop || !prop->data.pointer || prop_token.IsEmpty() || !type_name) {
    return false;
  }

  pxr::UsdAttribute vec_attr = prim.CreateAttribute(prop_token, type_name, true);

  if (!vec_attr) {
    printf("WARNING: Couldn't USD attribute for array property %s.\n",
           prop_token.GetString().c_str());
    return false;
  }

  VECT vec_value(static_cast<typename VECT::ScalarType *>(prop->data.pointer));

  return vec_attr.Set(vec_value, timecode);
}

}  // anonymous namespace

namespace blender::io::usd {

static void create_vector_attrib(const pxr::UsdPrim &prim,
                                 const IDProperty *prop,
                                 const pxr::TfToken &prop_token,
                                 const pxr::UsdTimeCode &timecode)
{
  if (!prim || !prop || prop_token.IsEmpty()) {
    return;
  }

  if (prop->type != IDP_ARRAY) {
    printf(
        "WARNING: Property %s is not an array type and can't be converted to a vector "
        "attribute.\n",
        prop_token.GetString().c_str());
    return;
  }

  pxr::SdfValueTypeName type_name;
  bool success = false;

  if (prop->subtype == IDP_FLOAT) {
    if (prop->len == 2) {
      type_name = pxr::SdfValueTypeNames->Float2;
      success = set_vec_attrib<pxr::GfVec2f>(prim, prop, prop_token, type_name, timecode);
    }
    else if (prop->len == 3) {
      type_name = pxr::SdfValueTypeNames->Float3;
      success = set_vec_attrib<pxr::GfVec3f>(prim, prop, prop_token, type_name, timecode);
    }
    else if (prop->len == 4) {
      type_name = pxr::SdfValueTypeNames->Float4;
      success = set_vec_attrib<pxr::GfVec4f>(prim, prop, prop_token, type_name, timecode);
    }
  }
  else if (prop->subtype == IDP_DOUBLE) {
    if (prop->len == 2) {
      type_name = pxr::SdfValueTypeNames->Double2;
      success = set_vec_attrib<pxr::GfVec2d>(prim, prop, prop_token, type_name, timecode);
    }
    else if (prop->len == 3) {
      type_name = pxr::SdfValueTypeNames->Double3;
      success = set_vec_attrib<pxr::GfVec3d>(prim, prop, prop_token, type_name, timecode);
    }
    else if (prop->len == 4) {
      type_name = pxr::SdfValueTypeNames->Double4;
      success = set_vec_attrib<pxr::GfVec4d>(prim, prop, prop_token, type_name, timecode);
    }
  }
  else if (prop->subtype == IDP_INT) {
    if (prop->len == 2) {
      type_name = pxr::SdfValueTypeNames->Int2;
      success = set_vec_attrib<pxr::GfVec2i>(prim, prop, prop_token, type_name, timecode);
    }
    else if (prop->len == 3) {
      type_name = pxr::SdfValueTypeNames->Int3;
      success = set_vec_attrib<pxr::GfVec3i>(prim, prop, prop_token, type_name, timecode);
    }
    else if (prop->len == 4) {
      type_name = pxr::SdfValueTypeNames->Int4;
      success = set_vec_attrib<pxr::GfVec4i>(prim, prop, prop_token, type_name, timecode);
    }
  }

  if (!type_name) {
    printf("WARNING: Couldn't determine USD type name for array property %s.\n",
           prop_token.GetString().c_str());
    return;
  }

  if (!success) {
    printf("WARNING: Couldn't set USD attribute from array property %s.\n",
           prop_token.GetString().c_str());
    return;
  }
}

USDAbstractWriter::USDAbstractWriter(const USDExporterContext &usd_export_context)
    : usd_export_context_(usd_export_context), frame_has_been_written_(false), is_animated_(false)
{
}

bool USDAbstractWriter::is_supported(const HierarchyContext * /*context*/) const
{
  return true;
}

pxr::UsdTimeCode USDAbstractWriter::get_export_time_code() const
{
  if (is_animated_) {
    return usd_export_context_.hierarchy_iterator->get_export_time_code();
  }
  /* By using the default timecode USD won't even write a single `timeSample` for non-animated
   * data. Instead, it writes it as non-timesampled. */
  static pxr::UsdTimeCode default_timecode = pxr::UsdTimeCode::Default();
  return default_timecode;
}

void USDAbstractWriter::write(HierarchyContext &context)
{
  if (!frame_has_been_written_) {
    is_animated_ = usd_export_context_.export_params.export_animation &&
                   check_is_animated(context);
  }
  else if (!is_animated_) {
    /* A frame has already been written, and without animation one frame is enough. */
    return;
  }

  do_write(context);

  frame_has_been_written_ = true;
}

bool USDAbstractWriter::check_is_animated(const HierarchyContext &context) const
{
  const Object *object = context.object;

  if (BKE_animdata_id_is_animated(static_cast<ID *>(object->data))) {
    return true;
  }
  if (BKE_key_from_object(object) != nullptr) {
    return true;
  }

  /* Test modifiers. */
  /* TODO(Sybren): replace this with a check on the depsgraph to properly check for dependency on
   * time. */
  ModifierData *md = static_cast<ModifierData *>(object->modifiers.first);
  while (md) {
    if (md->type != eModifierType_Subsurf) {
      return true;
    }
    md = md->next;
  }

  return false;
}

const pxr::SdfPath &USDAbstractWriter::usd_path() const
{
  return usd_export_context_.usd_path;
}

pxr::UsdShadeMaterial USDAbstractWriter::ensure_usd_material(Material *material,
                                                             const HierarchyContext &context)
{
  std::string material_prim_path_str;

  /* For instance prototypes, create the material beneath the prototyp prim. */
  if (usd_export_context_.export_params.use_instancing && !context.is_instance() &&
      usd_export_context_.hierarchy_iterator->is_prototype(context.object)) {

    material_prim_path_str += std::string(usd_export_context_.export_params.root_prim_path);
    if (context.object->data) {
      material_prim_path_str += context.higher_up_export_path;
    }
    else {
      material_prim_path_str += context.export_path;
    }
    material_prim_path_str += "/Looks";
  }

  if (material_prim_path_str.empty()) {
    material_prim_path_str = this->usd_export_context_.export_params.material_prim_path;
  }

  pxr::SdfPath material_library_path(material_prim_path_str);
  pxr::UsdStageRefPtr stage = usd_export_context_.stage;

  /* Construct the material. */
  pxr::TfToken material_name(usd_export_context_.hierarchy_iterator->get_id_name(&material->id));
  pxr::SdfPath usd_path = material_library_path.AppendChild(material_name);
  pxr::UsdShadeMaterial usd_material = pxr::UsdShadeMaterial::Get(stage, usd_path);
  if (usd_material) {
    return usd_material;
  }

  usd_material = (usd_export_context_.export_params.export_as_overs) ?
                     pxr::UsdShadeMaterial(usd_export_context_.stage->OverridePrim(usd_path)) :
                     pxr::UsdShadeMaterial::Define(usd_export_context_.stage, usd_path);

  // TODO(bskinner) maybe always export viewport material as variant...
  if (material->use_nodes && this->usd_export_context_.export_params.generate_cycles_shaders) {
    create_usd_cycles_material(this->usd_export_context_.stage,
                               material,
                               usd_material,
                               this->usd_export_context_.export_params);
  }
  if (material->use_nodes && this->usd_export_context_.export_params.generate_mdl) {
    create_mdl_material(this->usd_export_context_, material, usd_material);
    if (this->usd_export_context_.export_params.export_textures) {
      export_textures(material, this->usd_export_context_.stage);
    }
  }
  if (material->use_nodes && this->usd_export_context_.export_params.generate_preview_surface) {
    create_usd_preview_surface_material(this->usd_export_context_, material, usd_material);
  }
  else {
    create_usd_viewport_material(this->usd_export_context_, material, usd_material);
  }

  if (usd_export_context_.export_params.export_custom_properties && material) {
    auto prim = usd_material.GetPrim();
    write_id_properties(prim, material->id, get_export_time_code());
  }

  return usd_material;
}

void USDAbstractWriter::write_visibility(const HierarchyContext &context,
                                         const pxr::UsdTimeCode timecode,
                                         pxr::UsdGeomImageable &usd_geometry)
{
  pxr::UsdAttribute attr_visibility = usd_geometry.CreateVisibilityAttr(pxr::VtValue(), true);

  const bool is_visible = context.is_object_visible(
      usd_export_context_.export_params.evaluation_mode);
  const pxr::TfToken visibility = is_visible ? pxr::UsdGeomTokens->inherited :
                                               pxr::UsdGeomTokens->invisible;

  usd_value_writer_.SetAttribute(attr_visibility, pxr::VtValue(visibility), timecode);
}

/* Reference the original data instead of writing a copy. */
bool USDAbstractWriter::mark_as_instance(const HierarchyContext &context, const pxr::UsdPrim &prim)
{
  BLI_assert(context.is_instance());

  if (context.export_path == context.original_export_path) {
    printf("USD ref error: export path is reference path: %s\n", context.export_path.c_str());
    BLI_assert_msg(0, "USD reference error");
    return false;
  }

  std::string ref_path_str(usd_export_context_.export_params.root_prim_path);
  ref_path_str += context.original_export_path;

  pxr::SdfPath ref_path(ref_path_str);

  /* To avoid USD errors, make sure the referenced path exists. */
  usd_export_context_.stage->DefinePrim(ref_path);

  if (!prim.GetReferences().AddInternalReference(ref_path)) {
    /* See this URL for a description fo why referencing may fail"
     * https://graphics.pixar.com/usd/docs/api/class_usd_references.html#Usd_Failing_References
     */
    printf("USD Export warning: unable to add reference from %s to %s, not instancing object\n",
           context.export_path.c_str(),
           context.original_export_path.c_str());
    return false;
  }

  prim.SetInstanceable(true);

  return true;
}

void USDAbstractWriter::write_id_properties(pxr::UsdPrim &prim,
                                            const ID &id,
                                            pxr::UsdTimeCode timecode)
{
  if (usd_export_context_.export_params.author_blender_name) {
    if (GS(id.name) == ID_OB) {
      // Author property of original blenderName
      prim.CreateAttribute(pxr::TfToken(usdtokens::blenderName.GetString() + ":object"),
                           pxr::SdfValueTypeNames->String,
                           true)
          .Set<std::string>(std::string(id.name + 2));
    }
    else {
      prim.CreateAttribute(pxr::TfToken(usdtokens::blenderName.GetString() + ":data"),
                           pxr::SdfValueTypeNames->String,
                           true)
          .Set<std::string>(std::string(id.name + 2));
    }
  }

  if (id.properties)
    write_user_properties(prim, (IDProperty *)id.properties, timecode);
}

void USDAbstractWriter::write_user_properties(pxr::UsdPrim &prim,
                                              IDProperty *properties,
                                              pxr::UsdTimeCode timecode)
{
  if (properties == nullptr) {
    return;
  }

  if (properties->type != IDP_GROUP) {
    return;
  }

  IDProperty *prop;
  for (prop = (IDProperty *)properties->data.group.first; prop; prop = prop->next) {
    std::string prop_name = pxr::TfMakeValidIdentifier(prop->name);

    std::string full_prop_name;
    if (usd_export_context_.export_params.add_properties_namespace) {
      full_prop_name = "userProperties:";
    }
    full_prop_name += prop_name;

    pxr::TfToken prop_token = pxr::TfToken(full_prop_name);

    if (prim.HasAttribute(prop_token)) {
      /* Don't overwrite existing attributes, as these may have been
       * created by the exporter logic and shouldn't be changed. */
      continue;
    }

    switch (prop->type) {
      case IDP_INT:
        if (pxr::UsdAttribute int_attr = prim.CreateAttribute(
                prop_token, pxr::SdfValueTypeNames->Int, true)) {
          int_attr.Set<int>(prop->data.val, timecode);
        }
        break;
      case IDP_FLOAT:
        if (pxr::UsdAttribute float_attr = prim.CreateAttribute(
                prop_token, pxr::SdfValueTypeNames->Float, true)) {
          float_attr.Set<float>(*reinterpret_cast<float *>(&prop->data.val), timecode);
        }
        break;
      case IDP_DOUBLE:
        if (pxr::UsdAttribute double_attr = prim.CreateAttribute(
                prop_token, pxr::SdfValueTypeNames->Double, true)) {
          double_attr.Set<double>(*reinterpret_cast<double *>(&prop->data.val), timecode);
        }
        break;
      case IDP_STRING:
        if (pxr::UsdAttribute str_attr = prim.CreateAttribute(
                prop_token, pxr::SdfValueTypeNames->String, true)) {
          str_attr.Set<std::string>(static_cast<const char *>(prop->data.pointer), timecode);
        }
        break;
      case IDP_ARRAY:
        create_vector_attrib(prim, prop, prop_token, timecode);
        break;
    }
  }
}

}  // namespace blender::io::usd
