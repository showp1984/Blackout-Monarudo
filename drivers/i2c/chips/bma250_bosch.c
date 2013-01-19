
/*
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2011 Bosch Sensortec GmbH
 * All Rights Reserved
 */



#undef CONFIG_HAS_EARLYSUSPEND

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/bma250.h>

#define D(x...) printk(KERN_DEBUG "[GSNR][BMA250_BOSCH] " x)
#define I(x...) printk(KERN_INFO "[GSNR][BMA250_BOSCH] " x)
#define E(x...) printk(KERN_ERR "[GSNR][BMA250_BOSCH] " x)

#define HTC_ATTR 1

struct bma250acc{
	s16	x,
		y,
		z;
} ;

struct bma250_data {
	struct i2c_client *bma250_client;
	atomic_t delay;
	atomic_t enable;
	atomic_t selftest_result;
	unsigned char mode;
	struct input_dev *input;
	struct bma250acc value;
	struct mutex value_mutex;
	struct mutex enable_mutex;
	struct mutex mode_mutex;
	struct delayed_work work;
	struct work_struct irq_work;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	int IRQ;
	int chip_layout;
#ifdef HTC_ATTR
	struct class *g_sensor_class;
	struct device *g_sensor_dev;
#endif 

	struct bma250_platform_data *pdata;
	short offset_buf[3];
};

struct bma250_data *gdata;



#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma250_early_suspend(struct early_suspend *h);
static void bma250_late_resume(struct early_suspend *h);
#endif

static int bma250_smbus_read_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;
	dummy = i2c_smbus_read_byte_data(client, reg_addr);
	if (dummy < 0)
		return -1;
	*data = dummy & 0x000000ff;

	return 0;
}

static int bma250_smbus_write_byte(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data)
{
	s32 dummy;
	dummy = i2c_smbus_write_byte_data(client, reg_addr, *data);
	if (dummy < 0)
		return -1;
	return 0;
}

static int bma250_smbus_read_byte_block(struct i2c_client *client,
		unsigned char reg_addr, unsigned char *data, unsigned char len)
{
	s32 dummy;
	dummy = i2c_smbus_read_i2c_block_data(client, reg_addr, len, data);
	if (dummy < 0)
		return -1;
	return 0;
}

static int bma250_set_mode(struct i2c_client *client, unsigned char Mode)
{
	int comres = 0;
	unsigned char data1;


	if (Mode < 3) {
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_LOW_POWER__REG, &data1);
		switch (Mode) {
		case BMA250_MODE_NORMAL:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_EN_LOW_POWER, 0);
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_EN_SUSPEND, 0);
			break;
		case BMA250_MODE_LOWPOWER:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_EN_LOW_POWER, 1);
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_EN_SUSPEND, 0);
			break;
		case BMA250_MODE_SUSPEND:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_EN_LOW_POWER, 0);
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_EN_SUSPEND, 1);
			break;
		default:
			break;
		}

		comres += bma250_smbus_write_byte(client,
				BMA250_EN_LOW_POWER__REG, &data1);
	} else{
		comres = -1;
	}


	return comres;
}
#ifdef BMA250_ENABLE_INT1
static int bma250_set_int1_pad_sel(struct i2c_client *client, unsigned char
		int1sel)
{
	int comres = 0;
	unsigned char data;
	unsigned char state;
	state = 0x01;


	switch (int1sel) {
	case 0:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_LOWG__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_LOWG,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_LOWG__REG, &data);
		break;
	case 1:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_HIGHG__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_HIGHG,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_HIGHG__REG, &data);
		break;
	case 2:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_SLOPE__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_SLOPE,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_SLOPE__REG, &data);
		break;
	case 3:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_DB_TAP__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_DB_TAP,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_DB_TAP__REG, &data);
		break;
	case 4:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_SNG_TAP__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_SNG_TAP,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_SNG_TAP__REG, &data);
		break;
	case 5:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_ORIENT__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_ORIENT,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_ORIENT__REG, &data);
		break;
	case 6:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT1_PAD_FLAT__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT1_PAD_FLAT,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT1_PAD_FLAT__REG, &data);
		break;
	default:
		break;
	}

	return comres;
}
#endif 
#ifdef BMA250_ENABLE_INT2
static int bma250_set_int2_pad_sel(struct i2c_client *client, unsigned char
		int2sel)
{
	int comres = 0;
	unsigned char data;
	unsigned char state;
	state = 0x01;


	switch (int2sel) {
	case 0:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_LOWG__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_LOWG,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_LOWG__REG, &data);
		break;
	case 1:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_HIGHG__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_HIGHG,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_HIGHG__REG, &data);
		break;
	case 2:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_SLOPE__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_SLOPE,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_SLOPE__REG, &data);
		break;
	case 3:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_DB_TAP__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_DB_TAP,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_DB_TAP__REG, &data);
		break;
	case 4:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_SNG_TAP__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_SNG_TAP,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_SNG_TAP__REG, &data);
		break;
	case 5:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_ORIENT__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_ORIENT,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_ORIENT__REG, &data);
		break;
	case 6:
		comres = bma250_smbus_read_byte(client,
				BMA250_EN_INT2_PAD_FLAT__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_EN_INT2_PAD_FLAT,
				state);
		comres = bma250_smbus_write_byte(client,
				BMA250_EN_INT2_PAD_FLAT__REG, &data);
		break;
	default:
		break;
	}

	return comres;
}
#endif 

static int bma250_set_Int_Enable(struct i2c_client *client, unsigned char
		InterruptType , unsigned char value)
{
	int comres = 0;
	unsigned char data1, data2;


	comres = bma250_smbus_read_byte(client, BMA250_INT_ENABLE1_REG, &data1);
	comres = bma250_smbus_read_byte(client, BMA250_INT_ENABLE2_REG, &data2);

	value = value & 1;
	switch (InterruptType) {
	case 0:
		
		data2 = BMA250_SET_BITSLICE(data2, BMA250_EN_LOWG_INT, value);
		break;
	case 1:
		

		data2 = BMA250_SET_BITSLICE(data2, BMA250_EN_HIGHG_X_INT,
				value);
		break;
	case 2:
		

		data2 = BMA250_SET_BITSLICE(data2, BMA250_EN_HIGHG_Y_INT,
				value);
		break;
	case 3:
		

		data2 = BMA250_SET_BITSLICE(data2, BMA250_EN_HIGHG_Z_INT,
				value);
		break;
	case 4:
		

		data2 = BMA250_SET_BITSLICE(data2, BMA250_EN_NEW_DATA_INT,
				value);
		break;
	case 5:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_SLOPE_X_INT,
				value);
		break;
	case 6:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_SLOPE_Y_INT,
				value);
		break;
	case 7:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_SLOPE_Z_INT,
				value);
		break;
	case 8:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_SINGLE_TAP_INT,
				value);
		break;
	case 9:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_DOUBLE_TAP_INT,
				value);
		break;
	case 10:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_ORIENT_INT, value);
		break;
	case 11:
		

		data1 = BMA250_SET_BITSLICE(data1, BMA250_EN_FLAT_INT, value);
		break;
	default:
		break;
	}
	comres = bma250_smbus_write_byte(client, BMA250_INT_ENABLE1_REG,
			&data1);
	comres = bma250_smbus_write_byte(client, BMA250_INT_ENABLE2_REG,
			&data2);

	return comres;
}


static int bma250_get_mode(struct i2c_client *client, unsigned char *Mode)
{
	int comres = 0;


	comres = bma250_smbus_read_byte(client,
			BMA250_EN_LOW_POWER__REG, Mode);
	*Mode  = (*Mode) >> 6;


	return comres;
}

static int bma250_set_range(struct i2c_client *client, unsigned char Range)
{
	int comres = 0;
	unsigned char data1;


	if (Range < 4) {
		comres = bma250_smbus_read_byte(client,
				BMA250_RANGE_SEL_REG, &data1);
		switch (Range) {
		case 0:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_RANGE_SEL, 0);
			break;
		case 1:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_RANGE_SEL, 5);
			break;
		case 2:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_RANGE_SEL, 8);
			break;
		case 3:
			data1  = BMA250_SET_BITSLICE(data1,
					BMA250_RANGE_SEL, 12);
			break;
		default:
			break;
		}
		comres += bma250_smbus_write_byte(client,
				BMA250_RANGE_SEL_REG, &data1);
	} else{
		comres = -1;
	}


	return comres;
}

