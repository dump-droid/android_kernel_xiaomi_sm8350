// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Sysfs interface for the universal power supply monitor class
 *
 *  Copyright © 2007  David Woodhouse <dwmw2@infradead.org>
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/stat.h>

#include "power_supply.h"

#define MAX_PROP_NAME_LEN 30

struct power_supply_attr {
	const char *prop_name;
	char attr_name[MAX_PROP_NAME_LEN + 1];
	struct device_attribute dev_attr;
	const char * const *text_values;
	int text_values_len;
};

#define _POWER_SUPPLY_ATTR(_name, _text, _len)	\
[POWER_SUPPLY_PROP_ ## _name] =			\
{						\
	.prop_name = #_name,			\
	.attr_name = #_name "\0",		\
	.text_values = _text,			\
	.text_values_len = _len,		\
}

#define POWER_SUPPLY_ATTR(_name) _POWER_SUPPLY_ATTR(_name, NULL, 0)
#define _POWER_SUPPLY_ENUM_ATTR(_name, _text)	\
	_POWER_SUPPLY_ATTR(_name, _text, ARRAY_SIZE(_text))
#define POWER_SUPPLY_ENUM_ATTR(_name)	\
	_POWER_SUPPLY_ENUM_ATTR(_name, POWER_SUPPLY_ ## _name ## _TEXT)

#ifdef CONFIG_BATT_VERIFY_BY_DS28E16_PIPA
#define POWER_SUPPLY_PIPA_ATTR(_prop, _name)		\
	[POWER_SUPPLY_PROP_ ## _prop] = {		\
		.prop_name = #_prop,			\
		.attr_name = #_name,			\
		.text_values = NULL,			\
		.text_values_len = 0,			\
	}
#endif

#ifdef CONFIG_QPNP_FG_GEN4_PIPA
#define POWER_SUPPLY_FG_ATTR(_prop, _name)		\
	[POWER_SUPPLY_PROP_ ## _prop] = {		\
		.prop_name = #_prop,			\
		.attr_name = #_name,			\
		.text_values = NULL,			\
		.text_values_len = 0,			\
	}
#endif

#if defined(CONFIG_HL6111R) || defined(CONFIG_FUDA_1665)
#define POWER_SUPPLY_WLS_ATTR(_prop, _name)		\
	[POWER_SUPPLY_PROP_ ## _prop] = {		\
		.prop_name = #_prop,			\
		.attr_name = #_name,			\
		.text_values = NULL,			\
		.text_values_len = 0,			\
	}
#endif

static const char * const POWER_SUPPLY_TYPE_TEXT[] = {
	[POWER_SUPPLY_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_TYPE_BATTERY]		= "Battery",
	[POWER_SUPPLY_TYPE_UPS]			= "UPS",
	[POWER_SUPPLY_TYPE_MAINS]		= "Mains",
	[POWER_SUPPLY_TYPE_USB]			= "USB",
	[POWER_SUPPLY_TYPE_USB_DCP]		= "USB_DCP",
	[POWER_SUPPLY_TYPE_USB_CDP]		= "USB_CDP",
	[POWER_SUPPLY_TYPE_USB_ACA]		= "USB_ACA",
	[POWER_SUPPLY_TYPE_USB_TYPE_C]		= "USB_C",
	[POWER_SUPPLY_TYPE_USB_PD]		= "USB_PD",
	[POWER_SUPPLY_TYPE_USB_PD_DRP]		= "USB_PD_DRP",
	[POWER_SUPPLY_TYPE_APPLE_BRICK_ID]	= "BrickID",
	[POWER_SUPPLY_TYPE_WIRELESS]		= "Wireless",
#ifdef CONFIG_BATT_VERIFY_BY_DS28E16_PIPA
	[POWER_SUPPLY_TYPE_BATT_VERIFY]		= "Batt_Verify",
#endif
#ifdef CONFIG_QPNP_FG_GEN4_PIPA
	[POWER_SUPPLY_TYPE_BMS]			= "BMS",
#endif
};

static const char * const POWER_SUPPLY_USB_TYPE_TEXT[] = {
	[POWER_SUPPLY_USB_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_USB_TYPE_SDP]		= "SDP",
	[POWER_SUPPLY_USB_TYPE_DCP]		= "DCP",
	[POWER_SUPPLY_USB_TYPE_CDP]		= "CDP",
	[POWER_SUPPLY_USB_TYPE_ACA]		= "ACA",
	[POWER_SUPPLY_USB_TYPE_C]		= "C",
	[POWER_SUPPLY_USB_TYPE_PD]		= "PD",
	[POWER_SUPPLY_USB_TYPE_PD_DRP]		= "PD_DRP",
	[POWER_SUPPLY_USB_TYPE_PD_PPS]		= "PD_PPS",
	[POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID]	= "BrickID",
};

static const char * const POWER_SUPPLY_STATUS_TEXT[] = {
	[POWER_SUPPLY_STATUS_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_STATUS_CHARGING]		= "Charging",
	[POWER_SUPPLY_STATUS_DISCHARGING]	= "Discharging",
	[POWER_SUPPLY_STATUS_NOT_CHARGING]	= "Not charging",
	[POWER_SUPPLY_STATUS_FULL]		= "Full",
};

static const char * const POWER_SUPPLY_CHARGE_TYPE_TEXT[] = {
	[POWER_SUPPLY_CHARGE_TYPE_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_CHARGE_TYPE_NONE]		= "N/A",
	[POWER_SUPPLY_CHARGE_TYPE_TRICKLE]	= "Trickle",
	[POWER_SUPPLY_CHARGE_TYPE_FAST]		= "Fast",
	[POWER_SUPPLY_CHARGE_TYPE_STANDARD]	= "Standard",
	[POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE]	= "Adaptive",
	[POWER_SUPPLY_CHARGE_TYPE_CUSTOM]	= "Custom",
#ifdef CONFIG_BQ2597X_CHARGE_PUMP_PIPA
	[POWER_SUPPLY_CHARGE_TYPE_TAPER]	= "Taper",
#endif
};

static const char * const POWER_SUPPLY_HEALTH_TEXT[] = {
	[POWER_SUPPLY_HEALTH_UNKNOWN]		    = "Unknown",
	[POWER_SUPPLY_HEALTH_GOOD]		    = "Good",
	[POWER_SUPPLY_HEALTH_OVERHEAT]		    = "Overheat",
	[POWER_SUPPLY_HEALTH_DEAD]		    = "Dead",
	[POWER_SUPPLY_HEALTH_OVERVOLTAGE]	    = "Over voltage",
	[POWER_SUPPLY_HEALTH_UNSPEC_FAILURE]	    = "Unspecified failure",
	[POWER_SUPPLY_HEALTH_COLD]		    = "Cold",
	[POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE] = "Watchdog timer expire",
	[POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE]   = "Safety timer expire",
	[POWER_SUPPLY_HEALTH_OVERCURRENT]	    = "Over current",
	[POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED]  = "Calibration required",
	[POWER_SUPPLY_HEALTH_WARM]		    = "Warm",
	[POWER_SUPPLY_HEALTH_COOL]		    = "Cool",
	[POWER_SUPPLY_HEALTH_HOT]		    = "Hot",
};

static const char * const POWER_SUPPLY_TECHNOLOGY_TEXT[] = {
	[POWER_SUPPLY_TECHNOLOGY_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_TECHNOLOGY_NiMH]		= "NiMH",
	[POWER_SUPPLY_TECHNOLOGY_LION]		= "Li-ion",
	[POWER_SUPPLY_TECHNOLOGY_LIPO]		= "Li-poly",
	[POWER_SUPPLY_TECHNOLOGY_LiFe]		= "LiFe",
	[POWER_SUPPLY_TECHNOLOGY_NiCd]		= "NiCd",
	[POWER_SUPPLY_TECHNOLOGY_LiMn]		= "LiMn",
};

static const char * const POWER_SUPPLY_CAPACITY_LEVEL_TEXT[] = {
	[POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL]	= "Critical",
	[POWER_SUPPLY_CAPACITY_LEVEL_LOW]	= "Low",
	[POWER_SUPPLY_CAPACITY_LEVEL_NORMAL]	= "Normal",
	[POWER_SUPPLY_CAPACITY_LEVEL_HIGH]	= "High",
	[POWER_SUPPLY_CAPACITY_LEVEL_FULL]	= "Full",
};

static const char * const POWER_SUPPLY_SCOPE_TEXT[] = {
	[POWER_SUPPLY_SCOPE_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_SCOPE_SYSTEM]	= "System",
	[POWER_SUPPLY_SCOPE_DEVICE]	= "Device",
};

static struct power_supply_attr power_supply_attrs[] = {
	/* Properties of type `int' */
	POWER_SUPPLY_ENUM_ATTR(STATUS),
	POWER_SUPPLY_ENUM_ATTR(CHARGE_TYPE),
	POWER_SUPPLY_ENUM_ATTR(HEALTH),
	POWER_SUPPLY_ATTR(PRESENT),
	POWER_SUPPLY_ATTR(ONLINE),
	POWER_SUPPLY_ATTR(AUTHENTIC),
	POWER_SUPPLY_ENUM_ATTR(TECHNOLOGY),
	POWER_SUPPLY_ATTR(CYCLE_COUNT),
	POWER_SUPPLY_ATTR(VOLTAGE_MAX),
	POWER_SUPPLY_ATTR(VOLTAGE_MIN),
	POWER_SUPPLY_ATTR(VOLTAGE_MAX_DESIGN),
	POWER_SUPPLY_ATTR(VOLTAGE_MIN_DESIGN),
	POWER_SUPPLY_ATTR(VOLTAGE_NOW),
	POWER_SUPPLY_ATTR(VOLTAGE_AVG),
	POWER_SUPPLY_ATTR(VOLTAGE_OCV),
	POWER_SUPPLY_ATTR(VOLTAGE_BOOT),
	POWER_SUPPLY_ATTR(CURRENT_MAX),
	POWER_SUPPLY_ATTR(CURRENT_NOW),
	POWER_SUPPLY_ATTR(CURRENT_AVG),
	POWER_SUPPLY_ATTR(CURRENT_BOOT),
	POWER_SUPPLY_ATTR(POWER_NOW),
	POWER_SUPPLY_ATTR(POWER_AVG),
	POWER_SUPPLY_ATTR(CHARGE_FULL_DESIGN),
	POWER_SUPPLY_ATTR(CHARGE_EMPTY_DESIGN),
	POWER_SUPPLY_ATTR(CHARGE_FULL),
	POWER_SUPPLY_ATTR(CHARGE_EMPTY),
	POWER_SUPPLY_ATTR(CHARGE_NOW),
	POWER_SUPPLY_ATTR(CHARGE_AVG),
	POWER_SUPPLY_ATTR(CHARGE_COUNTER),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_CURRENT),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_CURRENT_MAX),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_VOLTAGE),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_VOLTAGE_MAX),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_LIMIT),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_LIMIT_MAX),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_START_THRESHOLD),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_END_THRESHOLD),
	POWER_SUPPLY_ATTR(INPUT_CURRENT_LIMIT),
	POWER_SUPPLY_ATTR(INPUT_VOLTAGE_LIMIT),
	POWER_SUPPLY_ATTR(INPUT_POWER_LIMIT),
	POWER_SUPPLY_ATTR(ENERGY_FULL_DESIGN),
	POWER_SUPPLY_ATTR(ENERGY_EMPTY_DESIGN),
	POWER_SUPPLY_ATTR(ENERGY_FULL),
	POWER_SUPPLY_ATTR(ENERGY_EMPTY),
	POWER_SUPPLY_ATTR(ENERGY_NOW),
	POWER_SUPPLY_ATTR(ENERGY_AVG),
	POWER_SUPPLY_ATTR(CAPACITY),
	POWER_SUPPLY_ATTR(CAPACITY_ALERT_MIN),
	POWER_SUPPLY_ATTR(CAPACITY_ALERT_MAX),
	POWER_SUPPLY_ATTR(CAPACITY_ERROR_MARGIN),
	POWER_SUPPLY_ENUM_ATTR(CAPACITY_LEVEL),
	POWER_SUPPLY_ATTR(TEMP),
	POWER_SUPPLY_ATTR(TEMP_MAX),
	POWER_SUPPLY_ATTR(TEMP_MIN),
	POWER_SUPPLY_ATTR(TEMP_ALERT_MIN),
	POWER_SUPPLY_ATTR(TEMP_ALERT_MAX),
	POWER_SUPPLY_ATTR(TEMP_AMBIENT),
	POWER_SUPPLY_ATTR(TEMP_AMBIENT_ALERT_MIN),
	POWER_SUPPLY_ATTR(TEMP_AMBIENT_ALERT_MAX),
	POWER_SUPPLY_ATTR(TIME_TO_EMPTY_NOW),
	POWER_SUPPLY_ATTR(TIME_TO_EMPTY_AVG),
	POWER_SUPPLY_ATTR(TIME_TO_FULL_NOW),
	POWER_SUPPLY_ATTR(TIME_TO_FULL_AVG),
	POWER_SUPPLY_ENUM_ATTR(TYPE),
	POWER_SUPPLY_ATTR(USB_TYPE),
	POWER_SUPPLY_ENUM_ATTR(SCOPE),
	POWER_SUPPLY_ATTR(PRECHARGE_CURRENT),
	POWER_SUPPLY_ATTR(CHARGE_TERM_CURRENT),
	POWER_SUPPLY_ATTR(CALIBRATE),
	POWER_SUPPLY_ATTR(MANUFACTURE_YEAR),
	POWER_SUPPLY_ATTR(MANUFACTURE_MONTH),
	POWER_SUPPLY_ATTR(MANUFACTURE_DAY),
	/* Properties of type `const char *' */
	POWER_SUPPLY_ATTR(MODEL_NAME),
	POWER_SUPPLY_ATTR(MANUFACTURER),
	POWER_SUPPLY_ATTR(SERIAL_NUMBER),
	POWER_SUPPLY_ATTR(QUICK_CHARGE_TYPE),
	POWER_SUPPLY_ATTR(TX_ADAPTER),
	POWER_SUPPLY_ATTR(SIGNAL_STRENGTH),
	POWER_SUPPLY_ATTR(REVERSE_CHG_MODE),
	[POWER_SUPPLY_PROP_BATTERY_SLAVE_TEMP] = {
		.prop_name = "batt_slave_temp",
		.attr_name = "batt_slave_temp",
		.text_values = NULL,
		.text_values_len = 0,
	},
