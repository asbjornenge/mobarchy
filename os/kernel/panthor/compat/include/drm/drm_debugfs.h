/* SPDX-License-Identifier: GPL-2.0 */
/* <drm/drm_debugfs.h> from 6.1, plus the GPUVA debugfs helper from 6.6 (see compat.h). */
#ifndef MOBARCHY_COMPAT_DRM_DEBUGFS_H
#define MOBARCHY_COMPAT_DRM_DEBUGFS_H

#include_next <drm/drm_debugfs.h>
#include <linux/seq_file.h>
#include <drm/drm_gpuvm.h>

#define DRM_DEBUGFS_GPUVA_INFO(show, data) { "gpuvas", show, 0, data }

/* Prints the mappings of a GPU VA space, like the 6.6+ core helper. */
static inline int drm_debugfs_gpuva_info(struct seq_file *m, struct drm_gpuvm *gpuvm)
{
	struct drm_gpuva *va, *kva = &gpuvm->kernel_alloc_node;

	if (!gpuvm->name)
		return -ENODEV;

	seq_printf(m, "DRM GPU VA space (%s) [0x%016llx;0x%016llx]\n",
		   gpuvm->name, gpuvm->mm_start, gpuvm->mm_start + gpuvm->mm_range);
	seq_printf(m, "Kernel reserved node [0x%016llx;0x%016llx]\n\n",
		   kva->va.addr, kva->va.addr + kva->va.range);
	seq_puts(m, " VAs | start              | range              | end                | object             | object offset\n");
	seq_puts(m, "-------------------------------------------------------------------------------------------------------------\n");
	drm_gpuvm_for_each_va(va, gpuvm) {
		if (unlikely(va == kva))
			continue;
		seq_printf(m, "     | 0x%016llx | 0x%016llx | 0x%016llx | 0x%016llx | 0x%016llx\n",
			   va->va.addr, va->va.range, va->va.addr + va->va.range,
			   (u64)(uintptr_t)va->gem.obj, va->gem.offset);
	}
	return 0;
}

#endif