static int bma250_get_range(struct i2c_client *client, unsigned char *Range)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client, BMA250_RANGE_SEL__REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_RANGE_SEL);
	*Range = data;


	return comres;
}


static int bma250_set_bandwidth(struct i2c_client *client, unsigned char BW)
{
	int comres = 0;
	unsigned char data;
	int Bandwidth = 0;


	if (BW < 8) {
		switch (BW) {
		case 0:
			Bandwidth = BMA250_BW_7_81HZ;
			break;
		case 1:
			Bandwidth = BMA250_BW_15_63HZ;
			break;
		case 2:
			Bandwidth = BMA250_BW_31_25HZ;
			break;
		case 3:
			Bandwidth = BMA250_BW_62_50HZ;
			break;
		case 4:
			Bandwidth = BMA250_BW_125HZ;
			break;
		case 5:
			Bandwidth = BMA250_BW_250HZ;
			break;
		case 6:
			Bandwidth = BMA250_BW_500HZ;
			break;
		case 7:
			Bandwidth = BMA250_BW_1000HZ;
			break;
		default:
			break;
		}
		comres = bma250_smbus_read_byte(client,
				BMA250_BANDWIDTH__REG, &data);
		data = BMA250_SET_BITSLICE(data, BMA250_BANDWIDTH,
				Bandwidth);
		comres += bma250_smbus_write_byte(client,
				BMA250_BANDWIDTH__REG, &data);
	} else{
		comres = -1;
	}


	return comres;
}

static int bma250_get_bandwidth(struct i2c_client *client, unsigned char *BW)
{
	int comres = 0;
	unsigned char data = 0;


	comres = bma250_smbus_read_byte(client, BMA250_BANDWIDTH__REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_BANDWIDTH);
	if (data <= 8) {
		*BW = 0;
	} else{
		if (data >= 0x0F)
			*BW = 7;
		else
			*BW = data - 8;

	}


	return comres;
}

#if defined(BMA250_ENABLE_INT1) || defined(BMA250_ENABLE_INT2)
static int bma250_get_interruptstatus1(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_STATUS1_REG, &data);
	*intstatus = data;

	return comres;
}


static int bma250_get_HIGH_first(struct i2c_client *client, unsigned char
						param, unsigned char *intstatus)
{
	int comres = 0;
	unsigned char data;

	switch (param) {
	case 0:
		comres = bma250_smbus_read_byte(client,
				BMA250_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA250_GET_BITSLICE(data, BMA250_HIGHG_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		comres = bma250_smbus_read_byte(client,
				BMA250_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA250_GET_BITSLICE(data, BMA250_HIGHG_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		comres = bma250_smbus_read_byte(client,
				BMA250_STATUS_ORIENT_HIGH_REG, &data);
		data = BMA250_GET_BITSLICE(data, BMA250_HIGHG_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return comres;
}

static int bma250_get_HIGH_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_HIGHG_SIGN_S);
	*intstatus = data;

	return comres;
}


static int bma250_get_slope_first(struct i2c_client *client, unsigned char
	param, unsigned char *intstatus)
{
	int comres = 0;
	unsigned char data;

	switch (param) {
	case 0:
		comres = bma250_smbus_read_byte(client,
				BMA250_STATUS_TAP_SLOPE_REG, &data);
		data = BMA250_GET_BITSLICE(data, BMA250_SLOPE_FIRST_X);
		*intstatus = data;
		break;
	case 1:
		comres = bma250_smbus_read_byte(client,
				BMA250_STATUS_TAP_SLOPE_REG, &data);
		data = BMA250_GET_BITSLICE(data, BMA250_SLOPE_FIRST_Y);
		*intstatus = data;
		break;
	case 2:
		comres = bma250_smbus_read_byte(client,
				BMA250_STATUS_TAP_SLOPE_REG, &data);
		data = BMA250_GET_BITSLICE(data, BMA250_SLOPE_FIRST_Z);
		*intstatus = data;
		break;
	default:
		break;
	}

	return comres;
}

static int bma250_get_slope_sign(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_STATUS_TAP_SLOPE_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_SLOPE_SIGN_S);
	*intstatus = data;

	return comres;
}

static int bma250_get_orient_status(struct i2c_client *client, unsigned char
		*intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_ORIENT_S);
	*intstatus = data;

	return comres;
}

static int bma250_get_orient_flat_status(struct i2c_client *client, unsigned
		char *intstatus)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_STATUS_ORIENT_HIGH_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_FLAT_S);
	*intstatus = data;

	return comres;
}
#endif 
static int bma250_set_Int_Mode(struct i2c_client *client, unsigned char Mode)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client,
			BMA250_INT_MODE_SEL__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_INT_MODE_SEL, Mode);
	comres = bma250_smbus_write_byte(client,
			BMA250_INT_MODE_SEL__REG, &data);


	return comres;
}

static int bma250_get_Int_Mode(struct i2c_client *client, unsigned char *Mode)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client,
			BMA250_INT_MODE_SEL__REG, &data);
	data  = BMA250_GET_BITSLICE(data, BMA250_INT_MODE_SEL);
	*Mode = data;


	return comres;
}
static int bma250_set_slope_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client,
			BMA250_SLOPE_DUR__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_SLOPE_DUR, duration);
	comres = bma250_smbus_write_byte(client,
			BMA250_SLOPE_DUR__REG, &data);


	return comres;
}

static int bma250_get_slope_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client,
			BMA250_SLOPE_DURN_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_SLOPE_DUR);
	*status = data;


	return comres;
}

static int bma250_set_slope_threshold(struct i2c_client *client,
		unsigned char threshold)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client,
			BMA250_SLOPE_THRES__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_SLOPE_THRES, threshold);
	comres = bma250_smbus_write_byte(client,
			BMA250_SLOPE_THRES__REG, &data);


	return comres;
}

static int bma250_get_slope_threshold(struct i2c_client *client,
		unsigned char *status)
{
	int comres = 0;
	unsigned char data;


	comres = bma250_smbus_read_byte(client,
			BMA250_SLOPE_THRES_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_SLOPE_THRES);
	*status = data;


	return comres;
}
static int bma250_set_low_g_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_LOWG_DUR__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_LOWG_DUR, duration);
	comres = bma250_smbus_write_byte(client, BMA250_LOWG_DUR__REG, &data);

	return comres;
}

static int bma250_get_low_g_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_LOW_DURN_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_LOWG_DUR);
	*status = data;

	return comres;
}

static int bma250_set_low_g_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_LOWG_THRES__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_LOWG_THRES, threshold);
	comres = bma250_smbus_write_byte(client, BMA250_LOWG_THRES__REG, &data);

	return comres;
}

static int bma250_get_low_g_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_LOW_THRES_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_LOWG_THRES);
	*status = data;

	return comres;
}

static int bma250_set_high_g_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_HIGHG_DUR__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_HIGHG_DUR, duration);
	comres = bma250_smbus_write_byte(client, BMA250_HIGHG_DUR__REG, &data);

	return comres;
}

static int bma250_get_high_g_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_HIGH_DURN_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_HIGHG_DUR);
	*status = data;

	return comres;
}

static int bma250_set_high_g_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_HIGHG_THRES__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_HIGHG_THRES, threshold);
	comres = bma250_smbus_write_byte(client, BMA250_HIGHG_THRES__REG,
			&data);

	return comres;
}

static int bma250_get_high_g_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_HIGH_THRES_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_HIGHG_THRES);
	*status = data;

	return comres;
}


static int bma250_set_tap_duration(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_DUR__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_TAP_DUR, duration);
	comres = bma250_smbus_write_byte(client, BMA250_TAP_DUR__REG, &data);

	return comres;
}

static int bma250_get_tap_duration(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_PARAM_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_TAP_DUR);
	*status = data;

	return comres;
}

static int bma250_set_tap_shock(struct i2c_client *client, unsigned char setval)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_SHOCK_DURN__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_TAP_SHOCK_DURN, setval);
	comres = bma250_smbus_write_byte(client, BMA250_TAP_SHOCK_DURN__REG,
			&data);

	return comres;
}

