
#pragma BLENDER_REQUIRE(common_math_lib.glsl)
#pragma BLENDER_REQUIRE(common_math_geom_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_shader_shared.hh)

/* ---------------------------------------------------------------------- */
/** \name Tilemap data
 * \{ */

/** Decoded tile data structure. */
struct ShadowTileData {
  /** Page inside the virtual shadow map atlas. */
  uvec2 page;
  /** If not 0, offset to the tilemap that has a valid page for this position. (cubemap only) */
  uint lod_tilemap_offset;
  /** Set to true during the setup phase if the tile is inside the view frustum. */
  bool is_visible;
  /** If the tile is needed for rendering. */
  bool is_used;
  /** True if the page points to a valid page. */
  bool is_allocated;
  /** True if an update is needed. */
  bool do_update;
};

#define SHADOW_TILE_NO_DATA 0u
#define SHADOW_TILE_IS_ALLOCATED (1u << 28u)
#define SHADOW_TILE_DO_UPDATE (1u << 29u)
#define SHADOW_TILE_IS_VISIBLE (1u << 30u)
#define SHADOW_TILE_IS_USED (1u << 31u)

ShadowTileData shadow_tile_data_unpack(uint data)
{
  ShadowTileData tile;
  tile.page.x = data & 0xFFu;
  tile.page.y = (data >> 8u) & 0xFFu;
  tile.lod_tilemap_offset = (data >> 16u) & 0xFu;
  tile.is_visible = flag_test(data, SHADOW_TILE_IS_VISIBLE);
  tile.is_used = flag_test(data, SHADOW_TILE_IS_USED);
  tile.is_allocated = flag_test(data, SHADOW_TILE_IS_ALLOCATED);
  tile.do_update = flag_test(data, SHADOW_TILE_DO_UPDATE);
  return tile;
}

uint shadow_tile_data_pack(ShadowTileData tile)
{
  uint data;
  data = tile.page.x;
  data |= tile.page.y << 8u;
  data |= tile.lod_tilemap_offset << 16u;
  set_flag_from_test(data, tile.is_visible, SHADOW_TILE_IS_VISIBLE);
  set_flag_from_test(data, tile.is_used, SHADOW_TILE_IS_USED);
  set_flag_from_test(data, tile.is_allocated, SHADOW_TILE_IS_ALLOCATED);
  set_flag_from_test(data, tile.do_update, SHADOW_TILE_DO_UPDATE);
  return data;
}

int shadow_tile_index(ivec2 tile)
{
  return tile.x + tile.y * SHADOW_TILEMAP_RES;
}

ivec2 shadow_tile_coord(int tile_index)
{
  return ivec2(tile_index % SHADOW_TILEMAP_RES, tile_index / SHADOW_TILEMAP_RES);
}

/* Return bottom left pixel position of the tilemap inside the tilemap atlas. */
ivec2 shadow_tilemap_start(int tilemap_index)
{
  return SHADOW_TILEMAP_RES *
         ivec2(tilemap_index % SHADOW_TILEMAP_PER_ROW, tilemap_index / SHADOW_TILEMAP_PER_ROW);
}

ivec2 shadow_tile_coord_in_atlas(ivec2 tile, int tilemap_index)
{
  return shadow_tilemap_start(tilemap_index) + tile;
}

/** \} */

/* ---------------------------------------------------------------------- */
/** \name Load / Store functions.
 * \{ */

void shadow_tile_store(restrict uimage2D tilemaps_img,
                       ivec2 tile_co,
                       int tilemap_index,
                       ShadowTileData data)
{
  uint tile_data = shadow_tile_data_pack(data);
  imageStore(tilemaps_img, shadow_tile_coord_in_atlas(tile_co, tilemap_index), uvec4(tile_data));
}
/* Ugly define because some compilers seems to not like the fact the imageAtomicOr is inside
 * a function. */
#define shadow_tile_set_flag(tilemaps_img, tile_co, tilemap_index, flag) \
  imageAtomicOr(tilemaps_img, shadow_tile_coord_in_atlas(tile_co, tilemap_index), flag)

