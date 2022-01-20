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
 * The Original Code is Copyright (C) 2016 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup RNA
 */

#include "DNA_cachefile_types.h"
#include "DNA_scene_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "rna_internal.h"

#ifdef RNA_RUNTIME

#  include "BLI_math.h"
#  include "BLI_string.h"

#  include "BKE_cachefile.h"

#  include "DEG_depsgraph.h"
#  include "DEG_depsgraph_build.h"

#  include "WM_api.h"
#  include "WM_types.h"

#  ifdef WITH_ALEMBIC
#    include "ABC_alembic.h"
#  endif

static void rna_CacheFile_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->data;

  DEG_id_tag_update(&cache_file->id, ID_RECALC_COPY_ON_WRITE);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static void rna_CacheFileLayer_update(Main *UNUSED(bmain), Scene *UNUSED(scene), PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;

  DEG_id_tag_update(&cache_file->id, ID_RECALC_COPY_ON_WRITE);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static void rna_CacheFile_attribute_mapping_update(Main *UNUSED(bmain),
                                                   Scene *UNUSED(scene),
                                                   PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;

  DEG_id_tag_update(&cache_file->id, ID_RECALC_COPY_ON_WRITE);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static void rna_CacheFile_dependency_update(Main *bmain, Scene *scene, PointerRNA *ptr)
{
  rna_CacheFile_update(bmain, scene, ptr);
  DEG_relations_tag_update(bmain);
}

static void rna_CacheFile_object_paths_begin(CollectionPropertyIterator *iter, PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->data;
  rna_iterator_listbase_begin(iter, &cache_file->object_paths, NULL);
}

static PointerRNA rna_CacheFile_active_layer_get(PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  return rna_pointer_inherit_refine(
      ptr, &RNA_CacheFileLayer, BKE_cachefile_get_active_layer(cache_file));
}

static void rna_CacheFile_active_layer_set(PointerRNA *ptr,
                                           PointerRNA value,
                                           struct ReportList *reports)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  int index = BLI_findindex(&cache_file->layers, value.data);
  if (index == -1) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Layer '%s' not found in object '%s'",
                ((CacheFileLayer *)value.data)->filepath,
                cache_file->id.name + 2);
    return;
  }

  cache_file->active_layer = index + 1;
}

static int rna_CacheFile_active_layer_index_get(PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  return cache_file->active_layer - 1;
}

static void rna_CacheFile_active_layer_index_set(PointerRNA *ptr, int value)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  cache_file->active_layer = value + 1;
}

static void rna_CacheFile_active_layer_index_range(
    PointerRNA *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, BLI_listbase_count(&cache_file->layers) - 1);
}

static void rna_CacheFileLayer_hidden_flag_set(PointerRNA *ptr, const bool value)
{
  CacheFileLayer *layer = (CacheFileLayer *)ptr->data;

  if (value) {
    layer->flag |= CACHEFILE_LAYER_HIDDEN;
  }
  else {
    layer->flag &= ~CACHEFILE_LAYER_HIDDEN;
  }
}

static CacheFileLayer *rna_CacheFile_layer_new(CacheFile *cache_file,
                                               bContext *C,
                                               ReportList *reports,
                                               const char *filepath)
{
  CacheFileLayer *layer = BKE_cachefile_add_layer(cache_file, filepath);
  if (layer == NULL) {
    BKE_reportf(
        reports, RPT_ERROR, "Cannot add a layer to CacheFile '%s'", cache_file->id.name + 2);
    return NULL;
  }

  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  BKE_cachefile_reload(depsgraph, cache_file);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
  return layer;
}

static void rna_CacheFile_layer_remove(CacheFile *cache_file, bContext *C, PointerRNA *layer_ptr)
{
  CacheFileLayer *layer = layer_ptr->data;
  BKE_cachefile_remove_layer(cache_file, layer);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  BKE_cachefile_reload(depsgraph, cache_file);
  WM_main_add_notifier(NC_OBJECT | ND_DRAW, NULL);
}

static PointerRNA rna_CacheFile_active_attribute_mapping_get(PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  return rna_pointer_inherit_refine(
      ptr, &RNA_CacheAttributeMapping, BKE_cachefile_get_active_attribute_mapping(cache_file));
}

