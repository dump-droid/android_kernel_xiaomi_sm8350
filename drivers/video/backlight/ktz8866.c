// SPDX-License-Identifier: GPL-2.0
/*
 * KTZ8866 backlight driver with the Xiaomi pipa brightness mapping.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "ktz8866.h"

struct ktz8866 {
	struct i2c_client *client;
	struct backlight_device *backlight;
	struct ktz8866_platform_data pdata;
	struct mutex lock;
	int hw_brightness;
	bool backlight_enabled;
	bool hbm_enabled;
	bool panel_id;
};

static int ktz8866_read(struct ktz8866 *ktz, u8 reg, u8 *value)
{
	int ret;

	ret = i2c_smbus_read_byte_data(ktz->client, reg);
	if (ret < 0) {
		dev_err(&ktz->client->dev, "failed reading register 0x%02x: %d\n",
			reg, ret);
		return ret;
	}

	*value = ret;
	return 0;
}

static int ktz8866_write(struct ktz8866 *ktz, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(ktz->client, reg, value);
	if (ret < 0)
		dev_err(&ktz->client->dev,
			"failed writing register 0x%02x: %d\n", reg, ret);

	return ret;
}

static int ktz8866_map_brightness(struct ktz8866 *ktz, int brightness)
{
	int index;

	brightness = clamp(brightness, 0,
			   ktz->hbm_enabled ? BL_LEVEL_MAX_HBM : BL_LEVEL_MAX);

	if (ktz->hbm_enabled) {
		if (brightness <= 2047)
			index = brightness * 1700 / 2047;
		else
			index = (brightness - 2048) * 347 / 2047 + 1700;
	} else {
		index = brightness;
	}

	return bl_level_remap[index];
}

static int ktz8866_unmap_brightness(struct ktz8866 *ktz, int code)
{
	int index;

	for (index = 0; index <= BL_LEVEL_MAX; index++) {
		if (bl_level_remap[index] >= code)
			break;
	}

	index = min(index, BL_LEVEL_MAX);
	if (!ktz->hbm_enabled)
		return index;

	if (index <= 1700)
		return DIV_ROUND_CLOSEST(index * 2047, 1700);

	return min(BL_LEVEL_MAX_HBM,
		   2048 + DIV_ROUND_CLOSEST((index - 1700) * 2047, 347));
}

static int ktz8866_backlight_update_status(struct backlight_device *backlight)
{
	struct ktz8866 *ktz = bl_get_data(backlight);
	int code, ret;

	code = ktz8866_map_brightness(ktz, backlight->props.brightness);
	mutex_lock(&ktz->lock);
	if (code == ktz->hw_brightness &&
	    (!!code == ktz->backlight_enabled)) {
		mutex_unlock(&ktz->lock);
		return 0;
	}
	if (!code && ktz->backlight_enabled) {
		ret = ktz8866_write(ktz, KTZ8866_DISP_BL_ENABLE, 0x1f);
		if (ret < 0)
			goto out;
		ktz->backlight_enabled = false;
		usleep_range(10000, 10010);
	}

	/* The chip latches the 3 LSBs when the 8 MSBs are written. */
	ret = ktz8866_write(ktz, KTZ8866_DISP_BB_LSB, code & 0x7);
	if (ret < 0)
		goto out;

	ret = ktz8866_write(ktz, KTZ8866_DISP_BB_MSB, code >> 3);
	if (ret < 0)
		goto out;

	if (code && !ktz->backlight_enabled) {
		ret = ktz8866_write(ktz, KTZ8866_DISP_BL_ENABLE, 0x5f);
		if (ret < 0)
			goto out;
		ktz->backlight_enabled = true;
	}

	ktz->hw_brightness = code;
	ret = 0;
out:
	mutex_unlock(&ktz->lock);
	return ret;
}

static int ktz8866_backlight_get_brightness(struct backlight_device *backlight)
{
	struct ktz8866 *ktz = bl_get_data(backlight);
	u8 msb, lsb;
	int ret;

	mutex_lock(&ktz->lock);
	ret = ktz8866_read(ktz, KTZ8866_DISP_BB_MSB, &msb);
	if (ret < 0)
		goto out;

	ret = ktz8866_read(ktz, KTZ8866_DISP_BB_LSB, &lsb);
	if (ret < 0)
		goto out;

	ret = ktz8866_unmap_brightness(ktz, (msb << 3) | (lsb & 0x7));
out:
	mutex_unlock(&ktz->lock);
	return ret;
}

static const struct backlight_ops ktz8866_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= ktz8866_backlight_update_status,
	.get_brightness	= ktz8866_backlight_get_brightness,
};