#ifdef CONFIG_BATT_VERIFY_BY_DS28E16_PIPA
	POWER_SUPPLY_PIPA_ATTR(ROMID, romid),
	POWER_SUPPLY_PIPA_ATTR(DS_STATUS, ds_status),
	POWER_SUPPLY_PIPA_ATTR(PAGENUMBER, pagenumber),
	POWER_SUPPLY_PIPA_ATTR(PAGEDATA, pagedata),
	POWER_SUPPLY_PIPA_ATTR(AUTHEN_RESULT, authen_result),
	POWER_SUPPLY_PIPA_ATTR(SESSION_SEED, session_seed),
	POWER_SUPPLY_PIPA_ATTR(S_SECRET, s_secret),
	POWER_SUPPLY_PIPA_ATTR(CHALLENGE, challenge),
	POWER_SUPPLY_PIPA_ATTR(AUTH_ANON, auth_anon),
	POWER_SUPPLY_PIPA_ATTR(AUTH_BDCONST, auth_bdconst),
	POWER_SUPPLY_PIPA_ATTR(PAGE0_DATA, page0_data),
	POWER_SUPPLY_PIPA_ATTR(PAGE1_DATA, page1_data),
	POWER_SUPPLY_PIPA_ATTR(VERIFY_MODEL_NAME, verify_model_name),
	POWER_SUPPLY_PIPA_ATTR(CHIP_OK, chip_ok),
	POWER_SUPPLY_PIPA_ATTR(MAXIM_BATT_CYCLE_COUNT, maxim_batt_cycle_count),