static int bma250_get_tap_shock(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_PARAM_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_TAP_SHOCK_DURN);
	*status = data;

	return comres;
}

static int bma250_set_tap_quiet(struct i2c_client *client, unsigned char
		duration)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_QUIET_DURN__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_TAP_QUIET_DURN, duration);
	comres = bma250_smbus_write_byte(client, BMA250_TAP_QUIET_DURN__REG,
			&data);

	return comres;
}

static int bma250_get_tap_quiet(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_PARAM_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_TAP_QUIET_DURN);
	*status = data;

	return comres;
}

static int bma250_set_tap_threshold(struct i2c_client *client, unsigned char
		threshold)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_THRES__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_TAP_THRES, threshold);
	comres = bma250_smbus_write_byte(client, BMA250_TAP_THRES__REG, &data);

	return comres;
}

static int bma250_get_tap_threshold(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_THRES_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_TAP_THRES);
	*status = data;

	return comres;
}

static int bma250_set_tap_samp(struct i2c_client *client, unsigned char samp)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_SAMPLES__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_TAP_SAMPLES, samp);
	comres = bma250_smbus_write_byte(client, BMA250_TAP_SAMPLES__REG,
			&data);

	return comres;
}

static int bma250_get_tap_samp(struct i2c_client *client, unsigned char *status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_TAP_THRES_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_TAP_SAMPLES);
	*status = data;

	return comres;
}

static int bma250_set_orient_mode(struct i2c_client *client, unsigned char mode)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_ORIENT_MODE__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_ORIENT_MODE, mode);
	comres = bma250_smbus_write_byte(client, BMA250_ORIENT_MODE__REG,
			&data);

	return comres;
}

static int bma250_get_orient_mode(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_ORIENT_PARAM_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_ORIENT_MODE);
	*status = data;

	return comres;
}

static int bma250_set_orient_blocking(struct i2c_client *client, unsigned char
		samp)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_ORIENT_BLOCK__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_ORIENT_BLOCK, samp);
	comres = bma250_smbus_write_byte(client, BMA250_ORIENT_BLOCK__REG,
			&data);

	return comres;
}

static int bma250_get_orient_blocking(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_ORIENT_PARAM_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_ORIENT_BLOCK);
	*status = data;

	return comres;
}

static int bma250_set_orient_hyst(struct i2c_client *client, unsigned char
		orienthyst)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_ORIENT_HYST__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_ORIENT_HYST, orienthyst);
	comres = bma250_smbus_write_byte(client, BMA250_ORIENT_HYST__REG,
			&data);

	return comres;
}

static int bma250_get_orient_hyst(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_ORIENT_PARAM_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_ORIENT_HYST);
	*status = data;

	return comres;
}
static int bma250_set_theta_blocking(struct i2c_client *client, unsigned char
		thetablk)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_THETA_BLOCK__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_THETA_BLOCK, thetablk);
	comres = bma250_smbus_write_byte(client, BMA250_THETA_BLOCK__REG,
			&data);

	return comres;
}

static int bma250_get_theta_blocking(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_THETA_BLOCK_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_THETA_BLOCK);
	*status = data;

	return comres;
}

static int bma250_set_theta_flat(struct i2c_client *client, unsigned char
		thetaflat)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_THETA_FLAT__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_THETA_FLAT, thetaflat);
	comres = bma250_smbus_write_byte(client, BMA250_THETA_FLAT__REG, &data);

	return comres;
}

static int bma250_get_theta_flat(struct i2c_client *client, unsigned char
		*status)
{
	int comres = 0 ;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_THETA_FLAT_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_THETA_FLAT);
	*status = data;

	return comres;
}

static int bma250_set_flat_hold_time(struct i2c_client *client, unsigned char
		holdtime)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_FLAT_HOLD_TIME__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_FLAT_HOLD_TIME, holdtime);
	comres = bma250_smbus_write_byte(client, BMA250_FLAT_HOLD_TIME__REG,
			&data);

	return comres;
}

static int bma250_get_flat_hold_time(struct i2c_client *client, unsigned char
		*holdtime)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_FLAT_HOLD_TIME_REG,
			&data);
	data  = BMA250_GET_BITSLICE(data, BMA250_FLAT_HOLD_TIME);
	*holdtime = data ;

	return comres;
}

static int bma250_write_reg(struct i2c_client *client, unsigned char addr,
		unsigned char *data)
{
	int comres = 0;
	comres = bma250_smbus_write_byte(client, addr, data);

	return comres;
}


static int bma250_set_offset_target_x(struct i2c_client *client, unsigned char
		offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client,
			BMA250_COMP_TARGET_OFFSET_X__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_COMP_TARGET_OFFSET_X,
			offsettarget);
	comres = bma250_smbus_write_byte(client,
			BMA250_COMP_TARGET_OFFSET_X__REG, &data);

	return comres;
}

static int bma250_get_offset_target_x(struct i2c_client *client, unsigned char
		*offsettarget)
{
	int comres = 0 ;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_OFFSET_PARAMS_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_COMP_TARGET_OFFSET_X);
	*offsettarget = data;

	return comres;
}

static int bma250_set_offset_target_y(struct i2c_client *client, unsigned char
		offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client,
			BMA250_COMP_TARGET_OFFSET_Y__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_COMP_TARGET_OFFSET_Y,
			offsettarget);
	comres = bma250_smbus_write_byte(client,
			BMA250_COMP_TARGET_OFFSET_Y__REG, &data);

	return comres;
}

static int bma250_get_offset_target_y(struct i2c_client *client, unsigned char
		*offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_OFFSET_PARAMS_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_COMP_TARGET_OFFSET_Y);
	*offsettarget = data;

	return comres;
}

static int bma250_set_offset_target_z(struct i2c_client *client, unsigned char
		offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client,
			BMA250_COMP_TARGET_OFFSET_Z__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_COMP_TARGET_OFFSET_Z,
			offsettarget);
	comres = bma250_smbus_write_byte(client,
			BMA250_COMP_TARGET_OFFSET_Z__REG, &data);

	return comres;
}

static int bma250_get_offset_target_z(struct i2c_client *client, unsigned char
		*offsettarget)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_OFFSET_PARAMS_REG,
			&data);
	data = BMA250_GET_BITSLICE(data, BMA250_COMP_TARGET_OFFSET_Z);
	*offsettarget = data;

	return comres;
}

static int bma250_get_cal_ready(struct i2c_client *client, unsigned char
		*calrdy)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_OFFSET_CTRL_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_FAST_COMP_RDY_S);
	*calrdy = data;

	return comres;
}

static int bma250_set_cal_trigger(struct i2c_client *client, unsigned char
		caltrigger)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_EN_FAST_COMP__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_EN_FAST_COMP, caltrigger);
	comres = bma250_smbus_write_byte(client, BMA250_EN_FAST_COMP__REG,
			&data);

	return comres;
}

static int bma250_set_selftest_st(struct i2c_client *client, unsigned char
		selftest)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_EN_SELF_TEST__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_EN_SELF_TEST, selftest);
	comres = bma250_smbus_write_byte(client, BMA250_EN_SELF_TEST__REG,
			&data);

	return comres;
}

static int bma250_set_selftest_stn(struct i2c_client *client, unsigned char stn)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client, BMA250_NEG_SELF_TEST__REG,
			&data);
	data = BMA250_SET_BITSLICE(data, BMA250_NEG_SELF_TEST, stn);
	comres = bma250_smbus_write_byte(client, BMA250_NEG_SELF_TEST__REG,
			&data);

	return comres;
}

static int bma250_set_ee_w(struct i2c_client *client, unsigned char eew)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client,
			BMA250_UNLOCK_EE_WRITE_SETTING__REG, &data);
	data = BMA250_SET_BITSLICE(data, BMA250_UNLOCK_EE_WRITE_SETTING, eew);
	comres = bma250_smbus_write_byte(client,
			BMA250_UNLOCK_EE_WRITE_SETTING__REG, &data);
	return comres;
}

