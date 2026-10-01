/* SPDX-License-Identifier: GPL-2.0 */
/* <drm/drm_file.h> from 6.1, plus what 6.18 code expects (see compat.h). */
#ifndef MOBARCHY_COMPAT_DRM_FILE_H
#define MOBARCHY_COMPAT_DRM_FILE_H

#include_next <drm/drm_file.h>

/* fdinfo memory stats (6.5+): kept as a type; the fdinfo output is patched out. */
struct drm_memory_stats {
	u64 shared;
	u64 private;
	u64 resident;
	u64 purgeable;
	u64 active;
};

/* drm_file.client_id (6.12+) only labels debug output: use the file pointer. */
#define mob_drm_file_client_id(file) ((u64)(uintptr_t)(file))

#endif
