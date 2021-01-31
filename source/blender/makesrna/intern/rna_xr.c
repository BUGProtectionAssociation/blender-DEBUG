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

/** \file
 * \ingroup RNA
 */

#include "BLI_math.h"

#include "DNA_space_types.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"
#include "DNA_xr_types.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "WM_types.h"

#include "rna_internal.h"

#ifdef RNA_RUNTIME

#  include "WM_api.h"

#  ifdef WITH_XR_OPENXR

static wmXrData *rna_XrSession_wm_xr_data_get(PointerRNA *ptr)
{
  /* Callers could also get XrSessionState pointer through ptr->data, but prefer if we just
   * consistently pass wmXrData pointers to the WM_xr_xxx() API. */

  BLI_assert((ptr->type == &RNA_XrSessionSettings) || (ptr->type == &RNA_XrSessionState));

  wmWindowManager *wm = (wmWindowManager *)ptr->owner_id;
  BLI_assert(wm && (GS(wm->id.name) == ID_WM));

  return &wm->xr;
}
#  endif

static bool rna_XrSessionSettings_use_positional_tracking_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.flag & XR_SESSION_USE_POSITION_TRACKING) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_use_positional_tracking_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.flag |= XR_SESSION_USE_POSITION_TRACKING;
  }
  else {
    xr->session_settings.flag &= (~XR_SESSION_USE_POSITION_TRACKING);
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_use_absolute_tracking_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.flag & XR_SESSION_USE_ABSOLUTE_TRACKING) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_use_absolute_tracking_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.flag |= XR_SESSION_USE_ABSOLUTE_TRACKING;
  }
  else {
    xr->session_settings.flag &= (~XR_SESSION_USE_ABSOLUTE_TRACKING);
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static void rna_XrSessionSettings_headset_object_set(PointerRNA *ptr,
                                                     PointerRNA value,
                                                     struct ReportList *UNUSED(reports))
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  Object *ob = value.data;

  if (xr->session_settings.headset_object) {
    /* Restore previous object's original pose. */
    WM_xr_session_state_viewer_object_get(xr, xr->session_settings.headset_object);
  }
  if (ob) {
    /* Store new object's original pose. */
    WM_xr_session_state_viewer_object_set(xr, ob);
  }

  xr->session_settings.headset_object = ob;
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_headset_object_enable_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.headset_flag & XR_OBJECT_ENABLE) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_headset_object_enable_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.headset_flag |= XR_OBJECT_ENABLE;
  }
  else {
    xr->session_settings.headset_flag &= (~XR_OBJECT_ENABLE);
  }

  /* Store/restore object's original pose. */
  Object *ob = xr->session_settings.headset_object;
  if (ob) {
    if (value) {
      WM_xr_session_state_viewer_object_set(xr, ob);
    }
    else {
      WM_xr_session_state_viewer_object_get(xr, ob);
    }
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_headset_object_autokey_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.headset_flag & XR_OBJECT_AUTOKEY) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_headset_object_autokey_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.headset_flag |= XR_OBJECT_AUTOKEY;
  }
  else {
    xr->session_settings.headset_flag &= (~XR_OBJECT_AUTOKEY);
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static void rna_XrSessionSettings_controller0_object_set(PointerRNA *ptr,
                                                         PointerRNA value,
                                                         struct ReportList *UNUSED(reports))
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  Object *ob = value.data;

  if (xr->session_settings.controller0_object) {
    /* Restore previous object's original pose. */
    WM_xr_session_state_controller_object_get(xr, 0, xr->session_settings.controller0_object);
  }
  if (ob) {
    /* Store new object's original pose. */
    WM_xr_session_state_controller_object_set(xr, 0, ob);
  }

  xr->session_settings.controller0_object = ob;
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_controller0_object_enable_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.controller0_flag & XR_OBJECT_ENABLE) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_controller0_object_enable_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.controller0_flag |= XR_OBJECT_ENABLE;
  }
  else {
    xr->session_settings.controller0_flag &= (~XR_OBJECT_ENABLE);
  }

  /* Store/restore object's original pose. */
  Object *ob = xr->session_settings.controller0_object;
  if (ob) {
    if (value) {
      WM_xr_session_state_controller_object_set(xr, 0, ob);
    }
    else {
      WM_xr_session_state_controller_object_get(xr, 0, ob);
    }
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_controller0_object_autokey_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.controller0_flag & XR_OBJECT_AUTOKEY) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_controller0_object_autokey_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.controller0_flag |= XR_OBJECT_AUTOKEY;
  }
  else {
    xr->session_settings.controller0_flag &= (~XR_OBJECT_AUTOKEY);
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static void rna_XrSessionSettings_controller1_object_set(PointerRNA *ptr,
                                                         PointerRNA value,
                                                         struct ReportList *UNUSED(reports))
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  Object *ob = value.data;

  if (xr->session_settings.controller1_object) {
    /* Restore previous object's original pose. */
    WM_xr_session_state_controller_object_get(xr, 1, xr->session_settings.controller1_object);
  }
  if (ob) {
    /* Store new object's original pose. */
    WM_xr_session_state_controller_object_set(xr, 1, ob);
  }

  xr->session_settings.controller1_object = ob;
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_controller1_object_enable_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.controller1_flag & XR_OBJECT_ENABLE) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_controller1_object_enable_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.controller1_flag |= XR_OBJECT_ENABLE;
  }
  else {
    xr->session_settings.controller1_flag &= (~XR_OBJECT_ENABLE);
  }

  /* Store/restore object's original pose. */
  Object *ob = xr->session_settings.controller1_object;
  if (ob) {
    if (value) {
      WM_xr_session_state_controller_object_set(xr, 1, ob);
    }
    else {
      WM_xr_session_state_controller_object_get(xr, 1, ob);
    }
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionSettings_controller1_object_autokey_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  return (xr->session_settings.controller1_flag & XR_OBJECT_AUTOKEY) != 0;
#  else
  UNUSED_VARS(ptr);
  return false;
#  endif
}