ShadowTileData shadow_tile_load(restrict uimage2D tilemaps_img, ivec2 tile_co, int tilemap_index)
{
  uint tile_data = SHADOW_TILE_NO_DATA;
  if (in_range_inclusive(tile_co, ivec2(0), ivec2(SHADOW_TILEMAP_RES - 1))) {
    tile_data = imageLoad(tilemaps_img, shadow_tile_coord_in_atlas(tile_co, tilemap_index)).x;
  }
  return shadow_tile_data_unpack(tile_data);
}

ShadowTileData shadow_tile_load(usampler2D tilemaps_tx, ivec2 tile_co, int tilemap_index)
{
  uint tile_data = SHADOW_TILE_NO_DATA;
  if (in_range_inclusive(tile_co, ivec2(0), ivec2(SHADOW_TILEMAP_RES - 1))) {
    tile_data = texelFetch(tilemaps_tx, shadow_tile_coord_in_atlas(tile_co, tilemap_index), 0).x;
  }
  return shadow_tile_data_unpack(tile_data);
}

/* This function should be the inverse of ShadowTileMap::tilemap_coverage_get. */
int shadow_directional_clipmap_level(ShadowData shadow, float distance_to_camera)
{
  /* Why do we need to bias by 2 here? I don't know... */
  int clipmap_lod = int(ceil(log2(distance_to_camera))) + 2;
  return clamp(clipmap_lod, shadow.clipmap_lod_min, shadow.clipmap_lod_max);
}

/** \} */

/* ---------------------------------------------------------------------- */
/** \name Frustum shapes.
 * \{ */

vec3 shadow_tile_corner_persp(ShadowTileMapData tilemap, ivec2 tile)
{
  return tilemap.corners[1].xyz + tilemap.corners[2].xyz * float(tile.x) +
         tilemap.corners[3].xyz * float(tile.y);
}

Pyramid shadow_tilemap_cubeface_bounds(ShadowTileMapData tilemap,
                                       ivec2 tile_start,
                                       const ivec2 extent)
{
  Pyramid shape;
  shape.corners[0] = tilemap.corners[0].xyz;
  shape.corners[1] = shadow_tile_corner_persp(tilemap, tile_start + ivec2(0, 0));
  shape.corners[2] = shadow_tile_corner_persp(tilemap, tile_start + ivec2(extent.x, 0));
  shape.corners[3] = shadow_tile_corner_persp(tilemap, tile_start + extent);
  shape.corners[4] = shadow_tile_corner_persp(tilemap, tile_start + ivec2(0, extent.y));
  return shape;
}

vec3 shadow_tile_corner_ortho(ShadowTileMapData tilemap, ivec2 tile, const bool far)
{
  return tilemap.corners[0].xyz + tilemap.corners[1].xyz * float(tile.x) +
         tilemap.corners[2].xyz * float(tile.y) + tilemap.corners[3].xyz * float(far);
}

Box shadow_tilemap_clipmap_bounds(ShadowTileMapData tilemap, ivec2 tile_start, const ivec2 extent)
{
  Box shape;
  shape.corners[0] = shadow_tile_corner_ortho(tilemap, tile_start + ivec2(0, 0), false);
  shape.corners[1] = shadow_tile_corner_ortho(tilemap, tile_start + ivec2(extent.x, 0), false);
  shape.corners[2] = shadow_tile_corner_ortho(tilemap, tile_start + extent, false);
  shape.corners[3] = shadow_tile_corner_ortho(tilemap, tile_start + ivec2(0, extent.y), false);
  shape.corners[4] = shadow_tile_corner_ortho(tilemap, tile_start + ivec2(0, 0), true);
  shape.corners[5] = shadow_tile_corner_ortho(tilemap, tile_start + ivec2(extent.x, 0), true);
  shape.corners[6] = shadow_tile_corner_ortho(tilemap, tile_start + extent, true);
  shape.corners[7] = shadow_tile_corner_ortho(tilemap, tile_start + ivec2(0, extent.y), true);
  return shape;
}

/** \} */
