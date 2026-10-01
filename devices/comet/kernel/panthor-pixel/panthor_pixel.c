// SPDX-License-Identifier: GPL-2.0
/*
 * panthor_pixel: prepares comet's Mali-G715 for the Panthor driver (ADR 0008).
 *
 * The Pixel device tree describes the GPU for Arm's kbase driver. This module adapts it,
 * at runtime and without touching the DT, to what Panthor expects:
 *
 * - Clock: the GPU frequency is set through Google's cal-if (ACPM), not the common clock
 *   framework. A clock is registered for the GPU device whose rate is the shader clock;
 *   setting it moves both GPU clock domains (top and shaders) to the matching row of the
 *   DVFS table, in the same order kbase does. ACPM sets the voltage with the frequency.
 * - OPP table: built from the DT's gpu_dvfs_table_v2, one OPP per row (shader clock).
 * - Power domains: the GPU has two ("top", "cores"). The PM core only attaches a single
 *   domain by itself, so both are attached here and linked to the GPU device for runtime
 *   PM, and so is the GPU's S2MPU (pKVM stage-2 MPU), which must be powered while the GPU
 *   does DMA.
 * - Bus protection: after every power-up of the top domain, a secure-monitor call sets
 *   the SoC's protection config for the GPU master (as kbase does); without it the
 *   GPU's memory accesses fail with bus errors.
 * - Binding: driver_override = "panthor", since the node's compatible is the legacy
 *   "arm,malit6xx".
 *
 * kbase must not be bound to the GPU when this module loads. Unloading undoes everything.
 */
#include <linux/arm-smccc.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

/* Google cal-if (cmupmucal.ko); rates in kHz. */
extern int cal_dfs_set_rate(unsigned int id, unsigned long rate);
extern unsigned long cal_dfs_get_rate(unsigned int id);

#define GPU_COMPATIBLE	"arm,malit6xx"

/*
 * Bring-up aid: keep the GPU's power domains and S2MPU on while this module is loaded.
 * A driver that gives up with the MCU still running then leaves a powered GPU behind,
 * instead of one that loses power mid-transaction (which took the phone down).
 */
static bool always_on;
module_param(always_on, bool, 0444);
MODULE_PARM_DESC(always_on, "keep the GPU power domains and S2MPU on (bring-up)");

/*
 * The secure monitor must set the SoC's protection config for the GPU as a bus master
 * after every power-up of the GPU's top domain, and clear it before every power-down, as
 * kbase does (pixel_gpu_power.c). Without the first, every GPU memory access fails with
 * a bus error and the CSF firmware never boots; without the second, the power domain's
 * disable sequence hangs (hard lockup in blkpwr_g3d).
 */
#define SMC_PROTECTION_SET	0x82002010
#define PROT_G3D		12
#define SMC_PROTECTION_DISABLE	0
#define SMC_PROTECTION_ENABLE	1
#define MAX_LEVELS	16

struct level {
	unsigned long top_khz;
	unsigned long shaders_khz;
};

static struct {
	struct platform_device *pdev;
	struct device *dev;
	u32 cal_top, cal_shaders;
	struct level levels[MAX_LEVELS];
	int nlevels;
	int cur;			/* index into levels, -1 if unknown */
	struct clk_hw hw;
	bool clk_registered;
	struct clk_lookup *clkdev;
	bool opps_added;
	struct device *pd[2];
	struct device_link *pd_link[2];
	struct device *s2mpu;
	struct device_link *s2mpu_link;
	bool held;		/* always_on: runtime PM references on the suppliers */
	struct notifier_block top_nb;
	bool top_nb_added;
} g;

static void set_gpu_protection(bool enable)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SMC_PROTECTION_SET, 0, PROT_G3D,
		      enable ? SMC_PROTECTION_ENABLE : SMC_PROTECTION_DISABLE, 0, 0, 0, 0, &res);
	if (res.a0)
		dev_err(g.dev, "SMC_PROTECTION_SET(G3D, %d) failed: %#lx\n", enable, res.a0);
}

static int top_domain_notify(struct notifier_block *nb, unsigned long action, void *data)
{
	if (action == GENPD_NOTIFY_ON)
		set_gpu_protection(true);
	else if (action == GENPD_NOTIFY_PRE_OFF)
		set_gpu_protection(false);
	return NOTIFY_OK;
}

static const char *const pd_names[] = { "top", "cores" };

