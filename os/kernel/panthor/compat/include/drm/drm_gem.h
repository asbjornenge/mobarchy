/* SPDX-License-Identifier: GPL-2.0 */
/* <drm/drm_gem.h> from 6.1, plus the GEM helpers 6.18 code expects (see compat.h). */
#ifndef MOBARCHY_COMPAT_DRM_GEM_H
#define MOBARCHY_COMPAT_DRM_GEM_H

#include_next <drm/drm_gem.h>
#include <linux/dma-resv.h>
#include <linux/iosys-map.h>

static inline bool drm_gem_is_imported(const struct drm_gem_object *obj)
{
	return !!obj->import_attach;
}

/* 6.18 can pass a private shmem mount; 6.1 always uses the default one. */
static inline int drm_gem_object_init_with_mnt(struct drm_device *dev, struct drm_gem_object *obj,
					       size_t size, struct vfsmount *gemfs)
{
	return drm_gem_object_init(dev, obj, size);
}

/* Undoes drm_gem_private_object_init(); in 6.1 that only set up the reservation object. */
static inline void drm_gem_private_object_fini(struct drm_gem_object *obj)
{
	dma_resv_fini(&obj->_resv);
}

/* 6.18 drm_gem_vmap()/vunmap(): take the reservation lock (6.1's are internal and don't). */
static inline int drm_gem_vmap_locked(struct drm_gem_object *obj, struct iosys_map *map)
{
	int ret;

	dma_resv_assert_held(obj->resv);
	if (!obj->funcs->vmap)
		return -EOPNOTSUPP;
	ret = obj->funcs->vmap(obj, map);
	if (ret)
		return ret;
	return iosys_map_is_null(map) ? -ENOMEM : 0;
}

static inline void drm_gem_vunmap_locked(struct drm_gem_object *obj, struct iosys_map *map)
{
	dma_resv_assert_held(obj->resv);
	if (iosys_map_is_null(map))
		return;
	if (obj->funcs->vunmap)
		obj->funcs->vunmap(obj, map);
	iosys_map_clear(map);
}

static inline int mob_drm_gem_vmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	int ret;

	dma_resv_lock(obj->resv, NULL);
	ret = drm_gem_vmap_locked(obj, map);
	dma_resv_unlock(obj->resv);
	return ret;
}
#define drm_gem_vmap mob_drm_gem_vmap

static inline void mob_drm_gem_vunmap(struct drm_gem_object *obj, struct iosys_map *map)
{
	dma_resv_lock(obj->resv, NULL);
	drm_gem_vunmap_locked(obj, map);
	dma_resv_unlock(obj->resv);
}
#define drm_gem_vunmap mob_drm_gem_vunmap

/* fdinfo memory accounting (6.5+). The type is kept so the stats code compiles; the
 * fdinfo output itself is patched out. */
enum drm_gem_object_status {
	DRM_GEM_OBJECT_RESIDENT  = BIT(0),
	DRM_GEM_OBJECT_PURGEABLE = BIT(1),
	DRM_GEM_OBJECT_ACTIVE    = BIT(2),
};

#endif