#endif
#ifdef CONFIG_QPNP_FG_GEN4_PIPA
	POWER_SUPPLY_FG_ATTR(REAL_CAPACITY, real_capacity),
	POWER_SUPPLY_FG_ATTR(SHUTDOWN_DELAY, shutdown_delay),
	POWER_SUPPLY_FG_ATTR(SHUTDOWN_DELAY_ENABLE, shutdown_delay_en),
	POWER_SUPPLY_FG_ATTR(CAPACITY_RAW, capacity_raw),
	POWER_SUPPLY_FG_ATTR(SOC_DECIMAL, soc_decimal),
	POWER_SUPPLY_FG_ATTR(SOC_DECIMAL_RATE, soc_decimal_rate),
	POWER_SUPPLY_FG_ATTR(CC_SOC, cc_soc),
	POWER_SUPPLY_FG_ATTR(RESISTANCE_ID, resistance_id),
	POWER_SUPPLY_FG_ATTR(RESISTANCE, resistance),
	POWER_SUPPLY_FG_ATTR(ESR_ACTUAL, esr_actual),
	POWER_SUPPLY_FG_ATTR(ESR_NOMINAL, esr_nominal),
	POWER_SUPPLY_FG_ATTR(BATTERY_TYPE, battery_type),
	POWER_SUPPLY_FG_ATTR(CHARGE_NOW_RAW, charge_now_raw),
	POWER_SUPPLY_FG_ATTR(CHARGE_COUNTER_SHADOW, charge_counter_shadow),
	POWER_SUPPLY_FG_ATTR(CYCLE_COUNTS, cycle_counts),
	POWER_SUPPLY_FG_ATTR(SOC_REPORTING_READY, soc_reporting_ready),
	POWER_SUPPLY_FG_ATTR(CLEAR_SOH, clear_soh),
	POWER_SUPPLY_FG_ATTR(SOH, soh),
	POWER_SUPPLY_FG_ATTR(DEBUG_BATTERY, debug_battery),
	POWER_SUPPLY_FG_ATTR(CHARGE_DONE, charge_done),
	POWER_SUPPLY_FG_ATTR(CHARGING_ENABLED, charging_enabled),
	POWER_SUPPLY_FG_ATTR(CC_STEP, cc_step),
	POWER_SUPPLY_FG_ATTR(CC_STEP_SEL, cc_step_sel),
	POWER_SUPPLY_FG_ATTR(BATT_AGE_LEVEL, batt_age_level),
	POWER_SUPPLY_FG_ATTR(SCALE_MODE_EN, scale_mode_en),
	POWER_SUPPLY_FG_ATTR(FASTCHARGE_MODE, fastcharge_mode),
	POWER_SUPPLY_FG_ATTR(FFC_TERMINATION_CURRENT, ffc_termination_current),
	POWER_SUPPLY_FG_ATTR(SYS_TERMINATION_CURRENT, sys_termination_current),
	POWER_SUPPLY_FG_ATTR(FFC_SYS_TERMINATION_CURRENT,
						ffc_sys_termination_current),
	POWER_SUPPLY_FG_ATTR(VBATT_FULL_VOL, vbatt_full_vol),
	POWER_SUPPLY_FG_ATTR(FFC_VBATT_FULL_VOL, ffc_vbatt_full_vol),
	POWER_SUPPLY_FG_ATTR(KI_COEFF_CURRENT, ki_coeff_current),
	POWER_SUPPLY_FG_ATTR(RECHARGE_SOC, recharge_soc),