static void rna_CacheFile_active_attribute_mapping_set(PointerRNA *ptr,
                                                       PointerRNA value,
                                                       struct ReportList *reports)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  int index = BLI_findindex(&cache_file->attribute_mappings, value.data);
  if (index == -1) {
    BKE_reportf(reports,
                RPT_ERROR,
                "Attribute mapping '%s' not found in object '%s'",
                ((CacheAttributeMapping *)value.data)->name,
                cache_file->id.name + 2);
    cache_file->active_attribute_mapping = 0;
    return;
  }

  cache_file->active_attribute_mapping = index + 1;
}

static int rna_CacheFile_active_attribute_mapping_index_get(PointerRNA *ptr)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  return cache_file->active_attribute_mapping - 1;
}

static void rna_CacheFile_active_attribute_mapping_index_set(PointerRNA *ptr, int value)
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;
  cache_file->active_attribute_mapping = value + 1;
}

static void rna_CacheFile_active_attribute_mapping_index_range(
    PointerRNA *ptr, int *min, int *max, int *UNUSED(softmin), int *UNUSED(softmax))
{
  CacheFile *cache_file = (CacheFile *)ptr->owner_id;

  *min = 0;
  *max = max_ii(0, BLI_listbase_count(&cache_file->attribute_mappings) - 1);
}

#else