static int bma250_set_ee_prog_trig(struct i2c_client *client)
{
	int comres = 0;
	unsigned char data;
	unsigned char eeprog;
	eeprog = 0x01;

	comres = bma250_smbus_read_byte(client,
			BMA250_START_EE_WRITE_SETTING__REG, &data);
	data = BMA250_SET_BITSLICE(data,
				BMA250_START_EE_WRITE_SETTING, eeprog);
	comres = bma250_smbus_write_byte(client,
			BMA250_START_EE_WRITE_SETTING__REG, &data);
	return comres;
}

static int bma250_get_eeprom_writing_status(struct i2c_client *client,
							unsigned char *eewrite)
{
	int comres = 0;
	unsigned char data;

	comres = bma250_smbus_read_byte(client,
				BMA250_EEPROM_CTRL_REG, &data);
	data = BMA250_GET_BITSLICE(data, BMA250_EE_WRITE_SETTING_S);
	*eewrite = data;

	return comres;
}

static int bma250_read_accel_x(struct i2c_client *client, short *a_x)
{
	int comres;
	unsigned char data[2];

	comres = bma250_smbus_read_byte_block(client, BMA250_ACC_X_LSB__REG,
			data, 2);
	*a_x = BMA250_GET_BITSLICE(data[0], BMA250_ACC_X_LSB) |
		(BMA250_GET_BITSLICE(data[1],
				     BMA250_ACC_X_MSB)<<BMA250_ACC_X_LSB__LEN);
	*a_x = *a_x <<
		(sizeof(short)*8-(BMA250_ACC_X_LSB__LEN+BMA250_ACC_X_MSB__LEN));
	*a_x = *a_x >>
		(sizeof(short)*8-(BMA250_ACC_X_LSB__LEN+BMA250_ACC_X_MSB__LEN));

	return comres;
}
static int bma250_read_accel_y(struct i2c_client *client, short *a_y)
{
	int comres;
	unsigned char data[2];

	comres = bma250_smbus_read_byte_block(client, BMA250_ACC_Y_LSB__REG,
			data, 2);
	*a_y = BMA250_GET_BITSLICE(data[0], BMA250_ACC_Y_LSB) |
		(BMA250_GET_BITSLICE(data[1],
				     BMA250_ACC_Y_MSB)<<BMA250_ACC_Y_LSB__LEN);
	*a_y = *a_y <<
		(sizeof(short)*8-(BMA250_ACC_Y_LSB__LEN+BMA250_ACC_Y_MSB__LEN));
	*a_y = *a_y >>
		(sizeof(short)*8-(BMA250_ACC_Y_LSB__LEN+BMA250_ACC_Y_MSB__LEN));

	return comres;
}

static int bma250_read_accel_z(struct i2c_client *client, short *a_z)
{
	int comres;
	unsigned char data[2];

	comres = bma250_smbus_read_byte_block(client, BMA250_ACC_Z_LSB__REG,
			data, 2);
	*a_z = BMA250_GET_BITSLICE(data[0], BMA250_ACC_Z_LSB) |
		BMA250_GET_BITSLICE(data[1],
				BMA250_ACC_Z_MSB)<<BMA250_ACC_Z_LSB__LEN;
	*a_z = *a_z <<
		(sizeof(short)*8-(BMA250_ACC_Z_LSB__LEN+BMA250_ACC_Z_MSB__LEN));
	*a_z = *a_z >>
		(sizeof(short)*8-(BMA250_ACC_Z_LSB__LEN+BMA250_ACC_Z_MSB__LEN));

	return comres;
}


static int bma250_read_accel_xyz(struct i2c_client *client,
		struct bma250acc *acc)
{
	int comres;
	unsigned char data[6];

	comres = bma250_smbus_read_byte_block(client,
			BMA250_ACC_X_LSB__REG, data, 6);

	acc->x = BMA250_GET_BITSLICE(data[0], BMA250_ACC_X_LSB)
		|(BMA250_GET_BITSLICE(data[1],
				BMA250_ACC_X_MSB)<<BMA250_ACC_X_LSB__LEN);
	acc->x = acc->x << (sizeof(short)*8-(BMA250_ACC_X_LSB__LEN
				+ BMA250_ACC_X_MSB__LEN));
	acc->x = acc->x >> (sizeof(short)*8-(BMA250_ACC_X_LSB__LEN
				+ BMA250_ACC_X_MSB__LEN));
	acc->y = BMA250_GET_BITSLICE(data[2], BMA250_ACC_Y_LSB)
		| (BMA250_GET_BITSLICE(data[3],
				BMA250_ACC_Y_MSB)<<BMA250_ACC_Y_LSB__LEN);
	acc->y = acc->y << (sizeof(short)*8-(BMA250_ACC_Y_LSB__LEN
				+ BMA250_ACC_Y_MSB__LEN));
	acc->y = acc->y >> (sizeof(short)*8-(BMA250_ACC_Y_LSB__LEN
				+ BMA250_ACC_Y_MSB__LEN));

	acc->z = BMA250_GET_BITSLICE(data[4], BMA250_ACC_Z_LSB)
		| (BMA250_GET_BITSLICE(data[5],
				BMA250_ACC_Z_MSB)<<BMA250_ACC_Z_LSB__LEN);
	acc->z = acc->z << (sizeof(short)*8-(BMA250_ACC_Z_LSB__LEN
				+ BMA250_ACC_Z_MSB__LEN));
	acc->z = acc->z >> (sizeof(short)*8-(BMA250_ACC_Z_LSB__LEN
				+ BMA250_ACC_Z_MSB__LEN));


	return comres;
}

static void bma250_work_func(struct work_struct *work)
{
	struct bma250_data *bma250 = container_of((struct delayed_work *)work,
			struct bma250_data, work);
	static struct bma250acc acc;
	unsigned long delay = msecs_to_jiffies(atomic_read(&bma250->delay));
	s16 data_x = 0, data_y = 0, data_z = 0;
	s16 hw_d[3] = {0};

	bma250_read_accel_xyz(bma250->bma250_client, &acc);

	hw_d[0] = acc.x + bma250->offset_buf[0];
	hw_d[1] = acc.y + bma250->offset_buf[1];
	hw_d[2] = acc.z + bma250->offset_buf[2];

	data_x = ((bma250->pdata->negate_x) ? (-hw_d[bma250->pdata->axis_map_x])
		   : (hw_d[bma250->pdata->axis_map_x]));
	data_y = ((bma250->pdata->negate_y) ? (-hw_d[bma250->pdata->axis_map_y])
		   : (hw_d[bma250->pdata->axis_map_y]));
	data_z = ((bma250->pdata->negate_z) ? (-hw_d[bma250->pdata->axis_map_z])
		   : (hw_d[bma250->pdata->axis_map_z]));

	input_report_abs(bma250->input, ABS_X, data_x);
	input_report_abs(bma250->input, ABS_Y, data_y);
	input_report_abs(bma250->input, ABS_Z, data_z);
	input_sync(bma250->input);
	mutex_lock(&bma250->value_mutex);
	bma250->value = acc;
	mutex_unlock(&bma250->value_mutex);
	schedule_delayed_work(&bma250->work, delay);
}