#endif
#if defined(CONFIG_HL6111R) || defined(CONFIG_FUDA_1665)
	POWER_SUPPLY_WLS_ATTR(INPUT_VOLTAGE_REGULATION,
						input_voltage_regulation),
#endif
#ifdef CONFIG_HL6111R
	POWER_SUPPLY_WLS_ATTR(VOLTAGE_STEP, voltage_step),
	POWER_SUPPLY_WLS_ATTR(CHIP_VERSION, chip_version),
#endif
#ifdef CONFIG_FUDA_1665
	POWER_SUPPLY_WLS_ATTR(PIN_ENABLED, pin_enabled),
	POWER_SUPPLY_WLS_ATTR(WIRELESS_VERSION, wireless_version),
	POWER_SUPPLY_WLS_ATTR(WIRELESS_FW_VERSION, wireless_fw_version),
	POWER_SUPPLY_WLS_ATTR(INPUT_VOLTAGE_VRECT, input_voltage_vrect),
	POWER_SUPPLY_WLS_ATTR(RX_IOUT, rx_iout),
	POWER_SUPPLY_WLS_ATTR(WIRELESS_TX_ID, wireless_tx_id),
	POWER_SUPPLY_WLS_ATTR(DIV_2_MODE, div_2_mode),
	POWER_SUPPLY_WLS_ATTR(OTG_STATE, otg_state),
	POWER_SUPPLY_WLS_ATTR(REVERSE_CHG_HALL3, reverse_chg_hall3),
	POWER_SUPPLY_WLS_ATTR(REVERSE_CHG_HALL4, reverse_chg_hall4),
	POWER_SUPPLY_WLS_ATTR(REVERSE_PEN_SOC, reverse_pen_soc),
	POWER_SUPPLY_WLS_ATTR(REVERSE_VOUT, reverse_vout),
	POWER_SUPPLY_WLS_ATTR(REVERSE_IOUT, reverse_iout),
	POWER_SUPPLY_WLS_ATTR(REVERSE_GPIO_STATE, reverse_gpio_state),
	POWER_SUPPLY_WLS_ATTR(REVERSE_PEN_CHG_STATE, reverse_pen_chg_state),
	POWER_SUPPLY_WLS_ATTR(PEN_MAC, pen_mac),
	POWER_SUPPLY_WLS_ATTR(SW_DISABLE_DC_EN, sw_disable_dc_en),
	POWER_SUPPLY_WLS_ATTR(WIRELESS_WAKELOCK, wireless_wakelock),