static void rna_XrSessionSettings_controller1_object_autokey_set(PointerRNA *ptr, bool value)
{
#  ifdef WITH_XR_OPENXR
  wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  if (value) {
    xr->session_settings.controller1_flag |= XR_OBJECT_AUTOKEY;
  }
  else {
    xr->session_settings.controller1_flag &= (~XR_OBJECT_AUTOKEY);
  }
#  else
  UNUSED_VARS(ptr, value);
#  endif
}

static bool rna_XrSessionState_is_running(bContext *C)
{
#  ifdef WITH_XR_OPENXR
  const wmWindowManager *wm = CTX_wm_manager(C);
  return WM_xr_session_exists(&wm->xr);
#  else
  UNUSED_VARS(C);
  return false;
#  endif
}

static void rna_XrSessionState_reset_to_base_pose(bContext *C)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  WM_xr_session_base_pose_reset(&wm->xr);
#  else
  UNUSED_VARS(C);
#  endif
}

static bool rna_XrSessionState_action_set_create(bContext *C, const char *action_set_name)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  return WM_xr_action_set_create(&wm->xr, action_set_name);
#  else
  UNUSED_VARS(C, name);
  return false;
#  endif
}

static bool rna_XrSessionState_action_create(bContext *C,
                                             const char *action_set_name,
                                             const char *action_name,
                                             int type,
                                             const char *user_path0,
                                             const char *user_path1,
                                             float threshold,
                                             const char *op,
                                             int op_flag)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  unsigned int count_subaction_paths = 0;
  const char *subaction_paths[2];

  if (user_path0 && !STREQ(user_path0, "")) {
    subaction_paths[0] = user_path0;
    ++count_subaction_paths;

    if (user_path1 && !STREQ(user_path1, "")) {
      subaction_paths[1] = user_path1;
      ++count_subaction_paths;
    }
  }
  else {
    if (user_path1 && !STREQ(user_path1, "")) {
      subaction_paths[0] = user_path1;
      ++count_subaction_paths;
    }
    else {
      return false;
    }
  }

  wmOperatorType *ot = NULL;
  IDProperty *op_properties = NULL;

  if (op[0] && (type == XR_FLOAT_INPUT || type == XR_VECTOR2F_INPUT)) {
    char idname[OP_MAX_TYPENAME];
    WM_operator_bl_idname(idname, op);
    ot = WM_operatortype_find(idname, true);
    if (ot) {
      /* Get properties from add-on key map for XR session. */
      wmKeyMap *km = WM_keymap_list_find(
          &wm->addonconf->keymaps, "XR Session", SPACE_EMPTY, RGN_TYPE_XR);
      if (km) {
        wmKeyMapItem *kmi = WM_keymap_item_find_xr(km, action_set_name, action_name);
        if (kmi && STREQ(kmi->idname, idname)) {
          op_properties = kmi->properties;
        }
      }
    }
  }

  return WM_xr_action_create(&wm->xr,
                             action_set_name,
                             action_name,
                             type,
                             count_subaction_paths,
                             subaction_paths,
                             threshold,
                             ot,
                             op_properties,
                             op_flag);