static ssize_t bma250_register_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int address, value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	sscanf(buf, "%d%d", &address, &value);

	if (bma250_write_reg(bma250->bma250_client, (unsigned char)address,
				(unsigned char *)&value) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_register_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	size_t count = 0;
	u8 reg[0x3d];
	int i;

	for (i = 0 ; i < 0x3d; i++) {
		bma250_smbus_read_byte(bma250->bma250_client, i, reg+i);

		count += sprintf(&buf[count], "0x%x: %d\n", i, reg[i]);
	}
	return count;


}
static ssize_t bma250_range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_range(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma250_range_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_range(bma250->bma250_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_bandwidth_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_bandwidth(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_bandwidth_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_bandwidth(bma250->bma250_client,
				(unsigned char) data) < 0)
		return -EINVAL;

	return count;
}


static ssize_t bma250_chip_layout_show(struct device *dev,	
		struct device_attribute *attr, char *buf)
{
	if (gdata == NULL) {
		E("%s: gdata == NULL\n", __func__);
		return 0;
	}

	return sprintf(buf, "chip_layout = %d\n"
			    "axis_map_x = %d, axis_map_y = %d,"
			    " axis_map_z = %d\n"
			    "negate_x = %d, negate_y = %d, negate_z = %d\n",
			    gdata->chip_layout,
			    gdata->pdata->axis_map_x, gdata->pdata->axis_map_y,
			    gdata->pdata->axis_map_z,
			    gdata->pdata->negate_x, gdata->pdata->negate_y,
			    gdata->pdata->negate_z);
}


static ssize_t bma250_get_raw_data_show(struct device *dev,	
		struct device_attribute *attr, char *buf)
{
	struct bma250_data *bma250 = gdata;
	static struct bma250acc acc;

	if (bma250 == NULL) {
		E("%s: bma250 == NULL\n", __func__);
		return 0;
	}

	bma250_read_accel_xyz(bma250->bma250_client, &acc);

	return sprintf(buf, "x = %d, y = %d, z = %d\n",
			    acc.x, acc.y, acc.z);
}


static ssize_t bma250_set_k_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250 == NULL) {
		E("%s: bma250 == NULL\n", __func__);
		return 0;
	}

	return sprintf(buf, "gs_kvalue = 0x%x\n", bma250->pdata->gs_kvalue);
}

static ssize_t bma250_set_k_value_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);
	int i = 0;

	if (bma250 == NULL) {
		E("%s: bma250 == NULL\n", __func__);
		return count;
	}

	D("%s: Set buf = %s\n", __func__, buf);

	bma250->pdata->gs_kvalue = simple_strtoul(buf, NULL, 10);

	D("%s: bma250->pdata->gs_kvalue = 0x%x\n", __func__,
		bma250->pdata->gs_kvalue);

	if ((bma250->pdata->gs_kvalue & (0x67 << 24)) != (0x67 << 24)) {
		bma250->offset_buf[0] = 0;
		bma250->offset_buf[1] = 0;
		bma250->offset_buf[2] = 0;
	} else {
		bma250->offset_buf[0] = (bma250->pdata->gs_kvalue >> 16) & 0xFF;
		bma250->offset_buf[1] = (bma250->pdata->gs_kvalue >>  8) & 0xFF;
		bma250->offset_buf[2] =  bma250->pdata->gs_kvalue        & 0xFF;

		for (i = 0; i < 3; i++) {
			if (bma250->offset_buf[i] > 127) {
				bma250->offset_buf[i] =
					bma250->offset_buf[i] - 256;
			}
		}
	}

	return count;
}


static ssize_t bma250_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_mode(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma250_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_mode(bma250->bma250_client, (unsigned char) data) < 0)
		return -EINVAL;

	return count;
}


static ssize_t bma250_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);
	struct bma250_data *bma250 = input_get_drvdata(input);
	struct bma250acc acc_value;

	mutex_lock(&bma250->value_mutex);
	acc_value = bma250->value;
	mutex_unlock(&bma250->value_mutex);

	return sprintf(buf, "%d %d %d\n", acc_value.x, acc_value.y,
			acc_value.z);
}

static ssize_t bma250_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma250->delay));

}

static ssize_t bma250_delay_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (data > BMA250_MAX_DELAY)
		data = BMA250_MAX_DELAY;
	atomic_set(&bma250->delay, (unsigned int) data);

	return count;
}


static ssize_t bma250_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma250->enable));

}

static void bma250_set_enable(struct device *dev, int enable)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);
	int pre_enable = atomic_read(&bma250->enable);
	int i = 0;

	mutex_lock(&bma250->enable_mutex);
	if (enable) {
		if (bma250->pdata->power_LPM)
			bma250->pdata->power_LPM(0);

		if (pre_enable == 0) {
			bma250_set_mode(bma250->bma250_client,
					BMA250_MODE_NORMAL);
			schedule_delayed_work(&bma250->work,
				msecs_to_jiffies(atomic_read(&bma250->delay)));
			atomic_set(&bma250->enable, 1);
		}

	} else {
		if (pre_enable == 1) {
			bma250_set_mode(bma250->bma250_client,
					BMA250_MODE_SUSPEND);
			cancel_delayed_work_sync(&bma250->work);
			atomic_set(&bma250->enable, 0);
		}

		if (bma250->pdata->power_LPM)
			bma250->pdata->power_LPM(1);
	}

	if ((bma250->pdata->gs_kvalue & (0x67 << 24)) != (0x67 << 24)) {
		bma250->offset_buf[0] = 0;
		bma250->offset_buf[1] = 0;
		bma250->offset_buf[2] = 0;
	} else {
		bma250->offset_buf[0] = (bma250->pdata->gs_kvalue >> 16) & 0xFF;
		bma250->offset_buf[1] = (bma250->pdata->gs_kvalue >>  8) & 0xFF;
		bma250->offset_buf[2] =  bma250->pdata->gs_kvalue        & 0xFF;

		for (i = 0; i < 3; i++) {
			if (bma250->offset_buf[i] > 127) {
				bma250->offset_buf[i] =
					bma250->offset_buf[i] - 256;
			}
		}
	}

	mutex_unlock(&bma250->enable_mutex);

}

static ssize_t bma250_enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if ((data == 0) || (data == 1))
		bma250_set_enable(dev, data);

	return count;
}

static ssize_t bma250_enable_int_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int type, value;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	sscanf(buf, "%d%d", &type, &value);

	if (bma250_set_Int_Enable(bma250->bma250_client, type, value) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_int_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_Int_Mode(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);
}