#endif
#ifdef CONFIG_BQ2597X_CHARGE_PUMP_PIPA
	POWER_SUPPLY_ATTR(BATTERY_CHARGING_ENABLED),
	POWER_SUPPLY_ATTR(INPUT_SUSPEND),
	POWER_SUPPLY_ATTR(BATTERY_INPUT_SUSPEND),
	POWER_SUPPLY_ATTR(CONNECTOR_TEMP),
	POWER_SUPPLY_ATTR(CHARGE_ENABLED),
	POWER_SUPPLY_ATTR(APDO_MAX),
	POWER_SUPPLY_ATTR(REAL_TYPE),
	POWER_SUPPLY_ATTR(HVDCP3_TYPE),
	POWER_SUPPLY_ATTR(TYPEC_POWER_ROLE),
	POWER_SUPPLY_ATTR(PD_ACTIVE),
	POWER_SUPPLY_ATTR(PD_AUTHENTICATION),
	POWER_SUPPLY_ATTR(FAKE_HVDCP3),
	POWER_SUPPLY_ATTR(DP_DM_BQ),
	POWER_SUPPLY_ATTR(STEP_VFLOAT_INDEX),
	POWER_SUPPLY_ATTR(TI_BATTERY_PRESENT),
	POWER_SUPPLY_ATTR(TI_VBUS_PRESENT),
	POWER_SUPPLY_ATTR(TI_BATTERY_VOLTAGE),
	POWER_SUPPLY_ATTR(TI_BATTERY_CURRENT),
	POWER_SUPPLY_ATTR(TI_BATTERY_TEMPERATURE),
	POWER_SUPPLY_ATTR(TI_BUS_VOLTAGE),
	POWER_SUPPLY_ATTR(TI_BUS_CURRENT),
	POWER_SUPPLY_ATTR(TI_BUS_TEMPERATURE),
	POWER_SUPPLY_ATTR(TI_DIE_TEMPERATURE),
	POWER_SUPPLY_ATTR(TI_ALARM_STATUS),
	POWER_SUPPLY_ATTR(TI_FAULT_STATUS),
	POWER_SUPPLY_ATTR(TI_REG_STATUS),
	POWER_SUPPLY_ATTR(TI_RESET_CHECK),
	POWER_SUPPLY_ATTR(TI_SET_BUS_PROTECTION_FOR_QC3),
	POWER_SUPPLY_ATTR(TI_SET_BUS_PROTECTION_FOR_PD),
	POWER_SUPPLY_ATTR(TI_BUS_ERROR_STATUS),