#  else
  UNUSED_VARS(C, action_set_name, name, type, user_path0, user_path1, threshold, op, op_flag);
  return false;
#  endif
}

bool rna_XrSessionState_action_space_create(bContext *C,
                                            const char *action_set_name,
                                            const char *action_name,
                                            const char *user_path0,
                                            const char *user_path1,
                                            float location[3],
                                            float rotation[3])
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  unsigned int count_subaction_paths = 0;
  const char *subaction_paths[2];

  if (user_path0 && !STREQ(user_path0, "")) {
    subaction_paths[0] = user_path0;
    ++count_subaction_paths;

    if (user_path1 && !STREQ(user_path1, "")) {
      subaction_paths[1] = user_path1;
      ++count_subaction_paths;
    }
  }
  else {
    if (user_path1 && !STREQ(user_path1, "")) {
      subaction_paths[0] = user_path1;
      ++count_subaction_paths;
    }
    else {
      return false;
    }
  }

  float poses[2][7];
  copy_v3_v3(poses[0], location);
  eul_to_quat(&poses[0][3], rotation);
  normalize_qt(&poses[0][3]);
  memcpy(poses[1], poses[0], sizeof(float[7]));

  return WM_xr_action_space_create(
      &wm->xr, action_set_name, action_name, count_subaction_paths, subaction_paths, poses);
#  else
  UNUSED_VARS(C, action_set_name, action_name, user_path0, user_path1, location, rotation);
  return false;
#  endif
}

bool rna_XrSessionState_action_binding_create(bContext *C,
                                              const char *action_set_name,
                                              const char *profile,
                                              const char *action_name,
                                              const char *interaction_path0,
                                              const char *interaction_path1)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  unsigned int count_interaction_paths = 0;
  const char *interaction_paths[2];

  if (interaction_path0 && !STREQ(interaction_path0, "")) {
    interaction_paths[0] = interaction_path0;
    ++count_interaction_paths;

    if (interaction_path1 && !STREQ(interaction_path1, "")) {
      interaction_paths[1] = interaction_path1;
      ++count_interaction_paths;
    }
  }
  else {
    if (interaction_path1 && !STREQ(interaction_path1, "")) {
      interaction_paths[0] = interaction_path1;
      ++count_interaction_paths;
    }
    else {
      return false;
    }
  }

  return WM_xr_action_binding_create(
      &wm->xr, action_set_name, profile, action_name, count_interaction_paths, interaction_paths);
#  else
  UNUSED_VARS(C, action_set_name, profile, action_name, interaction_path0, interaction_path1);
  return false;
#  endif
}

bool rna_XrSessionState_active_action_set_set(bContext *C, const char *action_set_name)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  return WM_xr_active_action_set_set(&wm->xr, action_set_name);
#  else
  UNUSED_VARS(C, action_set_name);
  return false;
#  endif
}

bool rna_XrSessionState_controller_pose_action_set(bContext *C,
                                                   const char *action_set_name,
                                                   const char *action_name)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  return WM_xr_controller_pose_action_set(&wm->xr, action_set_name, action_name);
#  else
  UNUSED_VARS(C, action_set_name, action_name);
  return false;
#  endif
}

