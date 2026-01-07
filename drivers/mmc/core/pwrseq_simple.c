// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 *  Simple MMC power sequence management
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_simple {
	struct mmc_pwrseq pwrseq;
	bool clk_enabled;
	u32 post_power_on_delay_ms;
	u32 power_off_delay_us;
	struct clk *ext_clk;
	struct gpio_descs *reset_gpios;
	struct regulator *vref;
};

#define to_pwrseq_simple(p) container_of(p, struct mmc_pwrseq_simple, pwrseq)

static void mmc_pwrseq_simple_set_gpios_value(struct mmc_pwrseq_simple *pwrseq,
					      int value)
{
	struct gpio_descs *reset_gpios = pwrseq->reset_gpios;

	if (!IS_ERR(reset_gpios)) {
		unsigned long *values;
		int nvalues = reset_gpios->ndescs;

		values = bitmap_alloc(nvalues, GFP_KERNEL);
		if (!values)
			return;

		if (value)
			bitmap_fill(values, nvalues);
		else
			bitmap_zero(values, nvalues);

		gpiod_set_array_value_cansleep(nvalues, reset_gpios->desc,
					       reset_gpios->info, values);

		bitmap_free(values);
	}
}

static void mmc_pwrseq_simple_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	if (!IS_ERR(pwrseq->ext_clk) && !pwrseq->clk_enabled) {
		clk_prepare_enable(pwrseq->ext_clk);
		pwrseq->clk_enabled = true;
	}

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);
}

static void mmc_pwrseq_simple_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 0);

	if (pwrseq->post_power_on_delay_ms)
		msleep(pwrseq->post_power_on_delay_ms);
}

static void mmc_pwrseq_simple_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);

	if (pwrseq->power_off_delay_us)
		usleep_range(pwrseq->power_off_delay_us,
			2 * pwrseq->power_off_delay_us);

	if (!IS_ERR(pwrseq->ext_clk) && pwrseq->clk_enabled) {
		clk_disable_unprepare(pwrseq->ext_clk);
		pwrseq->clk_enabled = false;
	}
}

static const struct mmc_pwrseq_ops mmc_pwrseq_simple_ops = {
	.pre_power_on = mmc_pwrseq_simple_pre_power_on,
	.post_power_on = mmc_pwrseq_simple_post_power_on,
	.power_off = mmc_pwrseq_simple_power_off,
};

static ssize_t pwr_gpio_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(to_platform_device(dev));
	return sysfs_emit(buf, "%s\n", pwrseq->clk_enabled ? "on" : "off");
}

static ssize_t pwr_gpio_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(to_platform_device(dev));
	bool on;

	if (sysfs_streq(buf, "on") || sysfs_streq(buf, "1"))
	on = true;
	else if (sysfs_streq(buf, "off") || sysfs_streq(buf, "0"))
	on = false;
	else
	return count;

	if (on) {
	if (!IS_ERR(pwrseq->ext_clk) && !pwrseq->clk_enabled) {
		clk_prepare_enable(pwrseq->ext_clk);
		pwrseq->clk_enabled = true;
	}
	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);
	mmc_pwrseq_simple_set_gpios_value(pwrseq, 0);
	if (pwrseq->post_power_on_delay_ms)
		msleep(pwrseq->post_power_on_delay_ms);
	} else {
		mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);
		if (pwrseq->power_off_delay_us)
			usleep_range(pwrseq->power_off_delay_us, 2 * pwrseq->power_off_delay_us);
		if (!IS_ERR(pwrseq->ext_clk) && pwrseq->clk_enabled) {
			clk_disable_unprepare(pwrseq->ext_clk);
			pwrseq->clk_enabled = false;
		}
	}

	return count;
}

static DEVICE_ATTR_RW(pwr_gpio);

static ssize_t vref_uV_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(to_platform_device(dev));
	int uV;
	if (IS_ERR(pwrseq->vref))
		return sysfs_emit(buf, "na\n");
	uV = regulator_get_voltage(pwrseq->vref);
	return sysfs_emit(buf, "%d\n", uV);
}

static ssize_t vref_uV_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(to_platform_device(dev));
	long min_uV, max_uV;
	int ret;
	if (IS_ERR(pwrseq->vref))
		return count;
	ret = kstrtol(buf, 10, &min_uV);
	if (ret == 0) {
		max_uV = min_uV;
		regulator_set_voltage(pwrseq->vref, (int)min_uV, (int)max_uV);
		return count;
	}
	ret = sscanf(buf, "%ld %ld", &min_uV, &max_uV);
	if (ret == 2) {
		regulator_set_voltage(pwrseq->vref, (int)min_uV, (int)max_uV);
		return count;
	}
	return count;
}

static DEVICE_ATTR_RW(vref_uV);

static const struct of_device_id mmc_pwrseq_simple_of_match[] = {
	{ .compatible = "mmc-pwrseq-simple",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mmc_pwrseq_simple_of_match);

static int mmc_pwrseq_simple_probe(struct platform_device *pdev)
{
	struct mmc_pwrseq_simple *pwrseq;
	struct device *dev = &pdev->dev;

	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->ext_clk = devm_clk_get(dev, "ext_clock");
	if (IS_ERR(pwrseq->ext_clk) && PTR_ERR(pwrseq->ext_clk) != -ENOENT)
		return PTR_ERR(pwrseq->ext_clk);

	pwrseq->reset_gpios = devm_gpiod_get_array(dev, "reset",
							GPIOD_OUT_HIGH);
	if (IS_ERR(pwrseq->reset_gpios) &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOENT &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOSYS) {
		return PTR_ERR(pwrseq->reset_gpios);
	}

	device_property_read_u32(dev, "post-power-on-delay-ms",
				 &pwrseq->post_power_on_delay_ms);
	device_property_read_u32(dev, "power-off-delay-us",
				 &pwrseq->power_off_delay_us);

	pwrseq->pwrseq.dev = dev;
	pwrseq->pwrseq.ops = &mmc_pwrseq_simple_ops;
	pwrseq->pwrseq.owner = THIS_MODULE;
	platform_set_drvdata(pdev, pwrseq);

	device_create_file(dev, &dev_attr_pwr_gpio);

	pwrseq->vref = devm_regulator_get_optional(dev, "vref");
	if (!IS_ERR(pwrseq->vref))
		device_create_file(dev, &dev_attr_vref_uV);

	return mmc_pwrseq_register(&pwrseq->pwrseq);
}

static int mmc_pwrseq_simple_remove(struct platform_device *pdev)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(pdev);

	mmc_pwrseq_unregister(&pwrseq->pwrseq);

	device_remove_file(&pdev->dev, &dev_attr_pwr_gpio);
	if (!IS_ERR(pwrseq->vref))
		device_remove_file(&pdev->dev, &dev_attr_vref_uV);

	return 0;
}

static struct platform_driver mmc_pwrseq_simple_driver = {
	.probe = mmc_pwrseq_simple_probe,
	.remove = mmc_pwrseq_simple_remove,
	.driver = {
		.name = "pwrseq_simple",
		.of_match_table = mmc_pwrseq_simple_of_match,
	},
};

module_platform_driver(mmc_pwrseq_simple_driver);
MODULE_LICENSE("GPL v2");