#endif
	POWER_SUPPLY_ATTR(HAS_DP),
};

static struct attribute *
__power_supply_attrs[ARRAY_SIZE(power_supply_attrs) + 1];

static struct power_supply_attr *to_ps_attr(struct device_attribute *attr)
{
	return container_of(attr, struct power_supply_attr, dev_attr);
}

static enum power_supply_property dev_attr_psp(struct device_attribute *attr)
{
	return  to_ps_attr(attr) - power_supply_attrs;
}

static ssize_t power_supply_show_usb_type(struct device *dev,
					  const struct power_supply_desc *desc,
					  union power_supply_propval *value,
					  char *buf)
{
	enum power_supply_usb_type usb_type;
	ssize_t count = 0;
	bool match = false;
	int i;

	for (i = 0; i < desc->num_usb_types; ++i) {
		usb_type = desc->usb_types[i];

		if (value->intval == usb_type) {
			count += sprintf(buf + count, "[%s] ",
					 POWER_SUPPLY_USB_TYPE_TEXT[usb_type]);
			match = true;
		} else {
			count += sprintf(buf + count, "%s ",
					 POWER_SUPPLY_USB_TYPE_TEXT[usb_type]);
		}
	}

	if (!match) {
		dev_warn(dev, "driver reporting unsupported connected type\n");
		return -EINVAL;
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t power_supply_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf) {
	ssize_t ret;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct power_supply_attr *ps_attr = to_ps_attr(attr);
	enum power_supply_property psp = dev_attr_psp(attr);
	union power_supply_propval value;

	if (psp == POWER_SUPPLY_PROP_TYPE) {
		value.intval = psy->desc->type;
	} else {
		ret = power_supply_get_property(psy, psp, &value);

		if (ret < 0) {
			if (ret == -ENODATA)
				dev_dbg_ratelimited(dev,
					"driver has no data for `%s' property\n",
					attr->attr.name);
			else if (ret != -ENODEV && ret != -EAGAIN)
				dev_err_ratelimited(dev,
					"driver failed to report `%s' property: %zd\n",
					attr->attr.name, ret);
			return ret;
		}
	}

	if (ps_attr->text_values_len > 0 &&
	    value.intval < ps_attr->text_values_len && value.intval >= 0) {
		return sprintf(buf, "%s\n", ps_attr->text_values[value.intval]);
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_USB_TYPE:
		ret = power_supply_show_usb_type(dev, psy->desc,
						&value, buf);
		break;
#ifdef CONFIG_BATT_VERIFY_BY_DS28E16_PIPA
	case POWER_SUPPLY_PROP_ROMID:
	case POWER_SUPPLY_PROP_DS_STATUS:
		ret = scnprintf(buf, PAGE_SIZE,
			"%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
			value.arrayval[0], value.arrayval[1], value.arrayval[2],
			value.arrayval[3], value.arrayval[4], value.arrayval[5],
			value.arrayval[6], value.arrayval[7]);
		break;
	case POWER_SUPPLY_PROP_PAGE0_DATA:
	case POWER_SUPPLY_PROP_PAGE1_DATA:
	case POWER_SUPPLY_PROP_PAGEDATA:
		ret = scnprintf(buf, PAGE_SIZE,
			"%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
			value.arrayval[0], value.arrayval[1], value.arrayval[2],
			value.arrayval[3], value.arrayval[4], value.arrayval[5],
			value.arrayval[6], value.arrayval[7], value.arrayval[8],
			value.arrayval[9], value.arrayval[10], value.arrayval[11],
			value.arrayval[12], value.arrayval[13], value.arrayval[14],
			value.arrayval[15]);
		break;
	case POWER_SUPPLY_PROP_VERIFY_MODEL_NAME:
#endif
#ifdef CONFIG_QPNP_FG_GEN4_PIPA
	case POWER_SUPPLY_PROP_BATTERY_TYPE:
	case POWER_SUPPLY_PROP_CYCLE_COUNTS:
#endif
	case POWER_SUPPLY_PROP_MODEL_NAME ... POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = sprintf(buf, "%s\n", value.strval);
		break;
#ifdef CONFIG_FUDA_1665
	case POWER_SUPPLY_PROP_PEN_MAC:
		ret = scnprintf(buf, PAGE_SIZE, "%llx\n",
				(unsigned long long)value.int64val);
		break;
#endif
	default:
		ret = sprintf(buf, "%d\n", value.intval);
	}

	return ret;
}

static ssize_t power_supply_store_property(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count) {
	ssize_t ret;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct power_supply_attr *ps_attr = to_ps_attr(attr);
	enum power_supply_property psp = dev_attr_psp(attr);
	union power_supply_propval value;
#ifdef CONFIG_FUDA_1665
	s64 value64;
#endif
	int rc;

#ifdef CONFIG_FUDA_1665
	if (psp == POWER_SUPPLY_PROP_PEN_MAC) {
		rc = kstrtoll(buf, 16, &value64);
		if (rc < 0)
			return rc;

		value.int64val = value64;
		ret = power_supply_set_property(psy, psp, &value);
		return ret < 0 ? ret : count;
	}
#endif

	ret = -EINVAL;
	if (ps_attr->text_values_len > 0) {
		ret = __sysfs_match_string(ps_attr->text_values,
					   ps_attr->text_values_len, buf);
	}

	/*
	 * If no match was found, then check to see if it is an integer.
	 * Integer values are valid for enums in addition to the text value.
	 */
	if (ret < 0) {
		long long_val;

		ret = kstrtol(buf, 10, &long_val);
		if (ret < 0)
			return ret;

		ret = long_val;
	}

	value.intval = ret;

	ret = power_supply_set_property(psy, psp, &value);
	if (ret < 0)
		return ret;

	return count;
}

static umode_t power_supply_attr_is_visible(struct kobject *kobj,
					   struct attribute *attr,
					   int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = dev_get_drvdata(dev);
	umode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
	int i;

	if (!power_supply_attrs[attrno].prop_name)
		return 0;

	if (attrno == POWER_SUPPLY_PROP_TYPE)
		return mode;

	for (i = 0; i < psy->desc->num_properties; i++) {
		int property = psy->desc->properties[i];

		if (property == attrno) {
			if (psy->desc->property_is_writeable &&
			    psy->desc->property_is_writeable(psy, property) > 0)
				mode |= S_IWUSR;

			return mode;
		}
	}

	return 0;
}

static struct attribute_group power_supply_attr_group = {
	.attrs = __power_supply_attrs,
	.is_visible = power_supply_attr_is_visible,
};

static const struct attribute_group *power_supply_attr_groups[] = {
	&power_supply_attr_group,
	NULL,
};

static void str_to_lower(char *str)
{
	while (*str) {
		*str = tolower(*str);
		str++;
	}
}

void power_supply_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = power_supply_attr_groups;

	for (i = 0; i < ARRAY_SIZE(power_supply_attrs); i++) {
		struct device_attribute *attr;

		if (!power_supply_attrs[i].prop_name) {
			pr_warn("%s: Property %d skipped because is is missing from power_supply_attrs\n",
				__func__, i);
			sprintf(power_supply_attrs[i].attr_name, "_err_%d", i);
		} else {
			str_to_lower(power_supply_attrs[i].attr_name);
		}

		attr = &power_supply_attrs[i].dev_attr;

		attr->attr.name = power_supply_attrs[i].attr_name;
		attr->show = power_supply_show_property;
		attr->store = power_supply_store_property;
		__power_supply_attrs[i] = &attr->attr;
	}
}