void rna_XrSessionState_action_state_get(bContext *C,
                                         const char *action_set_name,
                                         const char *action_name,
                                         const char *user_path,
                                         float *r_state)
{
  *r_state = 0.0f;
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  if (!WM_xr_action_state_get(
          &wm->xr, action_set_name, action_name, XR_FLOAT_INPUT, user_path, r_state)) {
    *r_state = 0.0f;
  }
#  else
  UNUSED_VARS(C, action_set_name, action_name, user_path);
#  endif
}

void rna_XrSessionState_pose_action_state_get(bContext *C,
                                              const char *action_set_name,
                                              const char *action_name,
                                              const char *user_path,
                                              float r_state[7])
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  if (!WM_xr_action_state_get(
          &wm->xr, action_set_name, action_name, XR_POSE_INPUT, user_path, r_state)) {
    zero_v3(r_state);
    unit_qt(&r_state[3]);
    return;
  }
#  else
  UNUSED_VARS(C, action_set_name, action_name, user_path);
  zero_v3(r_state);
  unit_qt(&r_state[3]);
#  endif
}

bool rna_XrSessionState_haptic_action_apply(bContext *C,
                                            const char *action_set_name,
                                            const char *action_name,
                                            const char *user_path0,
                                            const char *user_path1,
                                            float duration,
                                            float frequency,
                                            float amplitude)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);

  const char *subaction_paths[2];
  unsigned int count = 0;
  if (user_path0 && !STREQ(user_path0, "")) {
    subaction_paths[0] = user_path0;
    ++count;

    if (user_path1 && !STREQ(user_path1, "")) {
      subaction_paths[1] = user_path1;
      ++count;
    }
  }
  else {
    if (user_path1 && !STREQ(user_path1, "")) {
      subaction_paths[0] = user_path1;
      ++count;
    }
    else {
      return false;
    }
  }

  long long duration_msec = (long long)(duration * 1000.0f);

  return WM_xr_haptic_action_apply(&wm->xr,
                                   action_set_name,
                                   action_name,
                                   count,
                                   subaction_paths,
                                   &duration_msec,
                                   &frequency,
                                   &amplitude);
#  else
  UNUSED_VARS(
      C, action_set_name, action_name, user_path0, user_path1, duration, frequency, amplitude);
  return false;
#  endif
}