/* --- clock: cal-if behind the common clock framework ---------------------------------- */

static int level_for_rate(unsigned long rate_hz)
{
	int i, best = g.nlevels - 1;	/* the table is sorted fastest first */

	for (i = g.nlevels - 1; i >= 0; i--)
		if (g.levels[i].shaders_khz * 1000UL <= rate_hz)
			best = i;
	return best;
}

static unsigned long pp_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	return cal_dfs_get_rate(g.cal_shaders) * 1000UL;
}

static long pp_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *parent_rate)
{
	return g.levels[level_for_rate(rate)].shaders_khz * 1000UL;
}

static int pp_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	int next = level_for_rate(rate);
	const struct level *l = &g.levels[next];
	bool up = g.cur < 0 || next < g.cur;	/* lower index = faster */
	int ret;

	/* Like kbase: raise the shaders before the top level, lower them after it. */
	if (up) {
		ret = cal_dfs_set_rate(g.cal_shaders, l->shaders_khz);
		if (!ret)
			ret = cal_dfs_set_rate(g.cal_top, l->top_khz);
	} else {
		ret = cal_dfs_set_rate(g.cal_top, l->top_khz);
		if (!ret)
			ret = cal_dfs_set_rate(g.cal_shaders, l->shaders_khz);
	}
	if (ret) {
		dev_err(g.dev, "cal_dfs_set_rate to %lu/%lu kHz failed: %d\n",
			l->top_khz, l->shaders_khz, ret);
		return ret;
	}
	g.cur = next;
	return 0;
}

static const struct clk_ops pp_clk_ops = {
	.recalc_rate = pp_recalc_rate,
	.round_rate = pp_round_rate,
	.set_rate = pp_set_rate,
};

/* --- setup ------------------------------------------------------------------------- */