static int add_prop_uevent(struct device *dev, struct kobj_uevent_env *env,
			   enum power_supply_property prop, char *prop_buf)
{
	int ret = 0;
	struct power_supply_attr *pwr_attr;
	struct device_attribute *dev_attr;
	char *line;

	pwr_attr = &power_supply_attrs[prop];
	dev_attr = &pwr_attr->dev_attr;

	ret = power_supply_show_property(dev, dev_attr, prop_buf);
	if (ret == -ENODEV || ret == -ENODATA) {
		/*
		 * When a battery is absent, we expect -ENODEV. Don't abort;
		 * send the uevent with at least the the PRESENT=0 property
		 */
		return 0;
	}

	if (ret < 0)
		return ret;

	line = strchr(prop_buf, '\n');
	if (line)
		*line = 0;

	return add_uevent_var(env, "POWER_SUPPLY_%s=%s",
			      pwr_attr->prop_name, prop_buf);
}

int power_supply_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	int ret = 0, j;
	char *prop_buf;

	if (!psy || !psy->desc) {
		dev_dbg(dev, "No power supply yet\n");
		return ret;
	}

	ret = add_uevent_var(env, "POWER_SUPPLY_NAME=%s", psy->desc->name);
	if (ret)
		return ret;

	prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!prop_buf)
		return -ENOMEM;

	ret = add_prop_uevent(dev, env, POWER_SUPPLY_PROP_TYPE, prop_buf);
	if (ret)
		goto out;

	for (j = 0; j < psy->desc->num_properties; j++) {
		ret = add_prop_uevent(dev, env, psy->desc->properties[j],
				      prop_buf);
		if (ret)
			goto out;
	}

out:
	free_page((unsigned long)prop_buf);

	return ret;
}
