// SPDX-License-Identifier: GPL-2.0-only
/*
 * KTZ8866 backlight driver for Xiaomi Pad 6 (pipa).
 *
 * Brightness mapping and register values are from the pipa 4.19 driver.
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>

#include "ktz8866-regs.h"

struct ktz8866 {
	struct i2c_client *client;
	struct backlight_device *backlight;
	struct gpio_desc *hw_en_gpio;
	struct gpio_desc *panel_id_gpio;
	struct mutex lock;
	u16 last_level;
	int panel_id;
	bool hbm_enabled;
	bool skip_configuration;
	bool output_enabled;
	bool state_valid;
};

static int ktz8866_read(struct ktz8866 *chip, u8 reg, u8 *value)
{
	int ret;

	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		dev_err(&chip->client->dev, "failed reading register 0x%02x: %d\n",
			reg, ret);
		return ret;
	}

	*value = ret;
	return 0;
}

static int ktz8866_write(struct ktz8866 *chip, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(chip->client, reg, value);
	if (ret < 0)
		dev_err(&chip->client->dev,
			"failed writing 0x%02x to register 0x%02x: %d\n",
			value, reg, ret);

	return ret;
}

static int ktz8866_configure(struct ktz8866 *chip)
{
	int i, ret;
	u8 value;

	for (i = 0; i < ARRAY_SIZE(ktz8866_regs_conf); i++) {
		ret = ktz8866_write(chip, ktz8866_regs_conf[i].reg,
				    ktz8866_regs_conf[i].value);
		if (ret < 0)
			return ret;
	}

	/* The pipa panel-id low variant uses the source driver's BOE current. */
	if (!chip->panel_id) {
		ret = ktz8866_write(chip, KTZ8866_DISP_FULL_CURRENT, 0xc1);
		if (ret < 0)
			return ret;
	}

	ret = ktz8866_read(chip, KTZ8866_DISP_FLAGS, &value);
	if (!ret)
		dev_dbg(&chip->client->dev, "flags register: 0x%02x\n", value);

	return ret;
}

static unsigned int ktz8866_map_brightness(struct ktz8866 *chip,
					   unsigned int brightness)
{
	unsigned int map_index;

	if (chip->hbm_enabled) {
		if (brightness <= BL_LEVEL_MAX)
			map_index = brightness * 1700 / BL_LEVEL_MAX;
		else
			map_index = (brightness - (BL_LEVEL_MAX + 1)) *
				( BL_LEVEL_MAX - 1700) / BL_LEVEL_MAX + 1700;
	} else {
		map_index = min(brightness, (unsigned int)BL_LEVEL_MAX);
	}

	return ktz8866_bl_level_remap[map_index];
}

static int ktz8866_backlight_update_status(struct backlight_device *backlight)
{
	struct ktz8866 *chip = bl_get_data(backlight);
	unsigned int brightness, level;
	int requested;
	int ret = 0;
	u8 lsb, msb;

	requested = clamp(backlight->props.brightness, 0,
			  backlight->props.max_brightness);
	brightness = requested;
	if (backlight->props.power != FB_BLANK_UNBLANK ||
	    backlight->props.fb_blank != FB_BLANK_UNBLANK ||
	    (backlight->props.state &
	     (BL_CORE_FBBLANK | BL_CORE_SUSPENDED)))
		brightness = 0;

	level = ktz8866_map_brightness(chip, brightness);

	mutex_lock(&chip->lock);
	if (chip->state_valid && chip->last_level == level &&
	    chip->output_enabled == (level > 0)) {
		goto out;
	}

	/* KTZ8866 brightness bits 10:3 are in 0x05 and bits 2:0 in 0x04. */
	lsb = level & 0x7;
	msb = (level >> 3) & 0xff;
	if (level) {
		ret = ktz8866_write(chip, KTZ8866_DISP_BB_LSB, lsb);
		if (ret < 0)
			goto out;
		ret = ktz8866_write(chip, KTZ8866_DISP_BB_MSB, msb);
		if (ret < 0)
			goto out;
		if (!chip->output_enabled) {
			ret = ktz8866_write(chip, KTZ8866_DISP_BL_ENABLE, 0x5f);
			if (ret < 0)
				goto out;
		}
	} else {
		ret = ktz8866_write(chip, KTZ8866_DISP_BL_ENABLE, 0x1f);
		if (ret < 0)
			goto out;
		ret = ktz8866_write(chip, KTZ8866_DISP_BB_LSB, 0);
		if (ret < 0)
			goto out;
		ret = ktz8866_write(chip, KTZ8866_DISP_BB_MSB, 0);
		if (ret < 0)
			goto out;
	}

	chip->last_level = level;
	chip->output_enabled = level > 0;
	chip->state_valid = true;
out:
	mutex_unlock(&chip->lock);
	return ret;
}

