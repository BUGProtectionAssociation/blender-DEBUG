/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2001-2002 NaN Holding BV. All rights reserved. */

/** \file
 * \ingroup bke
 *
 * Mesh normal calculation functions.
 *
 * \see bmesh_mesh_normals.c for the equivalent #BMesh functionality.
 */

#include <climits>

#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_alloca.h"
#include "BLI_bit_vector.hh"
#include "BLI_linklist.h"
#include "BLI_linklist_stack.h"
#include "BLI_math.h"
#include "BLI_math_vec_types.hh"
#include "BLI_memarena.h"
#include "BLI_span.hh"
#include "BLI_stack.h"
#include "BLI_task.h"
#include "BLI_task.hh"
#include "BLI_utildefines.h"

#include "BKE_customdata.h"
#include "BKE_editmesh_cache.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"

#include "atomic_ops.h"

using blender::BitVector;
using blender::float3;
using blender::MutableSpan;
using blender::short2;
using blender::Span;

// #define DEBUG_TIME

#ifdef DEBUG_TIME
#  include "BLI_timeit.hh"
#endif

/* -------------------------------------------------------------------- */
/** \name Private Utility Functions
 * \{ */

/**
 * A thread-safe version of #add_v3_v3 that uses a spin-lock.
 *
 * \note Avoid using this when the chance of contention is high.
 */