static int read_dvfs_table(struct device_node *np)
{
	u32 size[2], *t;
	int rows, cols, i, ret;

	ret = of_property_read_u32(np, "gpu0_cmu_cal_id", &g.cal_top);
	ret = ret ?: of_property_read_u32(np, "gpu1_cmu_cal_id", &g.cal_shaders);
	ret = ret ?: of_property_read_u32_array(np, "gpu_dvfs_table_size_v2", size, 2);
	if (ret)
		return dev_err_probe(g.dev, ret, "missing cal ids or DVFS table size in DT\n");

	rows = size[0];
	cols = size[1];
	if (rows < 1 || rows > MAX_LEVELS || cols < 2)
		return dev_err_probe(g.dev, -EINVAL, "bad DVFS table size %dx%d\n", rows, cols);

	t = kcalloc(rows * cols, sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	ret = of_property_read_u32_array(np, "gpu_dvfs_table_v2", t, rows * cols);
	if (!ret) {
		/* Row: top kHz, shaders kHz, then governor/QoS columns we don't use. */
		for (i = 0; i < rows; i++) {
			g.levels[i].top_khz = t[i * cols + 0];
			g.levels[i].shaders_khz = t[i * cols + 1];
		}
		g.nlevels = rows;
	}
	kfree(t);
	return ret;
}

static void teardown(void)
{
	int i;

	/* Power down (if we held it on) while the notifier still clears the protection. */
	if (g.held) {
		pm_runtime_put_sync(g.s2mpu);
		pm_runtime_put_sync(g.pd[1]);
		pm_runtime_put_sync(g.pd[0]);
	}
	if (g.top_nb_added)
		dev_pm_genpd_remove_notifier(g.pd[0]);
	if (g.dev && g.pdev->driver_override)
		driver_set_override(g.dev, &g.pdev->driver_override, "", 0);
	if (g.s2mpu_link)
		device_link_del(g.s2mpu_link);
	if (g.s2mpu)
		put_device(g.s2mpu);
	for (i = ARRAY_SIZE(g.pd) - 1; i >= 0; i--) {
		if (g.pd_link[i])
			device_link_del(g.pd_link[i]);
		if (!IS_ERR_OR_NULL(g.pd[i]))
			dev_pm_domain_detach(g.pd[i], true);
	}
	if (g.opps_added)
		dev_pm_opp_remove_all_dynamic(g.dev);
	if (g.clkdev)
		clkdev_drop(g.clkdev);
	if (g.clk_registered)
		clk_hw_unregister(&g.hw);
	if (g.pdev)
		put_device(&g.pdev->dev);
	memset(&g, 0, sizeof(g));
}

static int __init panthor_pixel_init(void)
{
	struct clk_init_data init = { .name = "comet-gpu", .ops = &pp_clk_ops };
	struct device_node *np, *s2np;
	struct platform_device *s2pdev;
	int i, ret;

	np = of_find_compatible_node(NULL, NULL, GPU_COMPATIBLE);
	if (!np)
		return -ENODEV;
	g.pdev = of_find_device_by_node(np);
	if (!g.pdev) {
		of_node_put(np);
		return -EPROBE_DEFER;
	}
	g.dev = &g.pdev->dev;
	g.cur = -1;

	if (g.dev->driver) {
		dev_err(g.dev, "bound to %s; unbind it before loading panthor_pixel\n",
			g.dev->driver->name);
		ret = -EBUSY;
		goto err;
	}

	ret = read_dvfs_table(np);
	if (ret)
		goto err;

	g.hw.init = &init;
	ret = clk_hw_register(NULL, &g.hw);
	if (ret)
		goto err;
	g.clk_registered = true;
	g.clkdev = clkdev_hw_create(&g.hw, NULL, "%s", dev_name(g.dev));
	if (!g.clkdev) {
		ret = -ENOMEM;
		goto err;
	}

	for (i = 0; i < g.nlevels; i++) {
		ret = dev_pm_opp_add(g.dev, g.levels[i].shaders_khz * 1000UL, 0);
		if (ret)
			goto err;
		g.opps_added = true;
	}

	for (i = 0; i < ARRAY_SIZE(pd_names); i++) {
		g.pd[i] = dev_pm_domain_attach_by_name(g.dev, pd_names[i]);
		if (IS_ERR_OR_NULL(g.pd[i])) {
			ret = g.pd[i] ? PTR_ERR(g.pd[i]) : -ENODEV;
			dev_err(g.dev, "attaching power domain %s: %d\n", pd_names[i], ret);
			goto err;
		}
		g.pd_link[i] = device_link_add(g.dev, g.pd[i],
					       DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
		if (!g.pd_link[i]) {
			ret = -EINVAL;
			goto err;
		}
	}

	/* Protection config on every power-up of the top domain, and now if it is on. */
	g.top_nb.notifier_call = top_domain_notify;
	ret = dev_pm_genpd_add_notifier(g.pd[0], &g.top_nb);
	if (ret) {
		dev_err(g.dev, "genpd notifier on the top domain: %d\n", ret);
		goto err;
	}
	g.top_nb_added = true;

	s2np = of_parse_phandle(np, "s2mpus", 0);
	s2pdev = s2np ? of_find_device_by_node(s2np) : NULL;
	of_node_put(s2np);
	if (!s2pdev) {
		dev_err(g.dev, "no S2MPU device for the GPU\n");
		ret = -ENODEV;
		goto err;
	}
	g.s2mpu = &s2pdev->dev;
	g.s2mpu_link = device_link_add(g.dev, g.s2mpu, DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
	if (!g.s2mpu_link) {
		ret = -EINVAL;
		goto err;
	}

	if (always_on) {
		/* Same order as kbase powers up: top, cores, then the S2MPU. */
		pm_runtime_get_sync(g.pd[0]);
		pm_runtime_get_sync(g.pd[1]);
		pm_runtime_get_sync(g.s2mpu);
		g.held = true;
	}
	if (pm_runtime_active(g.pd[0]))
		set_gpu_protection(true);

	ret = driver_set_override(g.dev, &g.pdev->driver_override, "panthor", strlen("panthor"));
	if (ret)
		goto err;

	of_node_put(np);
	dev_info(g.dev, "ready for panthor: %d levels %lu-%lu kHz, cal ids %#x/%#x, s2mpu %s%s\n",
		 g.nlevels, g.levels[g.nlevels - 1].shaders_khz, g.levels[0].shaders_khz,
		 g.cal_top, g.cal_shaders, dev_name(g.s2mpu), always_on ? ", always on" : "");
	return 0;

err:
	of_node_put(np);
	teardown();
	return ret;
}

static void __exit panthor_pixel_exit(void)
{
	teardown();
}

module_init(panthor_pixel_init);
module_exit(panthor_pixel_exit);
MODULE_DESCRIPTION("Pixel (comet) platform glue for the Panthor GPU driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: cmupmucal");