static int ktz8866_backlight_conf(struct ktz8866 *ktz)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ktz8866_regs_conf); i++) {
		ret = ktz8866_write(ktz, ktz8866_regs_conf[i].reg,
				    ktz8866_regs_conf[i].value);
		if (ret < 0)
			return ret;
	}

	if (!ktz->panel_id)
		return ktz8866_write(ktz, KTZ8866_DISP_FULL_CURRENT, 0xc1);

	return 0;
}

static int ktz8866_parse_dt(struct ktz8866 *ktz)
{
	struct device *dev = &ktz->client->dev;
	struct device_node *np = dev->of_node;

	ktz->pdata.hw_en_gpio = of_get_named_gpio(np, "ktz8866,hwen-gpio", 0);
	if (ktz->pdata.hw_en_gpio < 0)
		return ktz->pdata.hw_en_gpio;

	ktz->pdata.panelid_gpio = of_get_named_gpio(np,
						    "ktz8866,panelid-gpio", 0);
	if (ktz->pdata.panelid_gpio < 0)
		return ktz->pdata.panelid_gpio;

	ktz->hbm_enabled = of_property_read_bool(np,
						 "ktz8866,backlight-HBM-enable");

	return 0;
}

static int ktz8866_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct backlight_properties props = { 0 };
	struct device *dev = &client->dev;
	struct ktz8866 *ktz;
	u8 flags;
	int ret;
	bool skip_config;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	ktz = devm_kzalloc(dev, sizeof(*ktz), GFP_KERNEL);
	if (!ktz)
		return -ENOMEM;

	ktz->client = client;
	ktz->hw_brightness = -1;
	mutex_init(&ktz->lock);
	i2c_set_clientdata(client, ktz);

	ret = ktz8866_parse_dt(ktz);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse device tree\n");

	ret = devm_gpio_request_one(dev, ktz->pdata.hw_en_gpio,
				    GPIOF_DIR_OUT | GPIOF_INIT_HIGH, "hwen");
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to request HWEN GPIO\n");

	ret = devm_gpio_request_one(dev, ktz->pdata.panelid_gpio,
				    GPIOF_DIR_IN, "panel_id");
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to request panel ID GPIO\n");

	ret = gpio_get_value_cansleep(ktz->pdata.panelid_gpio);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to read panel ID GPIO\n");
	ktz->panel_id = ret;

	msleep(10);
	skip_config = of_property_read_bool(dev->of_node,
					    "ktz8866,backlight-conf-disable");
	if (!skip_config) {
		ret = ktz8866_backlight_conf(ktz);
		if (ret < 0)
			return ret;
	}

	ret = ktz8866_read(ktz, KTZ8866_DISP_FLAGS, &flags);
	if (ret < 0)
		return ret;
	dev_dbg(dev, "KTZ8866 flags: 0x%02x\n", flags);

	props.type = BACKLIGHT_RAW;
	props.max_brightness = ktz->hbm_enabled ?
			       BL_LEVEL_MAX_HBM : BL_LEVEL_MAX;
	props.brightness = 98;

	ktz->backlight = devm_backlight_device_register(dev, dev_name(dev), dev,
						       ktz,
						       &ktz8866_backlight_ops,
						       &props);
	if (IS_ERR(ktz->backlight))
		return dev_err_probe(dev, PTR_ERR(ktz->backlight),
				     "failed to register backlight\n");

	return backlight_update_status(ktz->backlight);
}

static int ktz8866_remove(struct i2c_client *client)
{
	struct ktz8866 *ktz = i2c_get_clientdata(client);

	ktz->backlight->props.brightness = 0;
	return backlight_update_status(ktz->backlight);
}

static const struct i2c_device_id ktz8866_ids[] = {
	{ "ktz8866", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ktz8866_ids);

static const struct of_device_id ktz8866_match_table[] = {
	{ .compatible = "ktz,ktz8866" },
	{ }
};
MODULE_DEVICE_TABLE(of, ktz8866_match_table);

static struct i2c_driver ktz8866_driver = {
	.driver = {
		.name = "ktz8866",
		.of_match_table = ktz8866_match_table,
	},
	.probe = ktz8866_probe,
	.remove = ktz8866_remove,
	.id_table = ktz8866_ids,
};
module_i2c_driver(ktz8866_driver);

MODULE_DESCRIPTION("Kinetic KTZ8866 backlight driver for Xiaomi pipa");
MODULE_AUTHOR("Xiaomi kernel contributors");
MODULE_LICENSE("GPL");