static void add_v3_v3_atomic(float r[3], const float a[3])
{
#define FLT_EQ_NONAN(_fa, _fb) (*((const uint32_t *)&_fa) == *((const uint32_t *)&_fb))

  float virtual_lock = r[0];
  while (true) {
    /* This loops until following conditions are met:
     * - `r[0]` has same value as virtual_lock (i.e. it did not change since last try).
     * - `r[0]` was not `FLT_MAX`, i.e. it was not locked by another thread. */
    const float test_lock = atomic_cas_float(&r[0], virtual_lock, FLT_MAX);
    if (_ATOMIC_LIKELY(FLT_EQ_NONAN(test_lock, virtual_lock) && (test_lock != FLT_MAX))) {
      break;
    }
    virtual_lock = test_lock;
  }
  virtual_lock += a[0];
  r[1] += a[1];
  r[2] += a[2];

  /* Second atomic operation to 'release'
   * our lock on that vector and set its first scalar value. */
  /* Note that we do not need to loop here, since we 'locked' `r[0]`,
   * nobody should have changed it in the mean time. */
  virtual_lock = atomic_cas_float(&r[0], FLT_MAX, virtual_lock);
  BLI_assert(virtual_lock == FLT_MAX);

#undef FLT_EQ_NONAN
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Public Utility Functions
 *
 * Related to managing normals but not directly related to calculating normals.
 * \{ */

void BKE_mesh_normals_tag_dirty(Mesh *mesh)
{
  mesh->runtime->vert_normals_dirty = true;
  mesh->runtime->poly_normals_dirty = true;
}

float (*BKE_mesh_vertex_normals_for_write(Mesh *mesh))[3]
{
  if (mesh->runtime->vert_normals == nullptr) {
    mesh->runtime->vert_normals = (float(*)[3])MEM_malloc_arrayN(
        mesh->totvert, sizeof(float[3]), __func__);
  }

  BLI_assert(MEM_allocN_len(mesh->runtime->vert_normals) >= sizeof(float[3]) * mesh->totvert);

  return mesh->runtime->vert_normals;
}

float (*BKE_mesh_poly_normals_for_write(Mesh *mesh))[3]
{
  if (mesh->runtime->poly_normals == nullptr) {
    mesh->runtime->poly_normals = (float(*)[3])MEM_malloc_arrayN(
        mesh->totpoly, sizeof(float[3]), __func__);
  }

  BLI_assert(MEM_allocN_len(mesh->runtime->poly_normals) >= sizeof(float[3]) * mesh->totpoly);

  return mesh->runtime->poly_normals;
}

void BKE_mesh_vertex_normals_clear_dirty(Mesh *mesh)
{
  mesh->runtime->vert_normals_dirty = false;
  BLI_assert(mesh->runtime->vert_normals || mesh->totvert == 0);
}

void BKE_mesh_poly_normals_clear_dirty(Mesh *mesh)
{
  mesh->runtime->poly_normals_dirty = false;
  BLI_assert(mesh->runtime->poly_normals || mesh->totpoly == 0);
}

bool BKE_mesh_vertex_normals_are_dirty(const Mesh *mesh)
{
  return mesh->runtime->vert_normals_dirty;
}

bool BKE_mesh_poly_normals_are_dirty(const Mesh *mesh)
{
  return mesh->runtime->poly_normals_dirty;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Normal Calculation (Polygons)
 * \{ */

/*
 * COMPUTE POLY NORMAL
 *
 * Computes the normal of a planar
 * polygon See Graphics Gems for
 * computing newell normal.
 */
static void mesh_calc_ngon_normal(const MPoly *mpoly,
                                  const int *poly_verts,
                                  const float (*positions)[3],
                                  float r_normal[3])
{
  const int nverts = mpoly->totloop;
  const float *v_prev = positions[poly_verts[nverts - 1]];
  const float *v_curr;

  zero_v3(r_normal);

  /* Newell's Method */
  for (int i = 0; i < nverts; i++) {
    v_curr = positions[poly_verts[i]];
    add_newell_cross_v3_v3v3(r_normal, v_prev, v_curr);
    v_prev = v_curr;
  }

  if (UNLIKELY(normalize_v3(r_normal) == 0.0f)) {
    r_normal[2] = 1.0f; /* other axis set to 0.0 */
  }
}

void BKE_mesh_calc_poly_normal(const MPoly *mpoly,
                               const int *poly_verts,
                               const float (*positions)[3],
                               float r_no[3])
{
  if (mpoly->totloop > 4) {
    mesh_calc_ngon_normal(mpoly, poly_verts, positions, r_no);
  }
  else if (mpoly->totloop == 3) {
    normal_tri_v3(
        r_no, positions[poly_verts[0]], positions[poly_verts[1]], positions[poly_verts[2]]);
  }
  else if (mpoly->totloop == 4) {
    normal_quad_v3(r_no,
                   positions[poly_verts[0]],
                   positions[poly_verts[1]],
                   positions[poly_verts[2]],
                   positions[poly_verts[3]]);
  }
  else { /* horrible, two sided face! */
    r_no[0] = 0.0;
    r_no[1] = 0.0;
    r_no[2] = 1.0;
  }
}

static void calculate_normals_poly(const Span<float3> positions,
                                   const Span<MPoly> polys,
                                   const Span<int> corner_verts,
                                   MutableSpan<float3> poly_normals)
{
  using namespace blender;
  threading::parallel_for(polys.index_range(), 1024, [&](const IndexRange range) {
    for (const int poly_i : range) {
      const MPoly &poly = polys[poly_i];
      BKE_mesh_calc_poly_normal(&poly,
                                &corner_verts[poly.loopstart],
                                reinterpret_cast<const float(*)[3]>(positions.data()),
                                poly_normals[poly_i]);
    }
  });
}

void BKE_mesh_calc_normals_poly(const float (*positions)[3],
                                const int verts_num,
                                const int *corner_verts,
                                const int mloop_len,
                                const MPoly *mpoly,
                                int mpoly_len,
                                float (*r_poly_normals)[3])
{
  calculate_normals_poly({reinterpret_cast<const float3 *>(positions), verts_num},
                         {mpoly, mpoly_len},
                         {corner_verts, mloop_len},
                         {reinterpret_cast<float3 *>(r_poly_normals), mpoly_len});
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Normal Calculation (Polygons & Vertices)
 *
 * Take care making optimizations to this function as improvements to low-poly
 * meshes can slow down high-poly meshes. For details on performance, see D11993.
 * \{ */

static void calculate_normals_poly_and_vert(const Span<float3> positions,
                                            const Span<MPoly> polys,
                                            const Span<int> corner_verts,
                                            MutableSpan<float3> poly_normals,
                                            MutableSpan<float3> vert_normals)
{
  using namespace blender;

  /* Zero the vertex normal array for accumulation. */
  {
    memset(vert_normals.data(), 0, vert_normals.as_span().size_in_bytes());
  }

  /* Compute poly normals, accumulating them into vertex normals. */
  {
    threading::parallel_for(polys.index_range(), 1024, [&](const IndexRange range) {
      for (const int poly_i : range) {
        const MPoly &poly = polys[poly_i];
        const Span<int> poly_verts = corner_verts.slice(poly.loopstart, poly.totloop);

        float3 &pnor = poly_normals[poly_i];

        const int i_end = poly.totloop - 1;

        /* Polygon Normal and edge-vector. */
        /* Inline version of #BKE_mesh_calc_poly_normal, also does edge-vectors. */
        {
          zero_v3(pnor);
          /* Newell's Method */
          const float *v_curr = positions[poly_verts[i_end]];
          for (int i_next = 0; i_next <= i_end; i_next++) {
            const float *v_next = positions[poly_verts[i_next]];
            add_newell_cross_v3_v3v3(pnor, v_curr, v_next);
            v_curr = v_next;
          }
          if (UNLIKELY(normalize_v3(pnor) == 0.0f)) {
            pnor[2] = 1.0f; /* Other axes set to zero. */
          }
        }

        /* Accumulate angle weighted face normal into the vertex normal. */
        /* Inline version of #accumulate_vertex_normals_poly_v3. */
        {
          float edvec_prev[3], edvec_next[3], edvec_end[3];
          const float *v_curr = positions[poly_verts[i_end]];
          sub_v3_v3v3(edvec_prev, positions[poly_verts[i_end - 1]], v_curr);
          normalize_v3(edvec_prev);
          copy_v3_v3(edvec_end, edvec_prev);

          for (int i_next = 0, i_curr = i_end; i_next <= i_end; i_curr = i_next++) {
            const float *v_next = positions[poly_verts[i_next]];

            /* Skip an extra normalization by reusing the first calculated edge. */
            if (i_next != i_end) {
              sub_v3_v3v3(edvec_next, v_curr, v_next);
              normalize_v3(edvec_next);
            }
            else {
              copy_v3_v3(edvec_next, edvec_end);
            }

            /* Calculate angle between the two poly edges incident on this vertex. */
            const float fac = saacos(-dot_v3v3(edvec_prev, edvec_next));
            const float vnor_add[3] = {pnor[0] * fac, pnor[1] * fac, pnor[2] * fac};

            float *vnor = vert_normals[corner_verts[i_curr]];
            add_v3_v3_atomic(vnor, vnor_add);
            v_curr = v_next;
            copy_v3_v3(edvec_prev, edvec_next);
          }
        }
      }
    });
  }

  /* Normalize and validate computed vertex normals. */
  {
    threading::parallel_for(positions.index_range(), 1024, [&](const IndexRange range) {
      for (const int vert_i : range) {
        float *no = vert_normals[vert_i];

        if (UNLIKELY(normalize_v3(no) == 0.0f)) {
          /* Following Mesh convention; we use vertex coordinate itself for normal in this case. */
          normalize_v3_v3(no, positions[vert_i]);
        }
      }
    });
  }
}

void BKE_mesh_calc_normals_poly_and_vertex(const float (*positions)[3],
                                           const int mvert_len,
                                           const int *corner_verts,
                                           const int mloop_len,
                                           const MPoly *mpoly,
                                           const int mpoly_len,
                                           float (*r_poly_normals)[3],
                                           float (*r_vert_normals)[3])
{
  calculate_normals_poly_and_vert({reinterpret_cast<const float3 *>(positions), mvert_len},
                                  {mpoly, mpoly_len},
                                  {corner_verts, mloop_len},
                                  {reinterpret_cast<float3 *>(r_poly_normals), mpoly_len},
                                  {reinterpret_cast<float3 *>(r_vert_normals), mvert_len});
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mesh Normal Calculation
 * \{ */

const float (*BKE_mesh_vertex_normals_ensure(const Mesh *mesh))[3]
{
  if (!BKE_mesh_vertex_normals_are_dirty(mesh)) {
    BLI_assert(mesh->runtime->vert_normals != nullptr || mesh->totvert == 0);
    return mesh->runtime->vert_normals;
  }

  if (mesh->totvert == 0) {
    return nullptr;
  }

  std::lock_guard lock{mesh->runtime->normals_mutex};
  if (!BKE_mesh_vertex_normals_are_dirty(mesh)) {
    BLI_assert(mesh->runtime->vert_normals != nullptr);
    return mesh->runtime->vert_normals;
  }

  float(*vert_normals)[3];
  float(*poly_normals)[3];

  /* Isolate task because a mutex is locked and computing normals is multi-threaded. */
  blender::threading::isolate_task([&]() {
    Mesh &mesh_mutable = *const_cast<Mesh *>(mesh);
    const Span<float3> positions = mesh_mutable.positions();
    const Span<MPoly> polys = mesh_mutable.polys();
    const Span<int> corner_verts = mesh_mutable.corner_verts();

    vert_normals = BKE_mesh_vertex_normals_for_write(&mesh_mutable);
    poly_normals = BKE_mesh_poly_normals_for_write(&mesh_mutable);

    BKE_mesh_calc_normals_poly_and_vertex(reinterpret_cast<const float(*)[3]>(positions.data()),
                                          positions.size(),
                                          corner_verts.data(),
                                          corner_verts.size(),
                                          polys.data(),
                                          polys.size(),
                                          poly_normals,
                                          vert_normals);

    BKE_mesh_vertex_normals_clear_dirty(&mesh_mutable);
    BKE_mesh_poly_normals_clear_dirty(&mesh_mutable);
  });

  return vert_normals;
}

const float (*BKE_mesh_poly_normals_ensure(const Mesh *mesh))[3]
{
  if (!BKE_mesh_poly_normals_are_dirty(mesh)) {
    BLI_assert(mesh->runtime->poly_normals != nullptr || mesh->totpoly == 0);
    return mesh->runtime->poly_normals;
  }

  if (mesh->totpoly == 0) {
    return nullptr;
  }

  std::lock_guard lock{mesh->runtime->normals_mutex};
  if (!BKE_mesh_poly_normals_are_dirty(mesh)) {
    BLI_assert(mesh->runtime->poly_normals != nullptr);
    return mesh->runtime->poly_normals;
  }

  float(*poly_normals)[3];

  /* Isolate task because a mutex is locked and computing normals is multi-threaded. */
  blender::threading::isolate_task([&]() {
    Mesh &mesh_mutable = *const_cast<Mesh *>(mesh);
    const Span<float3> positions = mesh_mutable.positions();
    const Span<MPoly> polys = mesh_mutable.polys();
    const Span<int> corner_verts = mesh_mutable.corner_verts();

    poly_normals = BKE_mesh_poly_normals_for_write(&mesh_mutable);

    BKE_mesh_calc_normals_poly(reinterpret_cast<const float(*)[3]>(positions.data()),
                               positions.size(),
                               corner_verts.data(),
                               corner_verts.size(),
                               polys.data(),
                               polys.size(),
                               poly_normals);

    BKE_mesh_poly_normals_clear_dirty(&mesh_mutable);
  });

  return poly_normals;
}

void BKE_mesh_ensure_normals_for_display(Mesh *mesh)
{
  switch (mesh->runtime->wrapper_type) {
    case ME_WRAPPER_TYPE_SUBD:
    case ME_WRAPPER_TYPE_MDATA:
      BKE_mesh_vertex_normals_ensure(mesh);
      BKE_mesh_poly_normals_ensure(mesh);
      break;
    case ME_WRAPPER_TYPE_BMESH: {
      BMEditMesh *em = mesh->edit_mesh;
      EditMeshData *emd = mesh->runtime->edit_data;
      if (emd->vertexCos) {
        BKE_editmesh_cache_ensure_vert_normals(em, emd);
        BKE_editmesh_cache_ensure_poly_normals(em, emd);
      }
      return;
    }
  }
}

void BKE_mesh_calc_normals(Mesh *mesh)
{
#ifdef DEBUG_TIME
  SCOPED_TIMER_AVERAGED(__func__);
#endif
  BKE_mesh_vertex_normals_ensure(mesh);
}

void BKE_lnor_spacearr_init(MLoopNorSpaceArray *lnors_spacearr,
                            const int numLoops,
                            const char data_type)
{
  if (!(lnors_spacearr->lspacearr && lnors_spacearr->loops_pool)) {
    MemArena *mem;

    if (!lnors_spacearr->mem) {
      lnors_spacearr->mem = BLI_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);
    }
    mem = lnors_spacearr->mem;
    lnors_spacearr->lspacearr = (MLoopNorSpace **)BLI_memarena_calloc(
        mem, sizeof(MLoopNorSpace *) * size_t(numLoops));
    lnors_spacearr->loops_pool = (LinkNode *)BLI_memarena_alloc(
        mem, sizeof(LinkNode) * size_t(numLoops));

    lnors_spacearr->spaces_num = 0;
  }
  BLI_assert(ELEM(data_type, MLNOR_SPACEARR_BMLOOP_PTR, MLNOR_SPACEARR_LOOP_INDEX));
  lnors_spacearr->data_type = data_type;
}

void BKE_lnor_spacearr_tls_init(MLoopNorSpaceArray *lnors_spacearr,
                                MLoopNorSpaceArray *lnors_spacearr_tls)
{
  *lnors_spacearr_tls = *lnors_spacearr;
  lnors_spacearr_tls->mem = BLI_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);
}

void BKE_lnor_spacearr_tls_join(MLoopNorSpaceArray *lnors_spacearr,
                                MLoopNorSpaceArray *lnors_spacearr_tls)
{
  BLI_assert(lnors_spacearr->data_type == lnors_spacearr_tls->data_type);
  BLI_assert(lnors_spacearr->mem != lnors_spacearr_tls->mem);
  lnors_spacearr->spaces_num += lnors_spacearr_tls->spaces_num;
  BLI_memarena_merge(lnors_spacearr->mem, lnors_spacearr_tls->mem);
  BLI_memarena_free(lnors_spacearr_tls->mem);
  lnors_spacearr_tls->mem = nullptr;
  BKE_lnor_spacearr_clear(lnors_spacearr_tls);
}

void BKE_lnor_spacearr_clear(MLoopNorSpaceArray *lnors_spacearr)
{
  lnors_spacearr->spaces_num = 0;
  lnors_spacearr->lspacearr = nullptr;
  lnors_spacearr->loops_pool = nullptr;
  if (lnors_spacearr->mem != nullptr) {
    BLI_memarena_clear(lnors_spacearr->mem);
  }
}

void BKE_lnor_spacearr_free(MLoopNorSpaceArray *lnors_spacearr)
{
  lnors_spacearr->spaces_num = 0;
  lnors_spacearr->lspacearr = nullptr;
  lnors_spacearr->loops_pool = nullptr;
  BLI_memarena_free(lnors_spacearr->mem);
  lnors_spacearr->mem = nullptr;
}

MLoopNorSpace *BKE_lnor_space_create(MLoopNorSpaceArray *lnors_spacearr)
{
  lnors_spacearr->spaces_num++;
  return (MLoopNorSpace *)BLI_memarena_calloc(lnors_spacearr->mem, sizeof(MLoopNorSpace));
}

/* This threshold is a bit touchy (usual float precision issue), this value seems OK. */
#define LNOR_SPACE_TRIGO_THRESHOLD (1.0f - 1e-4f)

void BKE_lnor_space_define(MLoopNorSpace *lnor_space,
                           const float lnor[3],
                           float vec_ref[3],
                           float vec_other[3],
                           BLI_Stack *edge_vectors)
{
  const float pi2 = float(M_PI) * 2.0f;
  float tvec[3], dtp;
  const float dtp_ref = dot_v3v3(vec_ref, lnor);
  const float dtp_other = dot_v3v3(vec_other, lnor);

  if (UNLIKELY(fabsf(dtp_ref) >= LNOR_SPACE_TRIGO_THRESHOLD ||
               fabsf(dtp_other) >= LNOR_SPACE_TRIGO_THRESHOLD)) {
    /* If vec_ref or vec_other are too much aligned with lnor, we can't build lnor space,
     * tag it as invalid and abort. */
    lnor_space->ref_alpha = lnor_space->ref_beta = 0.0f;

    if (edge_vectors) {
      BLI_stack_clear(edge_vectors);
    }
    return;
  }

  copy_v3_v3(lnor_space->vec_lnor, lnor);

  /* Compute ref alpha, average angle of all available edge vectors to lnor. */
  if (edge_vectors) {
    float alpha = 0.0f;
    int count = 0;
    while (!BLI_stack_is_empty(edge_vectors)) {
      const float *vec = (const float *)BLI_stack_peek(edge_vectors);
      alpha += saacosf(dot_v3v3(vec, lnor));
      BLI_stack_discard(edge_vectors);
      count++;
    }
    /* NOTE: In theory, this could be `count > 2`,
     * but there is one case where we only have two edges for two loops:
     * a smooth vertex with only two edges and two faces (our Monkey's nose has that, e.g.).
     */
    BLI_assert(count >= 2); /* This piece of code shall only be called for more than one loop. */
    lnor_space->ref_alpha = alpha / float(count);
  }
  else {
    lnor_space->ref_alpha = (saacosf(dot_v3v3(vec_ref, lnor)) +
                             saacosf(dot_v3v3(vec_other, lnor))) /
                            2.0f;
  }

  /* Project vec_ref on lnor's ortho plane. */
  mul_v3_v3fl(tvec, lnor, dtp_ref);
  sub_v3_v3(vec_ref, tvec);
  normalize_v3_v3(lnor_space->vec_ref, vec_ref);

  cross_v3_v3v3(tvec, lnor, lnor_space->vec_ref);
  normalize_v3_v3(lnor_space->vec_ortho, tvec);

  /* Project vec_other on lnor's ortho plane. */
  mul_v3_v3fl(tvec, lnor, dtp_other);
  sub_v3_v3(vec_other, tvec);
  normalize_v3(vec_other);

  /* Beta is angle between ref_vec and other_vec, around lnor. */
  dtp = dot_v3v3(lnor_space->vec_ref, vec_other);
  if (LIKELY(dtp < LNOR_SPACE_TRIGO_THRESHOLD)) {
    const float beta = saacos(dtp);
    lnor_space->ref_beta = (dot_v3v3(lnor_space->vec_ortho, vec_other) < 0.0f) ? pi2 - beta : beta;
  }
  else {
    lnor_space->ref_beta = pi2;
  }
}

void BKE_lnor_space_add_loop(MLoopNorSpaceArray *lnors_spacearr,
                             MLoopNorSpace *lnor_space,
                             const int ml_index,
                             void *bm_loop,
                             const bool is_single)
{
  BLI_assert((lnors_spacearr->data_type == MLNOR_SPACEARR_LOOP_INDEX && bm_loop == nullptr) ||
             (lnors_spacearr->data_type == MLNOR_SPACEARR_BMLOOP_PTR && bm_loop != nullptr));

  lnors_spacearr->lspacearr[ml_index] = lnor_space;
  if (bm_loop == nullptr) {
    bm_loop = POINTER_FROM_INT(ml_index);
  }
  if (is_single) {
    BLI_assert(lnor_space->loops == nullptr);
    lnor_space->flags |= MLNOR_SPACE_IS_SINGLE;
    lnor_space->loops = (LinkNode *)bm_loop;
  }
  else {
    BLI_assert((lnor_space->flags & MLNOR_SPACE_IS_SINGLE) == 0);
    BLI_linklist_prepend_nlink(&lnor_space->loops, bm_loop, &lnors_spacearr->loops_pool[ml_index]);
  }
}

MINLINE float unit_short_to_float(const short val)
{
  return float(val) / float(SHRT_MAX);
}

MINLINE short unit_float_to_short(const float val)
{
  /* Rounding. */
  return short(floorf(val * float(SHRT_MAX) + 0.5f));
}

void BKE_lnor_space_custom_data_to_normal(const MLoopNorSpace *lnor_space,
                                          const short clnor_data[2],
                                          float r_custom_lnor[3])
{
  /* NOP custom normal data or invalid lnor space, return. */
  if (clnor_data[0] == 0 || lnor_space->ref_alpha == 0.0f || lnor_space->ref_beta == 0.0f) {
    copy_v3_v3(r_custom_lnor, lnor_space->vec_lnor);
    return;
  }

  {
    /* TODO: Check whether using #sincosf() gives any noticeable benefit
     * (could not even get it working under linux though)! */
    const float pi2 = float(M_PI * 2.0);
    const float alphafac = unit_short_to_float(clnor_data[0]);
    const float alpha = (alphafac > 0.0f ? lnor_space->ref_alpha : pi2 - lnor_space->ref_alpha) *
                        alphafac;
    const float betafac = unit_short_to_float(clnor_data[1]);

    mul_v3_v3fl(r_custom_lnor, lnor_space->vec_lnor, cosf(alpha));

    if (betafac == 0.0f) {
      madd_v3_v3fl(r_custom_lnor, lnor_space->vec_ref, sinf(alpha));
    }
    else {
      const float sinalpha = sinf(alpha);
      const float beta = (betafac > 0.0f ? lnor_space->ref_beta : pi2 - lnor_space->ref_beta) *
                         betafac;
      madd_v3_v3fl(r_custom_lnor, lnor_space->vec_ref, sinalpha * cosf(beta));
      madd_v3_v3fl(r_custom_lnor, lnor_space->vec_ortho, sinalpha * sinf(beta));
    }
  }
}

void BKE_lnor_space_custom_normal_to_data(const MLoopNorSpace *lnor_space,
                                          const float custom_lnor[3],
                                          short r_clnor_data[2])
{
  /* We use nullptr vector as NOP custom normal (can be simpler than giving auto-computed `lnor`).
   */
  if (is_zero_v3(custom_lnor) || compare_v3v3(lnor_space->vec_lnor, custom_lnor, 1e-4f)) {
    r_clnor_data[0] = r_clnor_data[1] = 0;
    return;
  }

  {
    const float pi2 = float(M_PI * 2.0);
    const float cos_alpha = dot_v3v3(lnor_space->vec_lnor, custom_lnor);
    float vec[3], cos_beta;
    float alpha;

    alpha = saacosf(cos_alpha);
    if (alpha > lnor_space->ref_alpha) {
      /* Note we could stick to [0, pi] range here,
       * but makes decoding more complex, not worth it. */
      r_clnor_data[0] = unit_float_to_short(-(pi2 - alpha) / (pi2 - lnor_space->ref_alpha));
    }
    else {
      r_clnor_data[0] = unit_float_to_short(alpha / lnor_space->ref_alpha);
    }

    /* Project custom lnor on (vec_ref, vec_ortho) plane. */
    mul_v3_v3fl(vec, lnor_space->vec_lnor, -cos_alpha);
    add_v3_v3(vec, custom_lnor);
    normalize_v3(vec);

    cos_beta = dot_v3v3(lnor_space->vec_ref, vec);

    if (cos_beta < LNOR_SPACE_TRIGO_THRESHOLD) {
      float beta = saacosf(cos_beta);
      if (dot_v3v3(lnor_space->vec_ortho, vec) < 0.0f) {
        beta = pi2 - beta;
      }

      if (beta > lnor_space->ref_beta) {
        r_clnor_data[1] = unit_float_to_short(-(pi2 - beta) / (pi2 - lnor_space->ref_beta));
      }
      else {
        r_clnor_data[1] = unit_float_to_short(beta / lnor_space->ref_beta);
      }
    }
    else {
      r_clnor_data[1] = 0;
    }
  }
}

#define LOOP_SPLIT_TASK_BLOCK_SIZE 1024

struct LoopSplitTaskData {
  /* Specific to each instance (each task). */

  /** We have to create those outside of tasks, since #MemArena is not thread-safe. */
  MLoopNorSpace *lnor_space;
  float3 *lnor;
  int ml_curr_index;
  int ml_prev_index;
  /** Also used a flag to switch between single or fan process! */
  const int *e2l_prev;
  int mp_index;

  /** This one is special, it's owned and managed by worker tasks,
   * avoid to have to create it for each fan! */
  BLI_Stack *edge_vectors;

  char pad_c;
};

struct LoopSplitTaskDataCommon {
  /* Read/write.
   * Note we do not need to protect it, though, since two different tasks will *always* affect
   * different elements in the arrays. */
  MLoopNorSpaceArray *lnors_spacearr;
  MutableSpan<float3> loopnors;
  MutableSpan<short2> clnors_data;

  /* Read-only. */
  Span<float3> positions;
  MutableSpan<MEdge> edges;
  Span<int> corner_verts;
  Span<int> corner_edges;
  Span<MPoly> polys;
  int (*edge_to_loops)[2];
  Span<int> loop_to_poly;
  Span<float3> polynors;
  Span<float3> vert_normals;
};

#define INDEX_UNSET INT_MIN
#define INDEX_INVALID -1
/* See comment about edge_to_loops below. */
#define IS_EDGE_SHARP(_e2l) ELEM((_e2l)[1], INDEX_UNSET, INDEX_INVALID)

static void mesh_edges_sharp_tag(LoopSplitTaskDataCommon *data,
                                 const bool check_angle,
                                 const float split_angle,
                                 const bool do_sharp_edges_tag)
{
  MutableSpan<MEdge> edges = data->edges;
  const Span<MPoly> polys = data->polys;
  const Span<int> corner_verts = data->corner_verts;
  const Span<int> corner_edges = data->corner_edges;
  const Span<int> loop_to_poly = data->loop_to_poly;

  MutableSpan<float3> loopnors = data->loopnors; /* NOTE: loopnors may be empty here. */
  const Span<float3> polynors = data->polynors;

  int(*edge_to_loops)[2] = data->edge_to_loops;

  BitVector sharp_edges;
  if (do_sharp_edges_tag) {
    sharp_edges.resize(edges.size(), false);
  }

  const float split_angle_cos = check_angle ? cosf(split_angle) : -1.0f;

  for (const int mp_index : polys.index_range()) {
    const MPoly &poly = polys[mp_index];
    int *e2l;
    int ml_curr_index = poly.loopstart;
    const int ml_last_index = (ml_curr_index + poly.totloop) - 1;

    for (; ml_curr_index <= ml_last_index; ml_curr_index++) {
      const int vert_i = corner_verts[ml_curr_index];
      const int edge_i = corner_edges[ml_curr_index];
      e2l = edge_to_loops[edge_i];

      /* Pre-populate all loop normals as if their verts were all-smooth,
       * this way we don't have to compute those later!
       */
      if (!loopnors.is_empty()) {
        copy_v3_v3(loopnors[ml_curr_index], data->vert_normals[vert_i]);
      }

      /* Check whether current edge might be smooth or sharp */
      if ((e2l[0] | e2l[1]) == 0) {
        /* 'Empty' edge until now, set e2l[0] (and e2l[1] to INDEX_UNSET to tag it as unset). */
        e2l[0] = ml_curr_index;
        /* We have to check this here too, else we might miss some flat faces!!! */
        e2l[1] = (poly.flag & ME_SMOOTH) ? INDEX_UNSET : INDEX_INVALID;
      }
      else if (e2l[1] == INDEX_UNSET) {
        const bool is_angle_sharp = (check_angle &&
                                     dot_v3v3(polynors[loop_to_poly[e2l[0]]], polynors[mp_index]) <
                                         split_angle_cos);

        /* Second loop using this edge, time to test its sharpness.
         * An edge is sharp if it is tagged as such, or its face is not smooth,
         * or both poly have opposed (flipped) normals, i.e. both loops on the same edge share the
         * same vertex, or angle between both its polys' normals is above split_angle value.
         */
        if (!(poly.flag & ME_SMOOTH) || (edges[edge_i].flag & ME_SHARP) ||
            vert_i == corner_verts[e2l[0]] || is_angle_sharp) {
          /* NOTE: we are sure that loop != 0 here ;). */
          e2l[1] = INDEX_INVALID;

          /* We want to avoid tagging edges as sharp when it is already defined as such by
           * other causes than angle threshold. */
          if (do_sharp_edges_tag && is_angle_sharp) {
            sharp_edges[edge_i].set();
          }
        }
        else {
          e2l[1] = ml_curr_index;
        }
      }
      else if (!IS_EDGE_SHARP(e2l)) {
        /* More than two loops using this edge, tag as sharp if not yet done. */
        e2l[1] = INDEX_INVALID;

        /* We want to avoid tagging edges as sharp when it is already defined as such by
         * other causes than angle threshold. */
        if (do_sharp_edges_tag) {
          sharp_edges[edge_i].reset();
        }
      }
      /* Else, edge is already 'disqualified' (i.e. sharp)! */
    }
  }

  /* If requested, do actual tagging of edges as sharp in another loop. */
  if (do_sharp_edges_tag) {
    for (const int i : edges.index_range()) {
      if (sharp_edges[i]) {
        edges[i].flag |= ME_SHARP;
      }
    }
  }
}

void BKE_edges_sharp_from_angle_set(const float (*positions)[3],
                                    const int numVerts,
                                    struct MEdge *medges,
                                    const int numEdges,
                                    const int *corner_verts,
                                    const int *corner_edges,
                                    const int numLoops,
                                    const MPoly *mpolys,
                                    const float (*polynors)[3],
                                    const int numPolys,
                                    const float split_angle)
{
  using namespace blender;
  using namespace blender::bke;
  if (split_angle >= float(M_PI)) {
    /* Nothing to do! */
    return;
  }

  /* Mapping edge -> loops. See #BKE_mesh_normals_loop_split for details. */
  int(*edge_to_loops)[2] = (int(*)[2])MEM_calloc_arrayN(
      size_t(numEdges), sizeof(*edge_to_loops), __func__);

  /* Simple mapping from a loop to its polygon index. */
  const Array<int> loop_to_poly = mesh_topology::build_loop_to_poly_map({mpolys, numPolys},
                                                                        numLoops);

  LoopSplitTaskDataCommon common_data = {};
  common_data.positions = {reinterpret_cast<const float3 *>(positions), numVerts};
  common_data.edges = {medges, numEdges};
  common_data.polys = {mpolys, numPolys};
  common_data.corner_verts = {corner_verts, numLoops};
  common_data.corner_edges = {corner_edges, numLoops};
  common_data.edge_to_loops = edge_to_loops;
  common_data.loop_to_poly = loop_to_poly;
  common_data.polynors = {reinterpret_cast<const float3 *>(polynors), numPolys};

  mesh_edges_sharp_tag(&common_data, true, split_angle, true);

  MEM_freeN(edge_to_loops);
}

static void loop_manifold_fan_around_vert_next(const Span<int> corner_verts,
                                               const Span<MPoly> polys,
                                               const Span<int> loop_to_poly,
                                               const int *e2lfan_curr,
                                               const uint mv_pivot_index,
                                               int *r_mlfan_curr_index,
                                               int *r_mlfan_vert_index,
                                               int *r_mpfan_curr_index)
{
  const int fan_vert_curr = corner_verts[*r_mlfan_curr_index];

  /* WARNING: This is rather complex!
   * We have to find our next edge around the vertex (fan mode).
   * First we find the next loop, which is either previous or next to mlfan_curr_index, depending
   * whether both loops using current edge are in the same direction or not, and whether
   * mlfan_curr_index actually uses the vertex we are fanning around!
   * mlfan_curr_index is the index of mlfan_next here, and mlfan_next is not the real next one
   * (i.e. not the future `mlfan_curr`). */
  *r_mlfan_curr_index = (e2lfan_curr[0] == *r_mlfan_curr_index) ? e2lfan_curr[1] : e2lfan_curr[0];
  *r_mpfan_curr_index = loop_to_poly[*r_mlfan_curr_index];

  BLI_assert(*r_mlfan_curr_index >= 0);
  BLI_assert(*r_mpfan_curr_index >= 0);

  const int fan_vert_next = corner_verts[*r_mlfan_curr_index];

  const MPoly &mpfan_next = polys[*r_mpfan_curr_index];

  if ((fan_vert_curr == fan_vert_next && fan_vert_curr == mv_pivot_index) ||
      (fan_vert_curr != fan_vert_next && fan_vert_curr != mv_pivot_index)) {
    /* We need the previous loop, but current one is our vertex's loop. */
    *r_mlfan_vert_index = *r_mlfan_curr_index;
    if (--(*r_mlfan_curr_index) < mpfan_next.loopstart) {
      *r_mlfan_curr_index = mpfan_next.loopstart + mpfan_next.totloop - 1;
    }
  }
  else {
    /* We need the next loop, which is also our vertex's loop. */
    if (++(*r_mlfan_curr_index) >= mpfan_next.loopstart + mpfan_next.totloop) {
      *r_mlfan_curr_index = mpfan_next.loopstart;
    }
    *r_mlfan_vert_index = *r_mlfan_curr_index;
  }
}

static void split_loop_nor_single_do(LoopSplitTaskDataCommon *common_data, LoopSplitTaskData *data)
{
  MLoopNorSpaceArray *lnors_spacearr = common_data->lnors_spacearr;
  const Span<short2> clnors_data = common_data->clnors_data;

  const Span<float3> positions = common_data->positions;
  const Span<MEdge> edges = common_data->edges;
  const Span<float3> polynors = common_data->polynors;
  const Span<int> corner_verts = common_data->corner_verts;
  const Span<int> corner_edges = common_data->corner_edges;

  MLoopNorSpace *lnor_space = data->lnor_space;
  float3 *lnor = data->lnor;
  const int ml_curr_index = data->ml_curr_index;
  const int ml_prev_index = data->ml_curr_index;
#if 0 /* Not needed for 'single' loop. */
  const int ml_prev_index = data->ml_prev_index;
  const int *e2l_prev = data->e2l_prev;
#endif
  const int mp_index = data->mp_index;

  /* Simple case (both edges around that vertex are sharp in current polygon),
   * this loop just takes its poly normal.
   */
  copy_v3_v3(*lnor, polynors[mp_index]);

#if 0
  printf("BASIC: handling loop %d / edge %d / vert %d / poly %d\n",
         ml_curr_index,
         ml_curr->e,
         ml_curr->v,
         mp_index);
#endif

  /* If needed, generate this (simple!) lnor space. */
  if (lnors_spacearr) {
    float vec_curr[3], vec_prev[3];

    /* The vertex we are "fanning" around! */
    const int mv_pivot_index = corner_verts[ml_curr_index];
    const float3 &mv_pivot = positions[mv_pivot_index];
    const MEdge *me_curr = &edges[corner_edges[ml_curr_index]];
    const float3 &mv_2 = (me_curr->v1 == mv_pivot_index) ? positions[me_curr->v2] :
                                                           positions[me_curr->v1];
    const MEdge *me_prev = &edges[corner_edges[ml_prev_index]];
    const float3 &mv_3 = (me_prev->v1 == mv_pivot_index) ? positions[me_prev->v2] :
                                                           positions[me_prev->v1];

    sub_v3_v3v3(vec_curr, mv_2, mv_pivot);
    normalize_v3(vec_curr);
    sub_v3_v3v3(vec_prev, mv_3, mv_pivot);
    normalize_v3(vec_prev);

    BKE_lnor_space_define(lnor_space, *lnor, vec_curr, vec_prev, nullptr);
    /* We know there is only one loop in this space, no need to create a link-list in this case. */
    BKE_lnor_space_add_loop(lnors_spacearr, lnor_space, ml_curr_index, nullptr, true);

    if (!clnors_data.is_empty()) {
      BKE_lnor_space_custom_data_to_normal(lnor_space, clnors_data[ml_curr_index], *lnor);
    }
  }
}

static void split_loop_nor_fan_do(LoopSplitTaskDataCommon *common_data, LoopSplitTaskData *data)
{
  MLoopNorSpaceArray *lnors_spacearr = common_data->lnors_spacearr;
  MutableSpan<float3> loopnors = common_data->loopnors;
  MutableSpan<short2> clnors_data = common_data->clnors_data;

  const Span<float3> positions = common_data->positions;
  const Span<MEdge> edges = common_data->edges;
  const Span<MPoly> polys = common_data->polys;
  const Span<int> corner_verts = common_data->corner_verts;
  const Span<int> corner_edges = common_data->corner_edges;
  const int(*edge_to_loops)[2] = common_data->edge_to_loops;
  const Span<int> loop_to_poly = common_data->loop_to_poly;
  const Span<float3> polynors = common_data->polynors;

  MLoopNorSpace *lnor_space = data->lnor_space;
#if 0 /* Not needed for 'fan' loops. */
  float(*lnor)[3] = data->lnor;
#endif
  const int ml_curr_index = data->ml_curr_index;
  const int ml_prev_index = data->ml_prev_index;
  const int mp_index = data->mp_index;
  const int *e2l_prev = data->e2l_prev;

  BLI_Stack *edge_vectors = data->edge_vectors;

  /* Sigh! we have to fan around current vertex, until we find the other non-smooth edge,
   * and accumulate face normals into the vertex!
   * Note in case this vertex has only one sharp edges, this is a waste because the normal is the
   * same as the vertex normal, but I do not see any easy way to detect that (would need to count
   * number of sharp edges per vertex, I doubt the additional memory usage would be worth it,
   * especially as it should not be a common case in real-life meshes anyway). */
  const int mv_pivot_index = corner_verts[ml_curr_index]; /* The vertex we are "fanning" around! */
  const float3 &mv_pivot = positions[mv_pivot_index];

  /* `ml_curr` would be mlfan_prev if we needed that one. */
  const MEdge *me_org = &edges[corner_edges[ml_curr_index]];

  float vec_curr[3], vec_prev[3], vec_org[3];
  float lnor[3] = {0.0f, 0.0f, 0.0f};

  /* We validate clnors data on the fly - cheapest way to do! */
  int clnors_avg[2] = {0, 0};
  short2 *clnor_ref = nullptr;
  int clnors_count = 0;
  bool clnors_invalid = false;

  /* Temp loop normal stack. */
  BLI_SMALLSTACK_DECLARE(normal, float *);
  /* Temp clnors stack. */
  BLI_SMALLSTACK_DECLARE(clnors, short *);

  const int *e2lfan_curr = e2l_prev;
  /* `mlfan_vert_index` the loop of our current edge might not be the loop of our current vertex!
   */
  int mlfan_curr_index = ml_prev_index;
  int mlfan_vert_index = ml_curr_index;
  int mpfan_curr_index = mp_index;

  BLI_assert(mlfan_curr_index >= 0);
  BLI_assert(mlfan_vert_index >= 0);
  BLI_assert(mpfan_curr_index >= 0);

  /* Only need to compute previous edge's vector once, then we can just reuse old current one! */
  {
    const float3 &mv_2 = (me_org->v1 == mv_pivot_index) ? positions[me_org->v2] :
                                                          positions[me_org->v1];

    sub_v3_v3v3(vec_org, mv_2, mv_pivot);
    normalize_v3(vec_org);
    copy_v3_v3(vec_prev, vec_org);

    if (lnors_spacearr) {
      BLI_stack_push(edge_vectors, vec_org);
    }
  }

  // printf("FAN: vert %d, start edge %d\n", mv_pivot_index, ml_curr->e);

  while (true) {
    const MEdge *me_curr = &edges[corner_edges[mlfan_curr_index]];
    /* Compute edge vectors.
     * NOTE: We could pre-compute those into an array, in the first iteration, instead of computing
     *       them twice (or more) here. However, time gained is not worth memory and time lost,
     *       given the fact that this code should not be called that much in real-life meshes.
     */
    {
      const float3 &mv_2 = (me_curr->v1 == mv_pivot_index) ? positions[me_curr->v2] :
                                                             positions[me_curr->v1];

      sub_v3_v3v3(vec_curr, mv_2, mv_pivot);
      normalize_v3(vec_curr);
    }

    // printf("\thandling edge %d / loop %d\n", corner_edges[mlfan_curr_index], mlfan_curr_index);

    {
      /* Code similar to accumulate_vertex_normals_poly_v3. */
      /* Calculate angle between the two poly edges incident on this vertex. */
      const float fac = saacos(dot_v3v3(vec_curr, vec_prev));
      /* Accumulate */
      madd_v3_v3fl(lnor, polynors[mpfan_curr_index], fac);

      if (!clnors_data.is_empty()) {
        /* Accumulate all clnors, if they are not all equal we have to fix that! */
        short2 *clnor = &clnors_data[mlfan_vert_index];
        if (clnors_count) {
          clnors_invalid |= ((*clnor_ref)[0] != (*clnor)[0] || (*clnor_ref)[1] != (*clnor)[1]);
        }
        else {
          clnor_ref = clnor;
        }
        clnors_avg[0] += (*clnor)[0];
        clnors_avg[1] += (*clnor)[1];
        clnors_count++;
        /* We store here a pointer to all custom lnors processed. */
        BLI_SMALLSTACK_PUSH(clnors, (short *)*clnor);
      }
    }

    /* We store here a pointer to all loop-normals processed. */
    BLI_SMALLSTACK_PUSH(normal, (float *)(loopnors[mlfan_vert_index]));

    if (lnors_spacearr) {
      /* Assign current lnor space to current 'vertex' loop. */
      BKE_lnor_space_add_loop(lnors_spacearr, lnor_space, mlfan_vert_index, nullptr, false);
      if (me_curr != me_org) {
        /* We store here all edges-normalized vectors processed. */
        BLI_stack_push(edge_vectors, vec_curr);
      }
    }

    if (IS_EDGE_SHARP(e2lfan_curr) || (me_curr == me_org)) {
      /* Current edge is sharp and we have finished with this fan of faces around this vert,
       * or this vert is smooth, and we have completed a full turn around it. */
      // printf("FAN: Finished!\n");
      break;
    }

    copy_v3_v3(vec_prev, vec_curr);

    /* Find next loop of the smooth fan. */
    loop_manifold_fan_around_vert_next(corner_verts,
                                       polys,
                                       loop_to_poly,
                                       e2lfan_curr,
                                       mv_pivot_index,
                                       &mlfan_curr_index,
                                       &mlfan_vert_index,
                                       &mpfan_curr_index);

    e2lfan_curr = edge_to_loops[corner_edges[mlfan_curr_index]];
  }

  {
    float lnor_len = normalize_v3(lnor);

    /* If we are generating lnor spacearr, we can now define the one for this fan,
     * and optionally compute final lnor from custom data too!
     */
    if (lnors_spacearr) {
      if (UNLIKELY(lnor_len == 0.0f)) {
        /* Use vertex normal as fallback! */
        copy_v3_v3(lnor, loopnors[mlfan_vert_index]);
        lnor_len = 1.0f;
      }

      BKE_lnor_space_define(lnor_space, lnor, vec_org, vec_curr, edge_vectors);

      if (!clnors_data.is_empty()) {
        if (clnors_invalid) {
          short *clnor;

          clnors_avg[0] /= clnors_count;
          clnors_avg[1] /= clnors_count;
          /* Fix/update all clnors of this fan with computed average value. */
          if (G.debug & G_DEBUG) {
            printf("Invalid clnors in this fan!\n");
          }
          while ((clnor = (short *)BLI_SMALLSTACK_POP(clnors))) {
            // print_v2("org clnor", clnor);
            clnor[0] = short(clnors_avg[0]);
            clnor[1] = short(clnors_avg[1]);
          }
          // print_v2("new clnors", clnors_avg);
        }
        /* Extra bonus: since small-stack is local to this function,
         * no more need to empty it at all cost! */

        BKE_lnor_space_custom_data_to_normal(lnor_space, *clnor_ref, lnor);
      }
    }

    /* In case we get a zero normal here, just use vertex normal already set! */
    if (LIKELY(lnor_len != 0.0f)) {
      /* Copy back the final computed normal into all related loop-normals. */
      float *nor;

      while ((nor = (float *)BLI_SMALLSTACK_POP(normal))) {
        copy_v3_v3(nor, lnor);
      }
    }
    /* Extra bonus: since small-stack is local to this function,
     * no more need to empty it at all cost! */
  }
}

static void loop_split_worker_do(LoopSplitTaskDataCommon *common_data,
                                 LoopSplitTaskData *data,
                                 BLI_Stack *edge_vectors)
{
  if (data->e2l_prev) {
    BLI_assert((edge_vectors == nullptr) || BLI_stack_is_empty(edge_vectors));
    data->edge_vectors = edge_vectors;
    split_loop_nor_fan_do(common_data, data);
  }
  else {
    /* No need for edge_vectors for 'single' case! */
    split_loop_nor_single_do(common_data, data);
  }
}

static void loop_split_worker(TaskPool *__restrict pool, void *taskdata)
{
  LoopSplitTaskDataCommon *common_data = (LoopSplitTaskDataCommon *)BLI_task_pool_user_data(pool);
  LoopSplitTaskData *data = (LoopSplitTaskData *)taskdata;

  /* Temp edge vectors stack, only used when computing lnor spacearr. */
  BLI_Stack *edge_vectors = common_data->lnors_spacearr ?
                                BLI_stack_new(sizeof(float[3]), __func__) :
                                nullptr;

  for (int i = 0; i < LOOP_SPLIT_TASK_BLOCK_SIZE; i++, data++) {
    /* A -1 ml_curr_index is used to tag ended data! */
    if (data->ml_curr_index == -1) {
      break;
    }
    loop_split_worker_do(common_data, data, edge_vectors);
  }

  if (edge_vectors) {
    BLI_stack_free(edge_vectors);
  }
}

/**
 * Check whether given loop is part of an unknown-so-far cyclic smooth fan, or not.
 * Needed because cyclic smooth fans have no obvious 'entry point',
 * and yet we need to walk them once, and only once.
 */
static bool loop_split_generator_check_cyclic_smooth_fan(const Span<int> corner_verts,
                                                         const Span<int> corner_edges,
                                                         const Span<MPoly> mpolys,
                                                         const int (*edge_to_loops)[2],
                                                         const Span<int> loop_to_poly,
                                                         const int *e2l_prev,
                                                         BitVector<> &skip_loops,
                                                         const int ml_curr_index,
                                                         const int ml_prev_index,
                                                         const int mp_curr_index)
{
  const int mv_pivot_index = corner_verts[ml_curr_index]; /* The vertex we are "fanning" around! */

  const int *e2lfan_curr = e2l_prev;
  if (IS_EDGE_SHARP(e2lfan_curr)) {
    /* Sharp loop, so not a cyclic smooth fan. */
    return false;
  }

  /* `mlfan_vert_index` the loop of our current edge might not be the loop of our current vertex!
   */
  int mlfan_curr_index = ml_prev_index;
  int mlfan_vert_index = ml_curr_index;
  int mpfan_curr_index = mp_curr_index;

  BLI_assert(mlfan_curr_index >= 0);
  BLI_assert(mlfan_vert_index >= 0);
  BLI_assert(mpfan_curr_index >= 0);

  BLI_assert(!skip_loops[mlfan_vert_index]);
  skip_loops[mlfan_vert_index].set();

  while (true) {
    /* Find next loop of the smooth fan. */
    loop_manifold_fan_around_vert_next(corner_verts,
                                       mpolys,
                                       loop_to_poly,
                                       e2lfan_curr,
                                       mv_pivot_index,
                                       &mlfan_curr_index,
                                       &mlfan_vert_index,
                                       &mpfan_curr_index);

    e2lfan_curr = edge_to_loops[corner_edges[mlfan_curr_index]];

    if (IS_EDGE_SHARP(e2lfan_curr)) {
      /* Sharp loop/edge, so not a cyclic smooth fan. */
      return false;
    }
    /* Smooth loop/edge. */
    if (skip_loops[mlfan_vert_index]) {
      if (mlfan_vert_index == ml_curr_index) {
        /* We walked around a whole cyclic smooth fan without finding any already-processed loop,
         * means we can use initial `ml_curr` / `ml_prev` edge as start for this smooth fan. */
        return true;
      }
      /* Already checked in some previous looping, we can abort. */
      return false;
    }

    /* We can skip it in future, and keep checking the smooth fan. */
    skip_loops[mlfan_vert_index].set();
  }
}

static void loop_split_generator(TaskPool *pool, LoopSplitTaskDataCommon *common_data)
{
  MLoopNorSpaceArray *lnors_spacearr = common_data->lnors_spacearr;
  MutableSpan<float3> loopnors = common_data->loopnors;

  const Span<int> corner_verts = common_data->corner_verts;
  const Span<int> corner_edges = common_data->corner_edges;
  const Span<MPoly> polys = common_data->polys;
  const Span<int> loop_to_poly = common_data->loop_to_poly;
  const int(*edge_to_loops)[2] = common_data->edge_to_loops;

  BitVector<> skip_loops(corner_verts.size(), false);

  LoopSplitTaskData *data_buff = nullptr;
  int data_idx = 0;

  /* Temp edge vectors stack, only used when computing lnor spacearr
   * (and we are not multi-threading). */
  BLI_Stack *edge_vectors = nullptr;

#ifdef DEBUG_TIME
  SCOPED_TIMER_AVERAGED(__func__);
#endif

  if (!pool) {
    if (lnors_spacearr) {
      edge_vectors = BLI_stack_new(sizeof(float[3]), __func__);
    }
  }

  /* We now know edges that can be smoothed (with their vector, and their two loops),
   * and edges that will be hard! Now, time to generate the normals.
   */
  for (const int mp_index : polys.index_range()) {
    const MPoly &poly = polys[mp_index];
    const int ml_last_index = (poly.loopstart + poly.totloop) - 1;
    int ml_curr_index = poly.loopstart;
    int ml_prev_index = ml_last_index;

    float3 *lnors = &loopnors[ml_curr_index];

    for (; ml_curr_index <= ml_last_index; ml_curr_index++, lnors++) {
      const int *e2l_curr = edge_to_loops[corner_edges[ml_curr_index]];
      const int *e2l_prev = edge_to_loops[corner_edges[ml_prev_index]];

#if 0
      printf("Checking loop %d / edge %u / vert %u (sharp edge: %d, skiploop: %d)",
             ml_curr_index,
             corner_edges[ml_curr_index],
             corner_verts[ml_curr_index],
             IS_EDGE_SHARP(e2l_curr),
             skip_loops[ml_curr_index]);
#endif

      /* A smooth edge, we have to check for cyclic smooth fan case.
       * If we find a new, never-processed cyclic smooth fan, we can do it now using that loop/edge
       * as 'entry point', otherwise we can skip it. */

      /* NOTE: In theory, we could make #loop_split_generator_check_cyclic_smooth_fan() store
       * mlfan_vert_index'es and edge indexes in two stacks, to avoid having to fan again around
       * the vert during actual computation of `clnor` & `clnorspace`.
       * However, this would complicate the code, add more memory usage, and despite its logical
       * complexity, #loop_manifold_fan_around_vert_next() is quite cheap in term of CPU cycles,
       * so really think it's not worth it. */
      if (!IS_EDGE_SHARP(e2l_curr) && (skip_loops[ml_curr_index] ||
                                       !loop_split_generator_check_cyclic_smooth_fan(corner_verts,
                                                                                     corner_edges,
                                                                                     polys,
                                                                                     edge_to_loops,
                                                                                     loop_to_poly,
                                                                                     e2l_prev,
                                                                                     skip_loops,
                                                                                     ml_curr_index,
                                                                                     ml_prev_index,
                                                                                     mp_index))) {
        // printf("SKIPPING!\n");
      }
      else {
        LoopSplitTaskData *data, data_local;

        // printf("PROCESSING!\n");

        if (pool) {
          if (data_idx == 0) {
            data_buff = (LoopSplitTaskData *)MEM_calloc_arrayN(
                LOOP_SPLIT_TASK_BLOCK_SIZE, sizeof(*data_buff), __func__);
          }
          for (const int i : blender::IndexRange(LOOP_SPLIT_TASK_BLOCK_SIZE)) {
            /* Used to tag the end of the buffer. */
            data_buff[i].ml_curr_index = -1;
          }
          data = &data_buff[data_idx];
        }
        else {
          data = &data_local;
          memset(data, 0, sizeof(*data));
        }

        if (IS_EDGE_SHARP(e2l_curr) && IS_EDGE_SHARP(e2l_prev)) {
          data->lnor = lnors;
          data->ml_curr_index = ml_curr_index;
#if 0 /* Not needed for 'single' loop. */
          data->ml_prev_index = ml_prev_index;
          data->e2l_prev = nullptr; /* Tag as 'single' task. */
#endif
          data->mp_index = mp_index;
          if (lnors_spacearr) {
            data->lnor_space = BKE_lnor_space_create(lnors_spacearr);
          }
        }
        /* We *do not need* to check/tag loops as already computed!
         * Due to the fact a loop only links to one of its two edges,
         * a same fan *will never be walked more than once!*
         * Since we consider edges having neighbor polys with inverted
         * (flipped) normals as sharp, we are sure that no fan will be skipped,
         * even only considering the case (sharp curr_edge, smooth prev_edge),
         * and not the alternative (smooth curr_edge, sharp prev_edge).
         * All this due/thanks to link between normals and loop ordering (i.e. winding).
         */
        else {
#if 0 /* Not needed for 'fan' loops. */
          data->lnor = lnors;
#endif
          data->ml_curr_index = ml_curr_index;
          data->ml_prev_index = ml_prev_index;
          data->e2l_prev = e2l_prev; /* Also tag as 'fan' task. */
          data->mp_index = mp_index;
          if (lnors_spacearr) {
            data->lnor_space = BKE_lnor_space_create(lnors_spacearr);
          }
        }

        if (pool) {
          data_idx++;
          if (data_idx == LOOP_SPLIT_TASK_BLOCK_SIZE) {
            BLI_task_pool_push(pool, loop_split_worker, data_buff, true, nullptr);
            data_idx = 0;
          }
        }
        else {
          loop_split_worker_do(common_data, data, edge_vectors);
        }
      }

      ml_prev_index = ml_curr_index;
    }
  }

  /* Last block of data. Since it is calloc'ed and we use first nullptr item as stopper,
   * everything is fine. */
  if (pool && data_idx) {
    BLI_task_pool_push(pool, loop_split_worker, data_buff, true, nullptr);
  }

  if (edge_vectors) {
    BLI_stack_free(edge_vectors);
  }
}

void BKE_mesh_normals_loop_split(const float (*positions)[3],
                                 const float (*vert_normals)[3],
                                 const int numVerts,
                                 const MEdge *medges,
                                 const int numEdges,
                                 const int *corner_verts,
                                 const int *corner_edges,
                                 float (*r_loopnors)[3],
                                 const int numLoops,
                                 const MPoly *mpolys,
                                 const float (*polynors)[3],
                                 const int numPolys,
                                 const bool use_split_normals,
                                 const float split_angle,
                                 const int *loop_to_poly_map,
                                 MLoopNorSpaceArray *r_lnors_spacearr,
                                 short (*clnors_data)[2])
{
  using namespace blender;
  using namespace blender::bke;
  /* For now this is not supported.
   * If we do not use split normals, we do not generate anything fancy! */
  BLI_assert(use_split_normals || !(r_lnors_spacearr));

  if (!use_split_normals) {
    /* In this case, we simply fill lnors with vnors (or fnors for flat faces), quite simple!
     * Note this is done here to keep some logic and consistency in this quite complex code,
     * since we may want to use lnors even when mesh's 'autosmooth' is disabled
     * (see e.g. mesh mapping code).
     * As usual, we could handle that on case-by-case basis,
     * but simpler to keep it well confined here. */
    int mp_index;

    for (mp_index = 0; mp_index < numPolys; mp_index++) {
      const MPoly *mp = &mpolys[mp_index];
      int ml_index = mp->loopstart;
      const int ml_index_end = ml_index + mp->totloop;
      const bool is_poly_flat = ((mp->flag & ME_SMOOTH) == 0);

      for (; ml_index < ml_index_end; ml_index++) {
        if (is_poly_flat) {
          copy_v3_v3(r_loopnors[ml_index], polynors[mp_index]);
        }
        else {
          copy_v3_v3(r_loopnors[ml_index], vert_normals[corner_verts[ml_index]]);
        }
      }
    }
    return;
  }

  /**
   * Mapping edge -> loops.
   * If that edge is used by more than two loops (polys),
   * it is always sharp (and tagged as such, see below).
   * We also use the second loop index as a kind of flag:
   *
   * - smooth edge: > 0.
   * - sharp edge: < 0 (INDEX_INVALID || INDEX_UNSET).
   * - unset: INDEX_UNSET.
   *
   * Note that currently we only have two values for second loop of sharp edges.
   * However, if needed, we can store the negated value of loop index instead of INDEX_INVALID
   * to retrieve the real value later in code).
   * Note also that loose edges always have both values set to 0! */
  int(*edge_to_loops)[2] = (int(*)[2])MEM_calloc_arrayN(
      size_t(numEdges), sizeof(*edge_to_loops), __func__);

  /* Simple mapping from a loop to its polygon index. */
  Span<int> loop_to_poly;
  Array<int> local_loop_to_poly_map;
  if (loop_to_poly_map) {
    loop_to_poly = {loop_to_poly_map, numLoops};
  }
  else {
    local_loop_to_poly_map = mesh_topology::build_loop_to_poly_map({mpolys, numPolys}, numLoops);
    loop_to_poly = local_loop_to_poly_map;
  }

  /* When using custom loop normals, disable the angle feature! */
  const bool check_angle = (split_angle < float(M_PI)) && (clnors_data == nullptr);

  MLoopNorSpaceArray _lnors_spacearr = {nullptr};

#ifdef DEBUG_TIME
  SCOPED_TIMER_AVERAGED(__func__);
#endif

  if (!r_lnors_spacearr && clnors_data) {
    /* We need to compute lnor spacearr if some custom lnor data are given to us! */
    r_lnors_spacearr = &_lnors_spacearr;
  }
  if (r_lnors_spacearr) {
    BKE_lnor_spacearr_init(r_lnors_spacearr, numLoops, MLNOR_SPACEARR_LOOP_INDEX);
  }

  /* Init data common to all tasks. */
  LoopSplitTaskDataCommon common_data;
  common_data.lnors_spacearr = r_lnors_spacearr;
  common_data.loopnors = {reinterpret_cast<float3 *>(r_loopnors), numLoops};
  common_data.clnors_data = {reinterpret_cast<short2 *>(clnors_data), clnors_data ? numLoops : 0};
  common_data.positions = {reinterpret_cast<const float3 *>(positions), numVerts};
  common_data.edges = {const_cast<MEdge *>(medges), numEdges};
  common_data.polys = {mpolys, numPolys};
  common_data.corner_verts = {corner_verts, numLoops};
  common_data.corner_edges = {corner_edges, numLoops};
  common_data.edge_to_loops = edge_to_loops;
  common_data.loop_to_poly = loop_to_poly;
  common_data.polynors = {reinterpret_cast<const float3 *>(polynors), numPolys};
  common_data.vert_normals = {reinterpret_cast<const float3 *>(vert_normals), numVerts};

  /* This first loop check which edges are actually smooth, and compute edge vectors. */
  mesh_edges_sharp_tag(&common_data, check_angle, split_angle, false);

  if (numLoops < LOOP_SPLIT_TASK_BLOCK_SIZE * 8) {
    /* Not enough loops to be worth the whole threading overhead. */
    loop_split_generator(nullptr, &common_data);
  }
  else {
    TaskPool *task_pool = BLI_task_pool_create(&common_data, TASK_PRIORITY_HIGH);

    loop_split_generator(task_pool, &common_data);

    BLI_task_pool_work_and_wait(task_pool);

    BLI_task_pool_free(task_pool);
  }

  MEM_freeN(edge_to_loops);

  if (r_lnors_spacearr) {
    if (r_lnors_spacearr == &_lnors_spacearr) {
      BKE_lnor_spacearr_free(r_lnors_spacearr);
    }
  }
}

#undef INDEX_UNSET
#undef INDEX_INVALID
#undef IS_EDGE_SHARP

/**
 * Compute internal representation of given custom normals (as an array of float[2]).
 * It also makes sure the mesh matches those custom normals, by setting sharp edges flag as needed
 * to get a same custom lnor for all loops sharing a same smooth fan.
 * If use_vertices if true, r_custom_loopnors is assumed to be per-vertex, not per-loop
 * (this allows to set whole vert's normals at once, useful in some cases).
 * r_custom_loopnors is expected to have normalized normals, or zero ones,
 * in which case they will be replaced by default loop/vertex normal.
 */
static void mesh_normals_loop_custom_set(const float (*positions)[3],
                                         const float (*vert_normals)[3],
                                         const int numVerts,
                                         MEdge *medges,
                                         const int numEdges,
                                         const int *corner_verts,
                                         const int *corner_edges,
                                         float (*r_custom_loopnors)[3],
                                         const int numLoops,
                                         const MPoly *mpolys,
                                         const float (*polynors)[3],
                                         const int numPolys,
                                         short (*r_clnors_data)[2],
                                         const bool use_vertices)
{
  using namespace blender;
  using namespace blender::bke;
  /* We *may* make that poor #BKE_mesh_normals_loop_split() even more complex by making it handling
   * that feature too, would probably be more efficient in absolute.
   * However, this function *is not* performance-critical, since it is mostly expected to be called
   * by io add-ons when importing custom normals, and modifier
   * (and perhaps from some editing tools later?).
   * So better to keep some simplicity here, and just call #BKE_mesh_normals_loop_split() twice! */
  MLoopNorSpaceArray lnors_spacearr = {nullptr};
  BitVector<> done_loops(numLoops, false);
  float(*lnors)[3] = (float(*)[3])MEM_calloc_arrayN(size_t(numLoops), sizeof(*lnors), __func__);
  const Array<int> loop_to_poly = mesh_topology::build_loop_to_poly_map({mpolys, numPolys},
                                                                        numLoops);
  /* In this case we always consider split nors as ON,
   * and do not want to use angle to define smooth fans! */
  const bool use_split_normals = true;
  const float split_angle = float(M_PI);

  BLI_SMALLSTACK_DECLARE(clnors_data, short *);

  /* Compute current lnor spacearr. */
  BKE_mesh_normals_loop_split(positions,
                              vert_normals,
                              numVerts,
                              medges,
                              numEdges,
                              corner_verts,
                              corner_edges,
                              lnors,
                              numLoops,
                              mpolys,
                              polynors,
                              numPolys,
                              use_split_normals,
                              split_angle,
                              loop_to_poly.data(),
                              &lnors_spacearr,
                              nullptr);

  /* Set all given zero vectors to their default value. */
  if (use_vertices) {
    for (int i = 0; i < numVerts; i++) {
      if (is_zero_v3(r_custom_loopnors[i])) {
        copy_v3_v3(r_custom_loopnors[i], vert_normals[i]);
      }
    }
  }
  else {
    for (int i = 0; i < numLoops; i++) {
      if (is_zero_v3(r_custom_loopnors[i])) {
        copy_v3_v3(r_custom_loopnors[i], lnors[i]);
      }
    }
  }

  BLI_assert(lnors_spacearr.data_type == MLNOR_SPACEARR_LOOP_INDEX);

  /* Now, check each current smooth fan (one lnor space per smooth fan!),
   * and if all its matching custom lnors are not (enough) equal, add sharp edges as needed.
   * This way, next time we run BKE_mesh_normals_loop_split(), we'll get lnor spacearr/smooth fans
   * matching given custom lnors.
   * Note this code *will never* unsharp edges! And quite obviously,
   * when we set custom normals per vertices, running this is absolutely useless. */
  if (!use_vertices) {
    for (int i = 0; i < numLoops; i++) {
      if (!lnors_spacearr.lspacearr[i]) {
        /* This should not happen in theory, but in some rare case (probably ugly geometry)
         * we can get some nullptr loopspacearr at this point. :/
         * Maybe we should set those loops' edges as sharp? */
        done_loops[i].set();
        if (G.debug & G_DEBUG) {
          printf("WARNING! Getting invalid nullptr loop space for loop %d!\n", i);
        }
        continue;
      }

      if (!done_loops[i]) {
        /* Notes:
         * - In case of mono-loop smooth fan, we have nothing to do.
         * - Loops in this linklist are ordered (in reversed order compared to how they were
         *   discovered by BKE_mesh_normals_loop_split(), but this is not a problem).
         *   Which means if we find a mismatching clnor,
         *   we know all remaining loops will have to be in a new, different smooth fan/lnor space.
         * - In smooth fan case, we compare each clnor against a ref one,
         *   to avoid small differences adding up into a real big one in the end!
         */
        if (lnors_spacearr.lspacearr[i]->flags & MLNOR_SPACE_IS_SINGLE) {
          done_loops[i].set();
          continue;
        }

        LinkNode *loops = lnors_spacearr.lspacearr[i]->loops;
        int corner_prev = -1;
        const float *org_nor = nullptr;

        while (loops) {
          const int lidx = POINTER_AS_INT(loops->link);
          float *nor = r_custom_loopnors[lidx];

          if (!org_nor) {
            org_nor = nor;
          }
          else if (dot_v3v3(org_nor, nor) < LNOR_SPACE_TRIGO_THRESHOLD) {
            /* Current normal differs too much from org one, we have to tag the edge between
             * previous loop's face and current's one as sharp.
             * We know those two loops do not point to the same edge,
             * since we do not allow reversed winding in a same smooth fan. */
            const MPoly *mp = &mpolys[loop_to_poly[lidx]];
            const int mlp = (lidx == mp->loopstart) ? mp->loopstart + mp->totloop - 1 : lidx - 1;
            const int edge = corner_edges[lidx];
            const int edge_p = corner_edges[mlp];
            const int prev_edge = corner_edges[corner_prev];
            medges[prev_edge == edge_p ? prev_edge : edge].flag |= ME_SHARP;

            org_nor = nor;
          }

          corner_prev = lidx;
          loops = loops->next;
          done_loops[lidx].set();
        }

        /* We also have to check between last and first loops,
         * otherwise we may miss some sharp edges here!
         * This is just a simplified version of above while loop.
         * See T45984. */
        loops = lnors_spacearr.lspacearr[i]->loops;
        if (loops && org_nor) {
          const int lidx = POINTER_AS_INT(loops->link);
          float *nor = r_custom_loopnors[lidx];

          if (dot_v3v3(org_nor, nor) < LNOR_SPACE_TRIGO_THRESHOLD) {
            const MPoly *mp = &mpolys[loop_to_poly[lidx]];
            const int mlp = (lidx == mp->loopstart) ? mp->loopstart + mp->totloop - 1 : lidx - 1;
            const int edge = corner_edges[lidx];
            const int edge_p = corner_edges[mlp];
            const int prev_edge = corner_edges[corner_prev];
            medges[prev_edge == edge_p ? prev_edge : edge].flag |= ME_SHARP;
          }
        }
      }
    }

    /* And now, recompute our new auto lnors and lnor spacearr! */
    BKE_lnor_spacearr_clear(&lnors_spacearr);
    BKE_mesh_normals_loop_split(positions,
                                vert_normals,
                                numVerts,
                                medges,
                                numEdges,
                                corner_verts,
                                corner_edges,
                                lnors,
                                numLoops,
                                mpolys,
                                polynors,
                                numPolys,
                                use_split_normals,
                                split_angle,
                                loop_to_poly.data(),
                                &lnors_spacearr,
                                nullptr);
  }
  else {
    done_loops.fill(true);
  }

  /* And we just have to convert plain object-space custom normals to our
   * lnor space-encoded ones. */
  for (int i = 0; i < numLoops; i++) {
    if (!lnors_spacearr.lspacearr[i]) {
      done_loops[i].reset();
      if (G.debug & G_DEBUG) {
        printf("WARNING! Still getting invalid nullptr loop space in second loop for loop %d!\n",
               i);
      }
      continue;
    }

    if (done_loops[i]) {
      /* Note we accumulate and average all custom normals in current smooth fan,
       * to avoid getting different clnors data (tiny differences in plain custom normals can
       * give rather huge differences in computed 2D factors). */
      LinkNode *loops = lnors_spacearr.lspacearr[i]->loops;
      if (lnors_spacearr.lspacearr[i]->flags & MLNOR_SPACE_IS_SINGLE) {
        BLI_assert(POINTER_AS_INT(loops) == i);
        const int nidx = use_vertices ? corner_verts[i] : i;
        float *nor = r_custom_loopnors[nidx];

        BKE_lnor_space_custom_normal_to_data(lnors_spacearr.lspacearr[i], nor, r_clnors_data[i]);
        done_loops[i].reset();
      }
      else {
        int avg_nor_count = 0;
        float avg_nor[3];
        short clnor_data_tmp[2], *clnor_data;

        zero_v3(avg_nor);
        while (loops) {
          const int lidx = POINTER_AS_INT(loops->link);
          const int nidx = use_vertices ? corner_verts[lidx] : lidx;
          float *nor = r_custom_loopnors[nidx];

          avg_nor_count++;
          add_v3_v3(avg_nor, nor);
          BLI_SMALLSTACK_PUSH(clnors_data, (short *)r_clnors_data[lidx]);

          loops = loops->next;
          done_loops[lidx].reset();
        }

        mul_v3_fl(avg_nor, 1.0f / float(avg_nor_count));
        BKE_lnor_space_custom_normal_to_data(lnors_spacearr.lspacearr[i], avg_nor, clnor_data_tmp);

        while ((clnor_data = (short *)BLI_SMALLSTACK_POP(clnors_data))) {
          clnor_data[0] = clnor_data_tmp[0];
          clnor_data[1] = clnor_data_tmp[1];
        }
      }
    }
  }

  MEM_freeN(lnors);
  BKE_lnor_spacearr_free(&lnors_spacearr);
}

void BKE_mesh_normals_loop_custom_set(const float (*positions)[3],
                                      const float (*vert_normals)[3],
                                      const int numVerts,
                                      MEdge *medges,
                                      const int numEdges,
                                      const int *corner_verts,
                                      const int *corner_edges,
                                      float (*r_custom_loopnors)[3],
                                      const int numLoops,
                                      const MPoly *mpolys,
                                      const float (*polynors)[3],
                                      const int numPolys,
                                      short (*r_clnors_data)[2])
{
  mesh_normals_loop_custom_set(positions,
                               vert_normals,
                               numVerts,
                               medges,
                               numEdges,
                               corner_verts,
                               corner_edges,
                               r_custom_loopnors,
                               numLoops,
                               mpolys,
                               polynors,
                               numPolys,
                               r_clnors_data,
                               false);
}

void BKE_mesh_normals_loop_custom_from_verts_set(const float (*positions)[3],
                                                 const float (*vert_normals)[3],
                                                 float (*r_custom_vertnors)[3],
                                                 const int numVerts,
                                                 MEdge *medges,
                                                 const int numEdges,
                                                 const int *corner_verts,
                                                 const int *corner_edges,
                                                 const int numLoops,
                                                 const MPoly *mpolys,
                                                 const float (*polynors)[3],
                                                 const int numPolys,
                                                 short (*r_clnors_data)[2])
{
  mesh_normals_loop_custom_set(positions,
                               vert_normals,
                               numVerts,
                               medges,
                               numEdges,
                               corner_verts,
                               corner_edges,
                               r_custom_vertnors,
                               numLoops,
                               mpolys,
                               polynors,
                               numPolys,
                               r_clnors_data,
                               true);
}

static void mesh_set_custom_normals(Mesh *mesh, float (*r_custom_nors)[3], const bool use_vertices)
{
  short(*clnors)[2];
  const int numloops = mesh->totloop;

  clnors = (short(*)[2])CustomData_get_layer(&mesh->ldata, CD_CUSTOMLOOPNORMAL);
  if (clnors != nullptr) {
    memset(clnors, 0, sizeof(*clnors) * size_t(numloops));
  }
  else {
    clnors = (short(*)[2])CustomData_add_layer(
        &mesh->ldata, CD_CUSTOMLOOPNORMAL, CD_SET_DEFAULT, nullptr, numloops);
  }
  const Span<float3> positions = mesh->positions();
  MutableSpan<MEdge> edges = mesh->edges_for_write();
  const Span<MPoly> polys = mesh->polys();

  mesh_normals_loop_custom_set(reinterpret_cast<const float(*)[3]>(positions.data()),
                               BKE_mesh_vertex_normals_ensure(mesh),
                               positions.size(),
                               edges.data(),
                               edges.size(),
                               mesh->corner_verts().data(),
                               mesh->corner_edges().data(),
                               r_custom_nors,
                               mesh->totloop,
                               polys.data(),
                               BKE_mesh_poly_normals_ensure(mesh),
                               polys.size(),
                               clnors,
                               use_vertices);
}

void BKE_mesh_set_custom_normals(Mesh *mesh, float (*r_custom_loopnors)[3])
{
  mesh_set_custom_normals(mesh, r_custom_loopnors, false);
}

void BKE_mesh_set_custom_normals_from_verts(Mesh *mesh, float (*r_custom_vertnors)[3])
{
  mesh_set_custom_normals(mesh, r_custom_vertnors, true);
}

void BKE_mesh_normals_loop_to_vertex(const int numVerts,
                                     const int *corner_verts,
                                     const int numLoops,
                                     const float (*clnors)[3],
                                     float (*r_vert_clnors)[3])
{
  int *vert_loops_count = (int *)MEM_calloc_arrayN(
      size_t(numVerts), sizeof(*vert_loops_count), __func__);

  copy_vn_fl((float *)r_vert_clnors, 3 * numVerts, 0.0f);

  int i;
  for (i = 0; i < numLoops; i++) {
    const int vert_i = corner_verts[i];
    add_v3_v3(r_vert_clnors[vert_i], clnors[i]);
    vert_loops_count[vert_i]++;
  }

  for (i = 0; i < numVerts; i++) {
    mul_v3_fl(r_vert_clnors[i], 1.0f / float(vert_loops_count[i]));
  }

  MEM_freeN(vert_loops_count);
}

#undef LNOR_SPACE_TRIGO_THRESHOLD

/** \} */