static int ktz8866_backlight_get_brightness(struct backlight_device *backlight)
{
	struct ktz8866 *chip = bl_get_data(backlight);
	u8 msb, lsb;
	int ret;

	mutex_lock(&chip->lock);
	ret = ktz8866_read(chip, KTZ8866_DISP_BB_MSB, &msb);
	if (!ret)
		ret = ktz8866_read(chip, KTZ8866_DISP_BB_LSB, &lsb);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		return ret;

	/* Preserve the raw register-level value exposed by the 4.19 driver. */
	return (lsb << 8) | msb;
}

static const struct backlight_ops ktz8866_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = ktz8866_backlight_update_status,
	.get_brightness = ktz8866_backlight_get_brightness,
};

static int ktz8866_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct backlight_properties props = { 0 };
	struct ktz8866 *chip;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	chip->hbm_enabled = of_property_read_bool(client->dev.of_node,
						 "ktz8866,backlight-HBM-enable");
	chip->skip_configuration = of_property_read_bool(client->dev.of_node,
							 "ktz8866,backlight-conf-disable");
	mutex_init(&chip->lock);

	chip->hw_en_gpio = devm_gpiod_get_from_of_node(&client->dev,
			client->dev.of_node, "ktz8866,hwen-gpio", 0,
			GPIOD_OUT_HIGH, "ktz8866-hwen");
	if (IS_ERR(chip->hw_en_gpio)) {
		ret = PTR_ERR(chip->hw_en_gpio);
		dev_err(&client->dev, "failed to request HW_EN GPIO: %d\n", ret);
		return ret;
	}

	chip->panel_id_gpio = devm_gpiod_get_from_of_node(&client->dev,
			client->dev.of_node, "ktz8866,panelid-gpio", 0,
			GPIOD_IN, "ktz8866-panel-id");
	if (IS_ERR(chip->panel_id_gpio)) {
		ret = PTR_ERR(chip->panel_id_gpio);
		dev_err(&client->dev, "failed to request panel-id GPIO: %d\n",
			ret);
		return ret;
	}

	chip->panel_id = gpiod_get_value_cansleep(chip->panel_id_gpio);
	if (chip->panel_id < 0)
		return chip->panel_id;

	i2c_set_clientdata(client, chip);
	if (!chip->skip_configuration) {
		ret = ktz8866_configure(chip);
		if (ret < 0)
			return ret;
	}

	props.type = BACKLIGHT_RAW;
	props.max_brightness = chip->hbm_enabled ? BL_LEVEL_MAX_HBM :
							     BL_LEVEL_MAX;
	props.brightness = 0;
	chip->backlight = devm_backlight_device_register(&client->dev,
			dev_name(&client->dev), &client->dev, chip,
			&ktz8866_backlight_ops, &props);
	if (IS_ERR(chip->backlight)) {
		ret = PTR_ERR(chip->backlight);
		dev_err(&client->dev, "failed to register backlight: %d\n", ret);
		return ret;
	}

	ret = backlight_update_status(chip->backlight);
	if (ret < 0)
		return ret;

	dev_info(&client->dev, "KTZ8866 registered (HBM %s, config %s)\n",
		 chip->hbm_enabled ? "enabled" : "disabled",
		 chip->skip_configuration ? "skipped by DT" : "initialized");
	return 0;
}

static int ktz8866_remove(struct i2c_client *client)
{
	struct ktz8866 *chip = i2c_get_clientdata(client);

	chip->backlight->props.brightness = 0;
	return backlight_update_status(chip->backlight);
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

MODULE_DESCRIPTION("KTZ8866 backlight driver for Xiaomi Pad 6 pipa");
MODULE_LICENSE("GPL");