static ssize_t bma250_int_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_Int_Mode(bma250->bma250_client, (unsigned char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_slope_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_slope_duration(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_slope_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_slope_duration(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_slope_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_slope_threshold(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_slope_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_slope_threshold(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_high_g_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_high_g_duration(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_high_g_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_high_g_duration(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_high_g_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_high_g_threshold(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_high_g_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_high_g_threshold(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_low_g_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_low_g_duration(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_low_g_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_low_g_duration(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_low_g_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_low_g_threshold(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_low_g_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_low_g_threshold(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_tap_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_tap_threshold(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_tap_threshold_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;
	if (bma250_set_tap_threshold(bma250->bma250_client, (unsigned char)data)
			< 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_tap_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_tap_duration(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_tap_duration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_tap_duration(bma250->bma250_client, (unsigned char)data)
			< 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_tap_quiet_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_tap_quiet(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_tap_quiet_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_tap_quiet(bma250->bma250_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_tap_shock_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_tap_shock(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_tap_shock_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_tap_shock(bma250->bma250_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_tap_samp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_tap_samp(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_tap_samp_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_tap_samp(bma250->bma250_client, (unsigned char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_orient_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_orient_mode(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_orient_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_orient_mode(bma250->bma250_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_orient_blocking_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_orient_blocking(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_orient_blocking_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_orient_blocking(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_orient_hyst_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_orient_hyst(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_orient_hyst_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_orient_hyst(bma250->bma250_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_orient_theta_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_theta_blocking(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_orient_theta_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_theta_blocking(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}

static ssize_t bma250_flat_theta_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_theta_flat(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_flat_theta_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_theta_flat(bma250->bma250_client, (unsigned char)data) <
			0)
		return -EINVAL;

	return count;
}
static ssize_t bma250_flat_hold_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_flat_hold_time(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_flat_hold_time_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_flat_hold_time(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	return count;
}


static ssize_t bma250_fast_calibration_x_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_offset_target_x(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_fast_calibration_x_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_offset_target_x(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma250_set_cal_trigger(bma250->bma250_client, 1) < 0)
		return -EINVAL;

	do {
		mdelay(2);
		bma250_get_cal_ready(bma250->bma250_client, &tmp);

	
		timeout++;
		if (timeout == 50) {
			I("get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	I("x axis fast calibration finished\n");
	return count;
}

static ssize_t bma250_fast_calibration_y_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_offset_target_y(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_fast_calibration_y_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_offset_target_y(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma250_set_cal_trigger(bma250->bma250_client, 2) < 0)
		return -EINVAL;

	do {
		mdelay(2);
		bma250_get_cal_ready(bma250->bma250_client, &tmp);

	
		timeout++;
		if (timeout == 50) {
			I("get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	I("y axis fast calibration finished\n");
	return count;
}

static ssize_t bma250_fast_calibration_z_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	unsigned char data;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	if (bma250_get_offset_target_z(bma250->bma250_client, &data) < 0)
		return sprintf(buf, "Read error\n");

	return sprintf(buf, "%d\n", data);

}

static ssize_t bma250_fast_calibration_z_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	unsigned char timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;

	if (bma250_set_offset_target_z(bma250->bma250_client, (unsigned
					char)data) < 0)
		return -EINVAL;

	if (bma250_set_cal_trigger(bma250->bma250_client, 3) < 0)
		return -EINVAL;

	do {
		mdelay(2);
		bma250_get_cal_ready(bma250->bma250_client, &tmp);

	
		timeout++;
		if (timeout == 50) {
			I("get fast calibration ready error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	I("z axis fast calibration finished\n");
	return count;
}

static ssize_t bma250_selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{


	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", atomic_read(&bma250->selftest_result));

}

static ssize_t bma250_selftest_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{

	unsigned long data;
	unsigned char clear_value = 0;
	int error;
	short value1 = 0;
	short value2 = 0;
	short diff = 0;
	unsigned long result = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;


	if (data != 1)
		return -EINVAL;
	
	if (bma250_set_range(bma250->bma250_client, 0) < 0)
		return -EINVAL;

	bma250_write_reg(bma250->bma250_client, 0x32, &clear_value);

	bma250_set_selftest_st(bma250->bma250_client, 1); 
	bma250_set_selftest_stn(bma250->bma250_client, 0); 
	mdelay(10);
	bma250_read_accel_x(bma250->bma250_client, &value1);
	bma250_set_selftest_stn(bma250->bma250_client, 1); 
	mdelay(10);
	bma250_read_accel_x(bma250->bma250_client, &value2);
	diff = value1-value2;

	I("diff x is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);

	if (abs(diff) < 204)
		result |= 1;

	bma250_set_selftest_st(bma250->bma250_client, 2); 
	bma250_set_selftest_stn(bma250->bma250_client, 0); 
	mdelay(10);
	bma250_read_accel_y(bma250->bma250_client, &value1);
	bma250_set_selftest_stn(bma250->bma250_client, 1); 
	mdelay(10);
	bma250_read_accel_y(bma250->bma250_client, &value2);
	diff = value1-value2;
	I("diff y is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);
	if (abs(diff) < 204)
		result |= 2;


	bma250_set_selftest_st(bma250->bma250_client, 3); 
	bma250_set_selftest_stn(bma250->bma250_client, 0); 
	mdelay(10);
	bma250_read_accel_z(bma250->bma250_client, &value1);
	bma250_set_selftest_stn(bma250->bma250_client, 1); 
	mdelay(10);
	bma250_read_accel_z(bma250->bma250_client, &value2);
	diff = value1-value2;

	I("diff z is %d,value1 is %d, value2 is %d\n", diff,
			value1, value2);
	if (abs(diff) < 102)
		result |= 4;

	atomic_set(&bma250->selftest_result, (unsigned int)result);

	I("self test finished\n");


	return count;
}


static ssize_t bma250_eeprom_writing_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long data;
	signed char tmp;
	int timeout = 0;
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct bma250_data *bma250 = i2c_get_clientdata(client);

	error = strict_strtoul(buf, 10, &data);
	if (error)
		return error;



	if (data != 1)
		return -EINVAL;

	
	if (bma250_set_ee_w(bma250->bma250_client, 1) < 0)
		return -EINVAL;

	I("unlock eeprom successful\n");

	if (bma250_set_ee_prog_trig(bma250->bma250_client) < 0)
		return -EINVAL;
	I("start update eeprom\n");

	do {
		mdelay(2);
		bma250_get_eeprom_writing_status(bma250->bma250_client, &tmp);

		I("wait 2ms eeprom write status is %d\n", tmp);
		timeout++;
		if (timeout == 1000) {
			I("get eeprom writing status error\n");
			return -EINVAL;
		};

	} while (tmp == 0);

	I("eeprom writing is finished\n");

	
	if (bma250_set_ee_w(bma250->bma250_client, 0) < 0)
		return -EINVAL;

	I("lock eeprom successful\n");
	return count;
}


static DEVICE_ATTR(range, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_range_show, bma250_range_store);
static DEVICE_ATTR(bandwidth, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_bandwidth_show, bma250_bandwidth_store);
static DEVICE_ATTR(mode, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_mode_show, bma250_mode_store);
static DEVICE_ATTR(value, S_IRUGO,
		bma250_value_show, NULL);
static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_delay_show, bma250_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_enable_show, bma250_enable_store);
static DEVICE_ATTR(enable_int, S_IWUSR|S_IWGRP|S_IWOTH,
		NULL, bma250_enable_int_store);
static DEVICE_ATTR(int_mode, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_int_mode_show, bma250_int_mode_store);
static DEVICE_ATTR(slope_duration, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_slope_duration_show, bma250_slope_duration_store);
static DEVICE_ATTR(slope_threshold, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_slope_threshold_show, bma250_slope_threshold_store);
static DEVICE_ATTR(high_g_duration, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_high_g_duration_show, bma250_high_g_duration_store);
static DEVICE_ATTR(high_g_threshold, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_high_g_threshold_show, bma250_high_g_threshold_store);
static DEVICE_ATTR(low_g_duration, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_low_g_duration_show, bma250_low_g_duration_store);
static DEVICE_ATTR(low_g_threshold, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_low_g_threshold_show, bma250_low_g_threshold_store);
static DEVICE_ATTR(tap_duration, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_tap_duration_show, bma250_tap_duration_store);
static DEVICE_ATTR(tap_threshold, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_tap_threshold_show, bma250_tap_threshold_store);
static DEVICE_ATTR(tap_quiet, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_tap_quiet_show, bma250_tap_quiet_store);
static DEVICE_ATTR(tap_shock, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_tap_shock_show, bma250_tap_shock_store);
static DEVICE_ATTR(tap_samp, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_tap_samp_show, bma250_tap_samp_store);
static DEVICE_ATTR(orient_mode, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_orient_mode_show, bma250_orient_mode_store);
static DEVICE_ATTR(orient_blocking, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_orient_blocking_show, bma250_orient_blocking_store);
static DEVICE_ATTR(orient_hyst, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_orient_hyst_show, bma250_orient_hyst_store);
static DEVICE_ATTR(orient_theta, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_orient_theta_show, bma250_orient_theta_store);
static DEVICE_ATTR(flat_theta, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_flat_theta_show, bma250_flat_theta_store);
static DEVICE_ATTR(flat_hold_time, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_flat_hold_time_show, bma250_flat_hold_time_store);
static DEVICE_ATTR(reg, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_register_show, bma250_register_store);
static DEVICE_ATTR(fast_calibration_x, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_fast_calibration_x_show,
		bma250_fast_calibration_x_store);
static DEVICE_ATTR(fast_calibration_y, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_fast_calibration_y_show,
		bma250_fast_calibration_y_store);
static DEVICE_ATTR(fast_calibration_z, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_fast_calibration_z_show,
		bma250_fast_calibration_z_store);
static DEVICE_ATTR(selftest, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		bma250_selftest_show, bma250_selftest_store);
static DEVICE_ATTR(eeprom_writing, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
		NULL, bma250_eeprom_writing_store);

static DEVICE_ATTR(chip_layout,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
		bma250_chip_layout_show,NULL);

static DEVICE_ATTR(get_raw_data, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
		bma250_get_raw_data_show, NULL);

static DEVICE_ATTR(set_k_value, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP,
		bma250_set_k_value_show, bma250_set_k_value_store);

static struct attribute *bma250_attributes[] = {
	&dev_attr_range.attr,
	&dev_attr_bandwidth.attr,
	&dev_attr_mode.attr,
	&dev_attr_value.attr,
	&dev_attr_delay.attr,
	&dev_attr_enable.attr,
	&dev_attr_enable_int.attr,
	&dev_attr_int_mode.attr,
	&dev_attr_slope_duration.attr,
	&dev_attr_slope_threshold.attr,
	&dev_attr_high_g_duration.attr,
	&dev_attr_high_g_threshold.attr,
	&dev_attr_low_g_duration.attr,
	&dev_attr_low_g_threshold.attr,
	&dev_attr_tap_threshold.attr,
	&dev_attr_tap_duration.attr,
	&dev_attr_tap_quiet.attr,
	&dev_attr_tap_shock.attr,
	&dev_attr_tap_samp.attr,
	&dev_attr_orient_mode.attr,
	&dev_attr_orient_blocking.attr,
	&dev_attr_orient_hyst.attr,
	&dev_attr_orient_theta.attr,
	&dev_attr_flat_theta.attr,
	&dev_attr_flat_hold_time.attr,
	&dev_attr_reg.attr,
	&dev_attr_fast_calibration_x.attr,
	&dev_attr_fast_calibration_y.attr,
	&dev_attr_fast_calibration_z.attr,
	&dev_attr_selftest.attr,
	&dev_attr_eeprom_writing.attr,
	&dev_attr_chip_layout.attr,
	&dev_attr_get_raw_data.attr,
	&dev_attr_set_k_value.attr,
	NULL
};

static struct attribute_group bma250_attribute_group = {
	.attrs = bma250_attributes
};


#if defined(BMA250_ENABLE_INT1) || defined(BMA250_ENABLE_INT2)
unsigned char *orient[] = {"upward looking portrait upright",   \
	"upward looking portrait upside-down",   \
		"upward looking landscape left",   \
		"upward looking landscape right",   \
		"downward looking portrait upright",   \
		"downward looking portrait upside-down",   \
		"downward looking landscape left",   \
		"downward looking landscape right"};

static void bma250_irq_work_func(struct work_struct *work)
{
	struct bma250_data *bma250 = container_of((struct work_struct *)work,
			struct bma250_data, irq_work);

	unsigned char status = 0;
	unsigned char i;
	unsigned char first_value = 0;
	unsigned char sign_value = 0;

	bma250_get_interruptstatus1(bma250->bma250_client, &status);

	switch (status) {

	case 0x01:
		I("Low G interrupt happened\n");
		input_report_rel(bma250->input, LOW_G_INTERRUPT,
				LOW_G_INTERRUPT_HAPPENED);
		break;
	case 0x02:
		for (i = 0; i < 3; i++) {
			bma250_get_HIGH_first(bma250->bma250_client, i,
					   &first_value);
			if (first_value == 1) {

				bma250_get_HIGH_sign(bma250->bma250_client,
						   &sign_value);

				if (sign_value == 1) {
					if (i == 0)
						input_report_rel(bma250->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_X_NEGATIVE_HAPPENED);
					if (i == 1)
						input_report_rel(bma250->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Y_NEGATIVE_HAPPENED);
					if (i == 2)
						input_report_rel(bma250->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Z_NEGATIVE_HAPPENED);
				} else {
					if (i == 0)
						input_report_rel(bma250->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_X_HAPPENED);
					if (i == 1)
						input_report_rel(bma250->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Y_HAPPENED);
					if (i == 2)
						input_report_rel(bma250->input,
						HIGH_G_INTERRUPT,
					HIGH_G_INTERRUPT_Z_HAPPENED);

				}
			   }

		      I("High G interrupt happened,exis is %d,"
				      "first is %d,sign is %d\n", i,
					   first_value, sign_value);
		}
		   break;
	case 0x04:
		for (i = 0; i < 3; i++) {
			bma250_get_slope_first(bma250->bma250_client, i,
					   &first_value);
			if (first_value == 1) {

				bma250_get_slope_sign(bma250->bma250_client,
						   &sign_value);

				if (sign_value == 1) {
					if (i == 0)
						input_report_rel(bma250->input,
						SLOP_INTERRUPT,
					SLOPE_INTERRUPT_X_NEGATIVE_HAPPENED);
					else if (i == 1)
						input_report_rel(bma250->input,
						SLOP_INTERRUPT,
					SLOPE_INTERRUPT_Y_NEGATIVE_HAPPENED);
					else if (i == 2)
						input_report_rel(bma250->input,
						SLOP_INTERRUPT,
					SLOPE_INTERRUPT_Z_NEGATIVE_HAPPENED);
				} else {
					if (i == 0)
						input_report_rel(bma250->input,
								SLOP_INTERRUPT,
						SLOPE_INTERRUPT_X_HAPPENED);
					else if (i == 1)
						input_report_rel(bma250->input,
								SLOP_INTERRUPT,
						SLOPE_INTERRUPT_Y_HAPPENED);
					else if (i == 2)
						input_report_rel(bma250->input,
								SLOP_INTERRUPT,
						SLOPE_INTERRUPT_Z_HAPPENED);

				}
			}

			I("Slop interrupt happened,exis is %d,"
					"first is %d,sign is %d\n", i,
					first_value, sign_value);
		}
		break;

	case 0x10:
		I("double tap interrupt happened\n");
		input_report_rel(bma250->input, DOUBLE_TAP_INTERRUPT,
					DOUBLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x20:
		I("single tap interrupt happened\n");
		input_report_rel(bma250->input, SINGLE_TAP_INTERRUPT,
					SINGLE_TAP_INTERRUPT_HAPPENED);
		break;
	case 0x40:
		bma250_get_orient_status(bma250->bma250_client,
				    &first_value);
		I("orient interrupt happened,%s\n",
				orient[first_value]);
		if (first_value == 0)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				UPWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 1)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				UPWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 2)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				UPWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 3)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				UPWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		else if (first_value == 4)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				DOWNWARD_PORTRAIT_UP_INTERRUPT_HAPPENED);
		else if (first_value == 5)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				DOWNWARD_PORTRAIT_DOWN_INTERRUPT_HAPPENED);
		else if (first_value == 6)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				DOWNWARD_LANDSCAPE_LEFT_INTERRUPT_HAPPENED);
		else if (first_value == 7)
			input_report_abs(bma250->input, ORIENT_INTERRUPT,
				DOWNWARD_LANDSCAPE_RIGHT_INTERRUPT_HAPPENED);
		break;
	case 0x80:
		bma250_get_orient_flat_status(bma250->bma250_client,
				    &sign_value);
		I("flat interrupt happened,flat status is %d\n",
				    sign_value);
		if (sign_value == 1) {
			input_report_abs(bma250->input, FLAT_INTERRUPT,
				FLAT_INTERRUPT_TURE_HAPPENED);
		} else {
			input_report_abs(bma250->input, FLAT_INTERRUPT,
				FLAT_INTERRUPT_FALSE_HAPPENED);
		}
		break;
	default:
		break;
	}

}

static irqreturn_t bma250_irq_handler(int irq, void *handle)
{


	struct bma250_data *data = handle;


	if (data == NULL)
		return IRQ_HANDLED;
	if (data->bma250_client == NULL)
		return IRQ_HANDLED;


	schedule_work(&data->irq_work);

	return IRQ_HANDLED;


}
#endif 
static int bma250_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	unsigned char tempvalue;
	struct bma250_data *data;
	struct input_dev *dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		I("i2c_check_functionality error\n");
		goto exit;
	}
	data = kzalloc(sizeof(struct bma250_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	
	tempvalue = i2c_smbus_read_byte_data(client, BMA250_CHIP_ID_REG);

	if (tempvalue == BMA250_CHIP_ID) {
		I("Bosch Sensortec Device detected! "
				"BMA250 registered I2C driver!\n");
	} else{
		I("Bosch Sensortec Device not found"
				"i2c error %d \n", tempvalue);
		err = -ENODEV;
		goto kfree_exit;
	}
	i2c_set_clientdata(client, data);
	data->bma250_client = client;
	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);
	bma250_set_bandwidth(client, BMA250_BW_SET);
	bma250_set_range(client, BMA250_RANGE_SET);

	data->pdata = kmalloc(sizeof(*data->pdata), GFP_KERNEL);
	if (data->pdata == NULL) {
		dev_err(&client->dev,
			"failed to allocate memory for pdata: %d\n", err);
		goto pdata_kmalloc_fail;
	}

	if (client->dev.platform_data != NULL) {
		memcpy(data->pdata, client->dev.platform_data,
		       sizeof(*data->pdata));
	}

	if (data->pdata) {
		data->chip_layout = data->pdata->chip_layout;
		data->pdata->gs_kvalue = gs_kvalue;
	} else {
		data->chip_layout = 0;
		data->pdata->gs_kvalue = 0;
	}
	I("BMA250 G-sensor I2C driver: gs_kvalue = 0x%X\n",
		data->pdata->gs_kvalue);

	gdata = data;
	D("%s: layout = %d\n", __func__, gdata->chip_layout);

#if defined(BMA250_ENABLE_INT1) || defined(BMA250_ENABLE_INT2)
	bma250_set_Int_Mode(client, 1);
#endif
#ifdef BMA250_ENABLE_INT1
	
	bma250_set_int1_pad_sel(client, PAD_LOWG);
	bma250_set_int1_pad_sel(client, PAD_HIGHG);
	bma250_set_int1_pad_sel(client, PAD_SLOP);
	bma250_set_int1_pad_sel(client, PAD_DOUBLE_TAP);
	bma250_set_int1_pad_sel(client, PAD_SINGLE_TAP);
	bma250_set_int1_pad_sel(client, PAD_ORIENT);
	bma250_set_int1_pad_sel(client, PAD_FLAT);
#endif

#ifdef BMA250_ENABLE_INT2
	
	bma250_set_int2_pad_sel(client, PAD_LOWG);
	bma250_set_int2_pad_sel(client, PAD_HIGHG);
	bma250_set_int2_pad_sel(client, PAD_SLOP);
	bma250_set_int2_pad_sel(client, PAD_DOUBLE_TAP);
	bma250_set_int2_pad_sel(client, PAD_SINGLE_TAP);
	bma250_set_int2_pad_sel(client, PAD_ORIENT);
	bma250_set_int2_pad_sel(client, PAD_FLAT);
#endif

#if defined(BMA250_ENABLE_INT1) || defined(BMA250_ENABLE_INT2)
	data->IRQ = client->irq;
	err = request_irq(data->IRQ, bma250_irq_handler, IRQF_TRIGGER_RISING,
			"bma250", data);
	if (err)
		E("could not request irq\n");

	INIT_WORK(&data->irq_work, bma250_irq_work_func);
#endif

	INIT_DELAYED_WORK(&data->work, bma250_work_func);
	atomic_set(&data->delay, BMA250_MAX_DELAY);
	atomic_set(&data->enable, 0);

	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;
	dev->name = SENSOR_NAME;
	dev->id.bustype = BUS_I2C;

	input_set_capability(dev, EV_REL, LOW_G_INTERRUPT);
	input_set_capability(dev, EV_REL, HIGH_G_INTERRUPT);
	input_set_capability(dev, EV_REL, SLOP_INTERRUPT);
	input_set_capability(dev, EV_REL, DOUBLE_TAP_INTERRUPT);
	input_set_capability(dev, EV_REL, SINGLE_TAP_INTERRUPT);
	input_set_capability(dev, EV_ABS, ORIENT_INTERRUPT);
	input_set_capability(dev, EV_ABS, FLAT_INTERRUPT);
	input_set_abs_params(dev, ABS_X, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Y, ABSMIN, ABSMAX, 0, 0);
	input_set_abs_params(dev, ABS_Z, ABSMIN, ABSMAX, 0, 0);

	input_set_drvdata(dev, data);

	err = input_register_device(dev);
	if (err < 0) {
		input_free_device(dev);
		goto kfree_exit;
	}

	data->input = dev;

#ifdef HTC_ATTR
	data->g_sensor_class = class_create(THIS_MODULE, "htc_g_sensor");
	if (IS_ERR(data->g_sensor_class)) {
		err = PTR_ERR(data->g_sensor_class);
		data->g_sensor_class = NULL;
		E("%s: could not allocate data->g_sensor_class\n", __func__);
		goto err_create_class;
	}

	data->g_sensor_dev = device_create(data->g_sensor_class,
				NULL, 0, "%s", "g_sensor");
	if (unlikely(IS_ERR(data->g_sensor_dev))) {
		err = PTR_ERR(data->g_sensor_dev);
		data->g_sensor_dev = NULL;
		E("%s: could not allocate data->g_sensor_dev\n", __func__);
		goto err_create_g_sensor_device;
	}

	dev_set_drvdata(data->g_sensor_dev, data);

	err = sysfs_create_group(&data->g_sensor_dev->kobj,
			&bma250_attribute_group);
	if (err < 0)
		goto error_sysfs;

#else 

	err = sysfs_create_group(&data->input->dev.kobj,
			&bma250_attribute_group);
	if (err < 0)
		goto error_sysfs;

#endif 

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = bma250_early_suspend;
	data->early_suspend.resume = bma250_late_resume;
	register_early_suspend(&data->early_suspend);
#endif

	mutex_init(&data->value_mutex);
	mutex_init(&data->mode_mutex);
	mutex_init(&data->enable_mutex);

	I("%s: BMA250 BOSCH driver probe successful", __func__);

	return 0;


error_sysfs:
	device_unregister(data->g_sensor_dev);
err_create_g_sensor_device:
	class_destroy(data->g_sensor_class);
err_create_class:
	input_unregister_device(data->input);
	kfree(data->pdata);
pdata_kmalloc_fail:
kfree_exit:
	kfree(data);
exit:
	return err;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void bma250_early_suspend(struct early_suspend *h)
{
	struct bma250_data *data =
		container_of(h, struct bma250_data, early_suspend);

	D("%s++\n", __func__);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma250_set_mode(data->bma250_client, BMA250_MODE_SUSPEND);
		cancel_delayed_work_sync(&data->work);
	}
	mutex_unlock(&data->enable_mutex);
}


static void bma250_late_resume(struct early_suspend *h)
{
	struct bma250_data *data =
		container_of(h, struct bma250_data, early_suspend);

	D("%s++\n", __func__);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma250_set_mode(data->bma250_client, BMA250_MODE_NORMAL);
		schedule_delayed_work(&data->work,
				msecs_to_jiffies(atomic_read(&data->delay)));
	}
	mutex_unlock(&data->enable_mutex);
}
#endif

static int __devexit bma250_remove(struct i2c_client *client)
{
	struct bma250_data *data = i2c_get_clientdata(client);

	bma250_set_enable(&client->dev, 0);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&data->early_suspend);
#endif
	sysfs_remove_group(&data->input->dev.kobj, &bma250_attribute_group);
	input_unregister_device(data->input);
	kfree(data);

	return 0;
}
#ifdef CONFIG_PM

static int bma250_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bma250_data *data = i2c_get_clientdata(client);

	D("%s++\n", __func__);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma250_set_mode(data->bma250_client, BMA250_MODE_SUSPEND);
		cancel_delayed_work_sync(&data->work);
	}
	mutex_unlock(&data->enable_mutex);

	return 0;
}

static int bma250_resume(struct i2c_client *client)
{
	struct bma250_data *data = i2c_get_clientdata(client);

	D("%s++\n", __func__);

	mutex_lock(&data->enable_mutex);
	if (atomic_read(&data->enable) == 1) {
		bma250_set_mode(data->bma250_client, BMA250_MODE_NORMAL);
		schedule_delayed_work(&data->work,
				msecs_to_jiffies(atomic_read(&data->delay)));
	}
	mutex_unlock(&data->enable_mutex);

	return 0;
}

#else

#define bma250_suspend		NULL
#define bma250_resume		NULL

#endif 

static const struct i2c_device_id bma250_id[] = {
	{ SENSOR_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, bma250_id);

static struct i2c_driver bma250_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SENSOR_NAME,
	},
	.suspend	= bma250_suspend,
	.resume		= bma250_resume,
	.id_table	= bma250_id,
	.probe		= bma250_probe,
	.remove		= __devexit_p(bma250_remove),

};

static int __init BMA250_init(void)
{
	return i2c_add_driver(&bma250_driver);
}

static void __exit BMA250_exit(void)
{
	i2c_del_driver(&bma250_driver);
}

MODULE_AUTHOR("Albert Zhang <xu.zhang@bosch-sensortec.com>");
MODULE_DESCRIPTION("BMA250 accelerometer sensor driver");
MODULE_LICENSE("GPL");

module_init(BMA250_init);
module_exit(BMA250_exit);