static void rna_XrSessionState_viewer_pose_location_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  WM_xr_session_state_viewer_pose_location_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void rna_XrSessionState_viewer_pose_rotation_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  WM_xr_session_state_viewer_pose_rotation_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static void rna_XrSessionState_controller_pose0_location_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  WM_xr_session_state_controller_pose_location_get(xr, 0, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void rna_XrSessionState_controller_pose0_rotation_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  WM_xr_session_state_controller_pose_rotation_get(xr, 0, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static void rna_XrSessionState_controller_pose1_location_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  WM_xr_session_state_controller_pose_location_get(xr, 1, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void rna_XrSessionState_controller_pose1_rotation_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSession_wm_xr_data_get(ptr);
  WM_xr_session_state_controller_pose_rotation_get(xr, 1, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

#else /* RNA_RUNTIME */

static void rna_def_xr_session_settings(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem base_pose_types[] = {
      {XR_BASE_POSE_SCENE_CAMERA,
       "SCENE_CAMERA",
       0,
       "Scene Camera",
       "Follow the active scene camera to define the VR view's base pose"},
      {XR_BASE_POSE_OBJECT,
       "OBJECT",
       0,
       "Object",
       "Follow the transformation of an object to define the VR view's base pose"},
      {XR_BASE_POSE_CUSTOM,
       "CUSTOM",
       0,
       "Custom",
       "Follow a custom transformation to define the VR view's base pose"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem controller_draw_styles[] = {
      {XR_CONTROLLER_DRAW_AXES, "AXES", 0, "Axes", "Draw controller's local axes"},
      {XR_CONTROLLER_DRAW_RAY, "RAY", 0, "Ray", "Draw a ray along controller's -Z axis"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem selection_eyes[] = {
      {XR_EYE_LEFT, "EYE_LEFT", 0, "Left Eye", "Use the left eye's perspective for VR selection"},
      {XR_EYE_RIGHT,
       "EYE_RIGHT",
       0,
       "Right Eye",
       "Use the right eye's perspective for VR selection"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "XrSessionSettings", NULL);
  RNA_def_struct_ui_text(srna, "XR Session Settings", "");

  prop = RNA_def_property(srna, "shading", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_ui_text(prop, "Shading Settings", "");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "base_pose_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, base_pose_types);
  RNA_def_property_ui_text(
      prop,
      "Base Pose Type",
      "Define where the location and rotation for the VR view come from, to which "
      "translation and rotation deltas from the VR headset will be applied to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Base Pose Object",
                           "Object to take the location and rotation to which translation and "
                           "rotation deltas from the VR headset will be applied to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_ui_text(prop,
                           "Base Pose Location",
                           "Coordinates to apply translation deltas from the VR headset to");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_angle", PROP_FLOAT, PROP_AXISANGLE);
  RNA_def_property_ui_text(
      prop,
      "Base Pose Angle",
      "Rotation angle around the Z-Axis to apply the rotation deltas from the VR headset to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_floor", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_GRIDFLOOR);
  RNA_def_property_ui_text(prop, "Display Grid Floor", "Show the ground plane grid");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_annotation", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_ANNOTATION);
  RNA_def_property_ui_text(prop, "Show Annotation", "Show annotations for this view");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_selection", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_SELECTION);
  RNA_def_property_ui_text(prop, "Show Selection", "Show selection outlines");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_controllers", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_XR_SHOW_CONTROLLERS);
  RNA_def_property_ui_text(
      prop, "Show Controllers", "Show VR controllers (requires VR action for controller poses)");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_custom_overlays", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_XR_SHOW_CUSTOM_OVERLAYS);
  RNA_def_property_ui_text(prop, "Show Custom Overlays", "Show custom VR overlays");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller_draw_style", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, controller_draw_styles);
  RNA_def_property_ui_text(
      prop, "Controller Draw Style", "Style to use when drawing VR controllers");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "selection_eye", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, selection_eyes);
  RNA_def_property_ui_text(
      prop, "Selection Eye", "Which eye's perspective to use when selecting in VR");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "clip_start", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_range(prop, 1e-6f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_property_ui_text(prop, "Clip Start", "VR viewport near clipping distance");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "clip_end", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_range(prop, 1e-6f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_property_ui_text(prop, "Clip End", "VR viewport far clipping distance");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "use_positional_tracking", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_use_positional_tracking_get",
                                 "rna_XrSessionSettings_use_positional_tracking_set");
  RNA_def_property_ui_text(
      prop,
      "Positional Tracking",
      "Allow VR headsets to affect the location in virtual space, in addition to the rotation");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "use_absolute_tracking", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_use_absolute_tracking_get",
                                 "rna_XrSessionSettings_use_absolute_tracking_set");
  RNA_def_property_ui_text(
      prop, "Absolute Tracking", "Use unadjusted location/rotation as defined by the XR runtime");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "headset_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_XrSessionSettings_headset_object_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Headset Object",
      "Object whose location and rotation will be constrained to the VR headset's pose");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller0_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_XrSessionSettings_controller0_object_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Controller 0 Object",
      "Object whose location and rotation will be constrained to the first VR controller's pose");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller1_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_pointer_funcs(
      prop, NULL, "rna_XrSessionSettings_controller1_object_set", NULL, NULL);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Controller 1 Object",
      "Object whose location and rotation will be constrained to the second VR controller's pose");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "headset_object_enable", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_headset_object_enable_get",
                                 "rna_XrSessionSettings_headset_object_enable_set");
  RNA_def_property_ui_text(prop, "Enable Headset Object", "Enable headset object");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "headset_object_autokey", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_headset_object_autokey_get",
                                 "rna_XrSessionSettings_headset_object_autokey_set");
  RNA_def_property_ui_text(prop,
                           "Auto Key Headset Object",
                           "Auto-insert keyframes for the headset object on animation playback");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller0_object_enable", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_controller0_object_enable_get",
                                 "rna_XrSessionSettings_controller0_object_enable_set");
  RNA_def_property_ui_text(prop, "Enable Controller 0 Object", "Enable first controller object");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller0_object_autokey", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_controller0_object_autokey_get",
                                 "rna_XrSessionSettings_controller0_object_autokey_set");
  RNA_def_property_ui_text(
      prop,
      "Auto Key Controller 0 Object",
      "Auto-insert keyframes for the first controlller object on animation playback");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller1_object_enable", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_controller1_object_enable_get",
                                 "rna_XrSessionSettings_controller1_object_enable_set");
  RNA_def_property_ui_text(prop, "Enable Controller 1 Object", "Enable second controller object");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "controller1_object_autokey", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop,
                                 "rna_XrSessionSettings_controller1_object_autokey_get",
                                 "rna_XrSessionSettings_controller1_object_autokey_set");
  RNA_def_property_ui_text(
      prop,
      "Auto Key Controller 1 Object",
      "Auto-insert keyframes for the second controlller object on animation playback");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);
}

