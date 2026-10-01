/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Compatibility layer: lets the 6.18 Panthor driver and the DRM helpers it bundles build
 * against a 6.1 kernel. Force-included into every file of the module (see Kbuild), before
 * any kernel header, so it only defines macros here; anything that needs kernel types
 * lives in the headers it points to.
 */
#ifndef MOBARCHY_PANTHOR_COMPAT_H
#define MOBARCHY_PANTHOR_COMPAT_H

#include <linux/version.h>

/*
 * The 6.1 core has an older GEM shmem helper built in, exporting the same names. Ours is
 * private to the module: rename every public identifier.
 */
#define drm_gem_shmem_create			mob_drm_gem_shmem_create
#define drm_gem_shmem_create_with_handle	mob_drm_gem_shmem_create_with_handle
#define drm_gem_shmem_create_with_mnt		mob_drm_gem_shmem_create_with_mnt
#define drm_gem_shmem_dumb_create		mob_drm_gem_shmem_dumb_create
#define drm_gem_shmem_fault			mob_drm_gem_shmem_fault
#define drm_gem_shmem_free			mob_drm_gem_shmem_free
#define drm_gem_shmem_funcs			mob_drm_gem_shmem_funcs
#define drm_gem_shmem_get_pages_locked		mob_drm_gem_shmem_get_pages_locked
#define drm_gem_shmem_get_pages_sgt		mob_drm_gem_shmem_get_pages_sgt
#define drm_gem_shmem_get_pages_sgt_locked	mob_drm_gem_shmem_get_pages_sgt_locked
#define drm_gem_shmem_get_sg_table		mob_drm_gem_shmem_get_sg_table
#define drm_gem_shmem_is_purgeable		mob_drm_gem_shmem_is_purgeable
#define drm_gem_shmem_madvise			mob_drm_gem_shmem_madvise
#define drm_gem_shmem_madvise_locked		mob_drm_gem_shmem_madvise_locked
#define drm_gem_shmem_mmap			mob_drm_gem_shmem_mmap
#define drm_gem_shmem_object_free		mob_drm_gem_shmem_object_free
#define drm_gem_shmem_object_get_sg_table	mob_drm_gem_shmem_object_get_sg_table
#define drm_gem_shmem_object_mmap		mob_drm_gem_shmem_object_mmap
#define drm_gem_shmem_object_pin		mob_drm_gem_shmem_object_pin
#define drm_gem_shmem_object_print_info		mob_drm_gem_shmem_object_print_info
#define drm_gem_shmem_object_unpin		mob_drm_gem_shmem_object_unpin
#define drm_gem_shmem_object_vmap		mob_drm_gem_shmem_object_vmap
#define drm_gem_shmem_object_vunmap		mob_drm_gem_shmem_object_vunmap
#define drm_gem_shmem_pin			mob_drm_gem_shmem_pin
#define drm_gem_shmem_pin_locked		mob_drm_gem_shmem_pin_locked
#define drm_gem_shmem_prime_import_no_map	mob_drm_gem_shmem_prime_import_no_map
#define drm_gem_shmem_prime_import_sg_table	mob_drm_gem_shmem_prime_import_sg_table
#define drm_gem_shmem_print_info		mob_drm_gem_shmem_print_info
#define drm_gem_shmem_purge			mob_drm_gem_shmem_purge
#define drm_gem_shmem_purge_locked		mob_drm_gem_shmem_purge_locked
#define drm_gem_shmem_put_pages_locked		mob_drm_gem_shmem_put_pages_locked
#define drm_gem_shmem_unpin			mob_drm_gem_shmem_unpin
#define drm_gem_shmem_unpin_locked		mob_drm_gem_shmem_unpin_locked
#define drm_gem_shmem_vmap			mob_drm_gem_shmem_vmap
#define drm_gem_shmem_vmap_locked		mob_drm_gem_shmem_vmap_locked
#define drm_gem_shmem_vm_close			mob_drm_gem_shmem_vm_close
#define drm_gem_shmem_vm_open			mob_drm_gem_shmem_vm_open
#define drm_gem_shmem_vm_ops			mob_drm_gem_shmem_vm_ops
#define drm_gem_shmem_vunmap			mob_drm_gem_shmem_vunmap
#define drm_gem_shmem_vunmap_locked		mob_drm_gem_shmem_vunmap_locked
#define __drm_gem_shmem_create			mob___drm_gem_shmem_create

/*
 * 6.5+ keeps the GPUVM bookkeeping of a GEM object in drm_gem_object.gpuva, which 6.1
 * lacks and we can't add (the struct belongs to the core). Every object this module puts
 * into a GPUVM is one of its shmem objects, so the fields live in our (bundled)
 * struct drm_gem_shmem_object instead, initialized when the object is created.
 */
#define mob_gpuva(obj) \
	(&container_of((obj), struct drm_gem_shmem_object, base)->gpuva)

#define drm_gem_gpuva_init(obj) do { } while (0)	/* done in __drm_gem_shmem_create */

#ifdef CONFIG_LOCKDEP
#define drm_gem_gpuva_assert_lock_held(gpuvm, obj) \
	lockdep_assert(drm_gpuvm_immediate_mode(gpuvm) ? \
		       lockdep_is_held(&mob_gpuva(obj)->lock) : \
		       dma_resv_held((obj)->resv))
#else
#define drm_gem_gpuva_assert_lock_held(gpuvm, obj) do { } while (0)
#endif

#define drm_gem_for_each_gpuvm_bo(entry__, obj__) \
	list_for_each_entry(entry__, &mob_gpuva(obj__)->list, list.entry.gem)
#define drm_gem_for_each_gpuvm_bo_safe(entry__, next__, obj__) \
	list_for_each_entry_safe(entry__, next__, &mob_gpuva(obj__)->list, list.entry.gem)

/* Fence deadlines (6.3) are a hint for the fence's signaller; 6.1 has none. */
#define dma_fence_set_deadline(fence, deadline) do { (void)(fence); (void)(deadline); } while (0)

/* Driver feature flag (6.5+) that only enables core GPUVA bookkeeping we do ourselves. */
#define DRIVER_GEM_GPUVA 0

/* disable_work_sync() (6.10) also blocks future queueing; at the points the driver uses
 * it (teardown), cancelling is equivalent. */
#define disable_work_sync(w) cancel_work_sync(w)

#endif