/* cachefile.object_paths */
static void rna_def_alembic_object_path(BlenderRNA *brna)
{
  StructRNA *srna = RNA_def_struct(brna, "CacheObjectPath", NULL);
  RNA_def_struct_sdna(srna, "CacheObjectPath");
  RNA_def_struct_ui_text(srna, "Object Path", "Path of an object inside of an Alembic archive");
  RNA_def_struct_ui_icon(srna, ICON_NONE);

  RNA_define_lib_overridable(true);

  PropertyRNA *prop = RNA_def_property(srna, "path", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Path", "Object path");
  RNA_def_struct_name_property(srna, prop);

  RNA_define_lib_overridable(false);
}

static void rna_def_cachefile_attribute_mapping(BlenderRNA *brna)
{
  StructRNA *srna = RNA_def_struct(brna, "CacheAttributeMapping", NULL);
  RNA_def_struct_sdna(srna, "CacheAttributeMapping");
  RNA_def_struct_ui_text(
      srna,
      "Cache Attribute Mapping",
      "Attribute Mappin of the cache, used to define how to interpret certain attributes");

  PropertyRNA *prop = RNA_def_property(srna, "name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop, "Name", "Name of the attribute to map");
  RNA_def_property_update(prop, 0, "rna_CacheFile_attribute_mapping_update");

  static const EnumPropertyItem rna_enum_cache_attribute_mapping_items[] = {
      {CACHEFILE_ATTRIBUTE_MAP_NONE, "MAP_NONE", 0, "None", ""},
      {CACHEFILE_ATTRIBUTE_MAP_TO_UVS,
       "MAP_TO_UVS",
       0,
       "UVs",
       "Read the attribute as a UV map of the same name"},
      {CACHEFILE_ATTRIBUTE_MAP_TO_VERTEX_COLORS,
       "MAP_TO_VERTEX_COLORS",
       0,
       "Vertex Colors",
       "Read the attribute as a vertex color layer of the same name"},
      {CACHEFILE_ATTRIBUTE_MAP_TO_WEIGHT_GROUPS,
       "MAP_TO_WEIGHT_GROUPS",
       0,
       "Weight Group",
       "Read the attribute as a weight group channel of the same name"},
      {CACHEFILE_ATTRIBUTE_MAP_TO_FLOAT2,
       "MAP_TO_FLOAT2",
       0,
       "2D Vector",
       "Interpret the attribute's data as generic 2D vectors"},
      {CACHEFILE_ATTRIBUTE_MAP_TO_FLOAT3,
       "MAP_TO_FLOAT3",
       0,
       "3D Vector",
       "Interpret the attribute's data as generic 3D vectors"},
      {CACHEFILE_ATTRIBUTE_MAP_TO_COLOR,
       "MAP_TO_COLOR",
       0,
       "Color",
       "Interpret the attribute's data as colors (RGBA)"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "mapping", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_cache_attribute_mapping_items);
  RNA_def_property_update(prop, 0, "rna_CacheFile_attribute_mapping_update");
  RNA_def_property_ui_text(prop, "Data Type", "Define the data type of the attribute");

  static const EnumPropertyItem rna_enum_cache_attribute_domain_items[] = {
      {CACHEFILE_ATTR_MAP_DOMAIN_AUTO,
       "AUTO",
       0,
       "Automatic",
       "Try to automatically determine the domain of the attribute"},
      {CACHEFILE_ATTR_MAP_DOMAIN_POINT,
       "POINT",
       0,
       "Point",
       "The attribute is defined on the points"},
      {CACHEFILE_ATTR_MAP_DOMAIN_FACE_CORNER,
       "FACE_CORNER",
       0,
       "Face Corner",
       "The attribute is defined on the face corners"},
      {CACHEFILE_ATTR_MAP_DOMAIN_FACE, "FACE", 0, "Face", "The attribute is defined on the faces"},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "domain", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_cache_attribute_domain_items);
  RNA_def_property_update(prop, 0, "rna_CacheFile_attribute_mapping_update");
  RNA_def_property_ui_text(prop, "Domain", "Define the domain on which the attribute is written");
}

static void rna_def_cachefile_attribute_mappings(BlenderRNA *brna, PropertyRNA *cprop)
{
  RNA_def_property_srna(cprop, "CacheAttributeMappings");
  StructRNA *srna = RNA_def_struct(brna, "CacheAttributeMappings", NULL);
  RNA_def_struct_sdna(srna, "CacheFile");
  RNA_def_struct_ui_text(
      srna, "Cache Attribute Mappings", "Collection of cache attribute mappings");

  PropertyRNA *prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "CacheAttributeMapping");
  RNA_def_property_pointer_funcs(prop,
                                 "rna_CacheFile_active_attribute_mapping_get",
                                 "rna_CacheFile_active_attribute_mapping_set",
                                 NULL,
                                 NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop, "Active Attribute Mapping", "Active attribute mapping of the CacheFile");
  RNA_def_property_ui_text(
      prop, "Active Attribute Mapping Index", "Active index in attribute mappings array");
}

/* cachefile.object_paths */
static void rna_def_cachefile_object_paths(BlenderRNA *brna, PropertyRNA *cprop)
{
  RNA_def_property_srna(cprop, "CacheObjectPaths");
  StructRNA *srna = RNA_def_struct(brna, "CacheObjectPaths", NULL);
  RNA_def_struct_sdna(srna, "CacheFile");
  RNA_def_struct_ui_text(srna, "Object Paths", "Collection of object paths");
}

static void rna_def_cachefile_layer(BlenderRNA *brna)
{
  StructRNA *srna = RNA_def_struct(brna, "CacheFileLayer", NULL);
  RNA_def_struct_sdna(srna, "CacheFileLayer");
  RNA_def_struct_ui_text(
      srna,
      "Cache Layer",
      "Layer of the cache, used to load or override data from the first the first layer");

  PropertyRNA *prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_FILEPATH);
  RNA_def_property_ui_text(prop, "File Path", "Path to the archive");
  RNA_def_property_update(prop, 0, "rna_CacheFileLayer_update");

  prop = RNA_def_property(srna, "hide_layer", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", CACHEFILE_LAYER_HIDDEN);
  RNA_def_property_boolean_funcs(prop, NULL, "rna_CacheFileLayer_hidden_flag_set");
  RNA_def_property_ui_icon(prop, ICON_HIDE_OFF, -1);
  RNA_def_property_ui_text(prop, "Hide Layer", "Do not load data from this layer");
  RNA_def_property_update(prop, 0, "rna_CacheFileLayer_update");
}

static void rna_def_cachefile_layers(BlenderRNA *brna, PropertyRNA *cprop)
{
  RNA_def_property_srna(cprop, "CacheFileLayers");
  StructRNA *srna = RNA_def_struct(brna, "CacheFileLayers", NULL);
  RNA_def_struct_sdna(srna, "CacheFile");
  RNA_def_struct_ui_text(srna, "Cache Layers", "Collection of cache layers");

  PropertyRNA *prop = RNA_def_property(srna, "active", PROP_POINTER, PROP_NONE);
  RNA_def_property_struct_type(prop, "CacheFileLayer");
  RNA_def_property_pointer_funcs(
      prop, "rna_CacheFile_active_layer_get", "rna_CacheFile_active_layer_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Active Layer", "Active layer of the CacheFile");

  /* Add a layer. */
  FunctionRNA *func = RNA_def_function(srna, "new", "rna_CacheFile_layer_new");
  RNA_def_function_flag(func, FUNC_USE_REPORTS | FUNC_USE_CONTEXT);
  RNA_def_function_ui_description(func, "Add a new layer");
  PropertyRNA *parm = RNA_def_string(
      func, "filepath", "File Path", 0, "", "File path to the archive used as a layer");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  /* Return type. */
  parm = RNA_def_pointer(func, "layer", "CacheFileLayer", "", "Newly created layer");
  RNA_def_function_return(func, parm);

  /* Remove a layer. */
  func = RNA_def_function(srna, "remove", "rna_CacheFile_layer_remove");
  RNA_def_function_flag(func, FUNC_USE_CONTEXT);
  RNA_def_function_ui_description(func, "Remove an existing layer from the cache file");
  parm = RNA_def_pointer(func, "layer", "CacheFileLayer", "", "Layer to remove");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED | PARM_RNAPTR);
  RNA_def_parameter_clear_flags(parm, PROP_THICK_WRAP, 0);
}

static void rna_def_cachefile(BlenderRNA *brna)
{
  StructRNA *srna = RNA_def_struct(brna, "CacheFile", "ID");
  RNA_def_struct_sdna(srna, "CacheFile");
  RNA_def_struct_ui_text(srna, "CacheFile", "");
  RNA_def_struct_ui_icon(srna, ICON_FILE);

  RNA_define_lib_overridable(true);

  PropertyRNA *prop = RNA_def_property(srna, "filepath", PROP_STRING, PROP_FILEPATH);
  RNA_def_property_ui_text(prop, "File Path", "Path to external displacements file");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "is_sequence", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(
      prop, "Sequence", "Whether the cache is separated in a series of files");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "use_render_procedural", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(
      prop,
      "Use Render Engine Procedural",
      "Display boxes in the viewport as placeholders for the objects, Cycles will use a "
      "procedural to load the objects during viewport rendering in experimental mode, "
      "other render engines will also receive a placeholder and should take care of loading the "
      "Alembic data themselves if possible");
  RNA_def_property_update(prop, 0, "rna_CacheFile_dependency_update");

  static const EnumPropertyItem cache_file_type_items[] = {
      {CACHE_FILE_TYPE_INVALID, "INVALID", 0, "Invalid", ""},
      {CACHEFILE_TYPE_ALEMBIC, "ALEMBIC", 0, "Alembic", ""},
      {CACHEFILE_TYPE_USD, "USD", 0, "USD", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, cache_file_type_items);
  RNA_def_property_ui_text(prop, "Type", "Type of the file used for storing data");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  /* ----------------- For Scene time ------------------- */

  prop = RNA_def_property(srna, "override_frame", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(prop,
                           "Override Frame",
                           "Whether to use a custom frame for looking up data in the cache file,"
                           " instead of using the current scene frame");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "frame", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "frame");
  RNA_def_property_range(prop, -MAXFRAME, MAXFRAME);
  RNA_def_property_ui_text(prop,
                           "Frame",
                           "The time to use for looking up the data in the cache file,"
                           " or to determine which file to use in a file sequence");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "frame_offset", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "frame_offset");
  RNA_def_property_range(prop, -MAXFRAME, MAXFRAME);
  RNA_def_property_ui_text(prop,
                           "Frame Offset",
                           "Subtracted from the current frame to use for "
                           "looking up the data in the cache file, or to "
                           "determine which file to use in a file sequence");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  /* ----------------- Cache controls ----------------- */

  prop = RNA_def_property(srna, "use_prefetch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_ui_text(
      prop,
      "Use Prefetch",
      "When enabled, the Cycles Procedural will preload animation data for faster updates");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "prefetch_cache_size", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_ui_text(
      prop,
      "Prefetch Cache Size",
      "Memory usage limit in megabytes for the Cycles Procedural cache, if the data does not "
      "fit within the limit, rendering is aborted");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  /* ----------------- Axis Conversion ----------------- */

  prop = RNA_def_property(srna, "forward_axis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "forward_axis");
  RNA_def_property_enum_items(prop, rna_enum_object_axis_items);
  RNA_def_property_ui_text(prop, "Forward", "");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "up_axis", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "up_axis");
  RNA_def_property_enum_items(prop, rna_enum_object_axis_items);
  RNA_def_property_ui_text(prop, "Up", "");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  prop = RNA_def_property(srna, "scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_sdna(prop, NULL, "scale");
  RNA_def_property_range(prop, 0.0001f, 1000.0f);
  RNA_def_property_ui_text(
      prop,
      "Scale",
      "Value by which to enlarge or shrink the object with respect to the world's origin"
      " (only applicable through a Transform Cache constraint)");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");

  /* object paths */
  prop = RNA_def_property(srna, "object_paths", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "object_paths", NULL);
  RNA_def_property_collection_funcs(prop,
                                    "rna_CacheFile_object_paths_begin",
                                    "rna_iterator_listbase_next",
                                    "rna_iterator_listbase_end",
                                    "rna_iterator_listbase_get",
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  RNA_def_property_struct_type(prop, "CacheObjectPath");
  RNA_def_property_srna(prop, "CacheObjectPaths");
  RNA_def_property_ui_text(
      prop, "Object Paths", "Paths of the objects inside the Alembic archive");

  /* ----------------- Alembic Velocity Attribute ----------------- */

  prop = RNA_def_property(srna, "velocity_name", PROP_STRING, PROP_NONE);
  RNA_def_property_ui_text(prop,
                           "Velocity Attribute",
                           "Name of the Alembic attribute used for generating motion blur data");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  static const EnumPropertyItem velocity_unit_items[] = {
      {CACHEFILE_VELOCITY_UNIT_SECOND, "SECOND", 0, "Second", ""},
      {CACHEFILE_VELOCITY_UNIT_FRAME, "FRAME", 0, "Frame", ""},
      {0, NULL, 0, NULL, NULL},
  };

  prop = RNA_def_property(srna, "velocity_unit", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_sdna(prop, NULL, "velocity_unit");
  RNA_def_property_enum_items(prop, velocity_unit_items);
  RNA_def_property_ui_text(
      prop,
      "Velocity Unit",
      "Define how the velocity vectors are interpreted with regard to time, 'frame' means "
      "the delta time is 1 frame, 'second' means the delta time is 1 / FPS");
  RNA_def_property_update(prop, 0, "rna_CacheFile_update");
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);

  /* ----------------- Alembic Layers ----------------- */

  prop = RNA_def_property(srna, "layers", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "layers", NULL);
  RNA_def_property_struct_type(prop, "CacheFileLayer");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Cache Layers", "Layers of the cache");
  rna_def_cachefile_layers(brna, prop);

  prop = RNA_def_property(srna, "active_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_int_sdna(prop, NULL, "active_layer");
  RNA_def_property_int_funcs(prop,
                             "rna_CacheFile_active_layer_index_get",
                             "rna_CacheFile_active_layer_index_set",
                             "rna_CacheFile_active_layer_index_range");

  /* ----------------- Attribute Mappings ----------------- */

  prop = RNA_def_property(srna, "attribute_mappings", PROP_COLLECTION, PROP_NONE);
  RNA_def_property_collection_sdna(prop, NULL, "attribute_mappings", NULL);
  RNA_def_property_struct_type(prop, "CacheAttributeMapping");
  RNA_def_property_override_clear_flag(prop, PROPOVERRIDE_OVERRIDABLE_LIBRARY);
  RNA_def_property_ui_text(prop, "Cache Attribute Mappings", "Attribute mappings of the cache");
  rna_def_cachefile_attribute_mappings(brna, prop);

  prop = RNA_def_property(srna, "active_attribute_mapping_index", PROP_INT, PROP_UNSIGNED);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_int_sdna(prop, NULL, "active_attribute_mapping");
  RNA_def_property_int_funcs(prop,
                             "rna_CacheFile_active_attribute_mapping_index_get",
                             "rna_CacheFile_active_attribute_mapping_index_set",
                             "rna_CacheFile_active_attribute_mapping_index_range");

  RNA_define_lib_overridable(false);

  rna_def_cachefile_object_paths(brna, prop);

  rna_def_animdata_common(srna);
}

void RNA_def_cachefile(BlenderRNA *brna)
{
  rna_def_cachefile(brna);
  rna_def_alembic_object_path(brna);
  rna_def_cachefile_layer(brna);
  rna_def_cachefile_attribute_mapping(brna);
}

#endif