static void rna_def_xr_session_state(BlenderRNA *brna)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm, *prop;

  static const EnumPropertyItem action_types[] = {
      {XR_FLOAT_INPUT, "BUTTON", 0, "Button", "Button action"},
      {XR_VECTOR2F_INPUT, "AXIS", 0, "Axis", "2D axis action"},
      {XR_POSE_INPUT, "POSE", 0, "Pose", "3D pose action"},
      {XR_VIBRATION_OUTPUT, "HAPTIC", 0, "Haptic", "Haptic output action"},
      {0, NULL, 0, NULL, NULL},
  };

  static const EnumPropertyItem op_flags[] = {
      {XR_OP_PRESS,
       "PRESS",
       0,
       "Press",
       "Execute operator on button press (non-modal operators only)"},
      {XR_OP_RELEASE,
       "RELEASE",
       0,
       "Release",
       "Execute operator on button release (non-modal operators only)"},
      {XR_OP_MODAL, "MODAL", 0, "Modal", "Use modal execution (modal operators only)"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "XrSessionState", NULL);
  RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
  RNA_def_struct_ui_text(srna, "Session State", "Runtime state information about the VR session");

  func = RNA_def_function(srna, "is_running", "rna_XrSessionState_is_running");
  RNA_def_function_ui_description(func, "Query if the VR session is currently running");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "reset_to_base_pose", "rna_XrSessionState_reset_to_base_pose");
  RNA_def_function_ui_description(func, "Force resetting of position and rotation deltas");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  func = RNA_def_function(srna, "create_action_set", "rna_XrSessionState_action_set_create");
  RNA_def_function_ui_description(func, "Create a VR action set");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "action_set_name",
                        NULL,
                        64,
                        "Action Set",
                        "Action set name (must not contain upper case letters or special "
                        "characters other than '-', '_', or '.'");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "create_action", "rna_XrSessionState_action_create");
  RNA_def_function_ui_description(func, "Create a VR action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "action_name",
                        NULL,
                        64,
                        "Action",
                        "Action name (must not contain upper case letters or special characters "
                        "other than '-', '_', or '.'");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_enum(func, "type", action_types, 0, "Type", "Action type");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path0", NULL, 64, "User Path 0", "User path 0");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path1", NULL, 64, "User Path 1", "User path 1");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "threshold",
                       0.3f,
                       0.0f,
                       1.0f,
                       "Threshold",
                       "Input threshold for button/axis actions",
                       0.0f,
                       1.0f);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_string(func, "op", NULL, OP_MAX_TYPENAME, "Operator", "Operator to execute");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_enum(func,
                      "op_flag",
                      op_flags,
                      0,
                      "Operator Flag",
                      "When to execute the operator (press, release, or modal)");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "create_action_space", "rna_XrSessionState_action_space_create");
  RNA_def_function_ui_description(func, "Create a VR action space");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, 64, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path0", NULL, 64, "User Path 0", "OpenXR user path 0");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path1", NULL, 64, "User Path 1", "OpenXR user path 1");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float_translation(func,
                                   "location",
                                   3,
                                   NULL,
                                   -FLT_MAX,
                                   FLT_MAX,
                                   "Location Offset",
                                   "Location offset",
                                   -FLT_MAX,
                                   FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float_rotation(func,
                                "rotation",
                                3,
                                NULL,
                                -2 * M_PI,
                                2 * M_PI,
                                "Rotation Offset",
                                "Rotation offset",
                                -2 * M_PI,
                                2 * M_PI);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(
      srna, "create_action_binding", "rna_XrSessionState_action_binding_create");
  RNA_def_function_ui_description(func, "Create a VR action binding");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "profile", NULL, 256, "Profile", "OpenXR interaction profile path");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, 64, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "interaction_path0",
                        NULL,
                        256,
                        "Interaction Path 0",
                        "OpenXR interaction (user + component) path 0");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func,
                        "interaction_path1",
                        NULL,
                        256,
                        "Interaction Path 1",
                        "OpenXR interaction (user + component) path 1");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(
      srna, "set_active_action_set", "rna_XrSessionState_active_action_set_set");
  RNA_def_function_ui_description(func, "Set the active VR action set");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(
      srna, "set_controller_pose_action", "rna_XrSessionState_controller_pose_action_set");
  RNA_def_function_ui_description(func, "Set the action that determines the VR controller poses");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action", NULL, 64, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  func = RNA_def_function(srna, "get_action_state", "rna_XrSessionState_action_state_get");
  RNA_def_function_ui_description(func, "Get the current state of a VR action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, 64, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path", NULL, 64, "User Path", "OpenXR user path");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "state",
                       0.0f,
                       -FLT_MAX,
                       FLT_MAX,
                       "Action state",
                       "Current state of the VR action",
                       -FLT_MAX,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  func = RNA_def_function(
      srna, "get_pose_action_state", "rna_XrSessionState_pose_action_state_get");
  RNA_def_function_ui_description(func, "Get the current state of a VR pose action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, 64, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path", NULL, 64, "User Path", "OpenXR user path");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float_array(func,
                             "state",
                             7,
                             NULL,
                             -FLT_MAX,
                             FLT_MAX,
                             "Pose state",
                             "Location + quaternion rotation",
                             -FLT_MAX,
                             FLT_MAX);
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_OUTPUT);

  func = RNA_def_function(srna, "apply_haptic_action", "rna_XrSessionState_haptic_action_apply");
  RNA_def_function_ui_description(func, "Apply a VR haptic action");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_set_name", NULL, 64, "Action Set", "Action set name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "action_name", NULL, 64, "Action", "Action name");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path0", NULL, 64, "User Path 0", "OpenXR user path 0");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_string(func, "user_path1", NULL, 64, "User Path 1", "OpenXR user path 1");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "duration",
                       0.0f,
                       0.0f,
                       FLT_MAX,
                       "Duration",
                       "Haptic duration in seconds, 0 = minimum supported duration",
                       0.0f,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float(func,
                       "frequency",
                       0.0f,
                       0.0f,
                       FLT_MAX,
                       "Frequency",
                       "Haptic frequency, 0 = default frequency",
                       0.0f,
                       FLT_MAX);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_float(
      func, "amplitude", 1.0f, 0.0f, 1.0f, "Amplitude", "Haptic amplitude (0 ~ 1)", 0.0f, 1.0f);
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  prop = RNA_def_property(srna, "viewer_pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_pose_location_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Pose Location",
      "Last known location of the viewer pose (center between the eyes) in world space");

  prop = RNA_def_property(srna, "viewer_pose_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_pose_rotation_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Pose Rotation",
      "Last known rotation of the viewer pose (center between the eyes) in world space");

  prop = RNA_def_property(srna, "controller_pose0_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_controller_pose0_location_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Controller Pose 0 Location",
                           "Last known location of the first controller pose in world space");

  prop = RNA_def_property(srna, "controller_pose0_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_controller_pose0_rotation_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Controller Pose 0 Rotation",
                           "Last known rotation of the first controller pose in world space");

  prop = RNA_def_property(srna, "controller_pose1_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_controller_pose1_location_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Controller Pose Location 1",
                           "Last known location of the second controller pose in world space");

  prop = RNA_def_property(srna, "controller_pose1_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_controller_pose1_rotation_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Controller Pose 1 Rotation",
                           "Last known rotation of the second controller pose in world space");
}

void RNA_def_xr(BlenderRNA *brna)
{
  RNA_define_animate_sdna(false);

  rna_def_xr_session_settings(brna);
  rna_def_xr_session_state(brna);

  RNA_define_animate_sdna(true);
}

#endif /* RNA_RUNTIME */
