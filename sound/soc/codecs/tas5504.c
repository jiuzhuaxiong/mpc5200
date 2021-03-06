/*
 * Texas Instruments TAS5504 low power audio CODEC
 * ALSA SoC CODEC driver
 *
 * Copyright (C) Jon Smirl <jonsmirl@gmail.com>
 */
#define DEBUG

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/soc-of-simple.h>
#include <sound/initval.h>

#include "tas5504.h"

#define TAS_MAX_8B_REG 0x40
#define TAS_REG_MAX 0xe1

#define TAS5504_RATES (SNDRV_PCM_RATE_32000 |\
				SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |\
				SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)
#define TAS5504_FORMATS SNDRV_PCM_FMTBIT_S32

/* Size and number of variable length entries in the cache */
#define TAS_CACHE_1_MAX 41
#define TAS_CACHE_2_MAX 16
#define TAS_CACHE_3_MAX 2
#define TAS_CACHE_4_MAX 8
#define TAS_CACHE_5_MAX 30
#define TAS_CACHE_6_MAX 4

/* Size and offset of specific  entries in the cache */
static struct {
	u8 size;
	u8 offset;
} cache_map[] = {
[0x00]=	{1, 0xff},	/* TAS_REG_CLOCK_CONTROL */
[0x01]=	{1, 0xff},	/* TAS_REG_GENERAL_STATUS */
[0x02]=	{1, 0xff},	/* TAS_REG_ERROR_STATUS */
[0x03]=	{1, 0},		/* TAS_REG_SYS_CONTROL_1 */
[0x04]=	{1, 1},		/* TAS_REG_SYS_CONTROL_2 */
[0x05]=	{1, 2},		/* TAS_REG_CH_CONFIG_1 */
[0x06]=	{1, 3},		/* TAS_REG_CH_CONFIG_2 */
[0x0b]=	{1, 4},		/* TAS_REG_CH_CONFIG_3 */
[0x0c]=	{1, 5},		/* TAS_REG_CH_CONFIG_4 */
[0x0d]=	{1, 6},		/* TAS_REG_HP_CONFIG */
[0x0e]=	{1, 0xff},	/* TAS_REG_SERIAL_CONTROL */
[0x0f]=	{1, 7},		/* TAS_REG_SOFT_MUTE */
[0x14]=	{1, 8},		/* TAS_REG_AUTO_MUTE */
[0x15]=	{1, 9},		/* TAS_REG_AUTO_MUTE_PWM */
[0x16]=	{1, 10},	/* TAS_REG_MODULATE_LIMIT */
[0x1b]=	{1, 11},	/* TAS_REG_IC_DELAY_1 */
[0x1c]=	{1, 12},	/* TAS_REG_IC_DELAY_2 */
[0x21]=	{1, 13},	/* TAS_REG_IC_DELAY_3 */
[0x22]=	{1, 14},	/* TAS_REG_IC_DELAY_4 */
[0x23]=	{1, 15},	/* TAS_REG_IC_OFFSET */
[0x40]=	{1, 16},	/* TAS_REG_BANK_SWITCH */
[0x41]=	{8, 1,},	/* TAS_REG_IN8X4_1 */
[0x42]=	{8, 2,},	/* TAS_REG_IN8X4_2 */
[0x47]=	{8, 3,},	/* TAS_REG_IN8X4_3 */
[0x48]=	{8, TAS_CACHE_2_MAX,},	/* TAS_REG_IN8X4_4 */
[0x49]=	{1, 17},	/* TAS_REG_IPMIX_1_TO_CH4 */
[0x4a]=	{1, 18},	/* TAS_REG_IPMIX_2_TO_CH4 */
[0x4b]=	{1, 19},	/* TAS_REG_IPMIX_3_TO_CH2 */
[0x4c]=	{1, 20},	/* TAS_REG_CH3_BP_BQ2 */
[0x4d]=	{1, 21},	/* TAS_REG_CH3_BP */
[0x4e]=	{1, 22},	/* TAS_REG_IPMIX_4_TO_CH12 */
[0x4f]=	{1, 23},	/* TAS_REG_CH4_BP_BQ2 */
[0x50]=	{1, 24},	/* TAS_REG_CH4_BP */
[0x51]=	{5, 1,},	/* TAS_REG_CH1_BQ_1 */
[0x52]=	{5, 2,},	/* TAS_REG_CH1_BQ_2 */
[0x53]=	{5, 3,},	/* TAS_REG_CH1_BQ_3 */
[0x54]=	{5, 4,},	/* TAS_REG_CH1_BQ_4 */
[0x55]=	{5, 5,},	/* TAS_REG_CH1_BQ_5 */
[0x56]=	{5, 6,},	/* TAS_REG_CH1_BQ_6 */
[0x57]=	{5, 7,},	/* TAS_REG_CH1_BQ_7 */
[0x58]=	{5, 8,},	/* TAS_REG_CH2_BQ_1 */
[0x59]=	{5, 9,},	/* TAS_REG_CH2_BQ_2	 */
[0x5a]=	{5, 10,},	/* TAS_REG_CH2_BQ_3 */
[0x5b]=	{5, 11,},	/* TAS_REG_CH2_BQ_4 */
[0x5c]=	{5, 12,},	/* TAS_REG_CH2_BQ_5 */
[0x5d]=	{5, 13,},	/* TAS_REG_CH2_BQ_6 */
[0x5e]=	{5, 14,},	/* TAS_REG_CH2_BQ_7 */
[0x7b]=	{5, 15,},	/* TAS_REG_CH3_BQ_1 */
[0x7c]=	{5, 16,},	/* TAS_REG_CH3_BQ_2 */
[0x7d]=	{5, 17,},	/* TAS_REG_CH3_BQ_3 */
[0x7e]=	{5, 18,},	/* TAS_REG_CH3_BQ_4 */
[0x7f]=	{5, 19,},	/* TAS_REG_CH3_BQ_5 */
[0x80]=	{5, 20,},	/* TAS_REG_CH3_BQ_6 */
[0x81]=	{5, 21,},	/* TAS_REG_CH3_BQ_7 */
[0x82]=	{5, 22,},	/* TAS_REG_CH4_BQ_1 */
[0x83]=	{5, 23,},	/* TAS_REG_CH4_BQ_2 */
[0x84]=	{5, 24,},	/* TAS_REG_CH4_BQ_3 */
[0x85]=	{5, 25,},	/* TAS_REG_CH4_BQ_4 */
[0x86]=	{5, 26,},	/* TAS_REG_CH4_BQ_5 */
[0x87]=	{5, 27,},	/* TAS_REG_CH4_BQ_6 */
[0x88]=	{5, 28,},	/* TAS_REG_CH4_BQ_7 */
[0x89]=	{2, 1,},	/* TAS_REG_BT_BYPASS_CH1 */
[0x8a]=	{2, 2,},	/* TAS_REG_BT_BYPASS_CH2 */
[0x8f]=	{2, 3,},	/* TAS_REG_BT_BYPASS_CH3 */
[0x90]=	{2, 4,},	/* TAS_REG_BT_BYPASS_CH4 */
[0x91]=	{1, 25,},	/* TAS_REG_LOUDNESS_LG */
[0x92]=	{2, 5,},	/* TAS_REG_LOUDNESS_LO */
[0x93]=	{1, 26,},	/* TAS_REG_LOUDNESS_G */
[0x94]=	{2, 6,},	/* TAS_REG_LOUDNESS_O */
[0x95]=	{5, 29,},	/* TAS_REG_LOUDNESS_BQ */
[0x96]=	{1, 27,},	/* TAS_REG_DRC1_CNTL_123 */
[0x97]=	{1, 28,},	/* TAS_REG_DRC2_CNTL_4 */
[0x98]=	{2, 7,},	/* TAS_REG_DRC1_ENERGY */
[0x99]=	{4, 3,},	/* TAS_REG_DRC1_THRESHOLD */
[0x9a]=	{3, 1,},	/* TAS_REG_DRC1_SLOPE */
[0x9b]=	{4, 4,},	/* TAS_REG_DRC1_OFFSET */
[0x9c]=	{4, 5,},	/* TAS_REG_DRC1_ATTACK */
[0x9d]=	{2, 8,},	/* TAS_REG_DRC2_ENERGY */
[0x9e]=	{4, 6,},	/* TAS_REG_DRC2_THRESHOLD */
[0x9f]=	{2, TAS_CACHE_3_MAX,},	/* TAS_REG_DRC2_SLOPE */
[0xa0]=	{4, 7,},	/* TAS_REG_DRC2_OFFSET */
[0xa1]=	{4, TAS_CACHE_2_MAX,},	/* TAS_REG_DRC2_ATTACK */
[0xa2]=	{2, 9,},	/* TAS_REG_DRC_BYPASS_1 */
[0xa3]=	{2, 10,},	/* TAS_REG_DRC_BYPASS_2 */
[0xa8]=	{2, 11,},	/* TAS_REG_DRC_BYPASS_3 */
[0xa9]=	{2, 12,},	/* TAS_REG_DRC_BYPASS_4 */
[0xaa]=	{2, 13,},	/* TAS_REG_SEL_OP14_S */
[0xab]=	{2, 14,},	/* TAS_REG_SEL_OP14_T */
[0xb0]=	{2, 15,},	/* TAS_REG_SEL_OP14_Y */
[0xb1]=	{2, TAS_CACHE_2_MAX,},	/* TAS_REG_SEL_OP14_Z */
[0xcf]=	{5, TAS_CACHE_5_MAX,},	/* TAS_REG_VOLUME_BQ */
[0xd0]=	{1, 29},	/* TAS_REG_VOL_TB_SLEW */
[0xd1]=	{1, 30},	/* TAS_REG_VOL_CH1 */
[0xd2]=	{1, 31},	/* TAS_REG_VOL_CH2 */
[0xd7]=	{1, 32},	/* TAS_REG_VOL_CH3 */
[0xd8]=	{1, 33},	/* TAS_REG_VOL_CH4 */
[0xd9]=	{1, 34},	/* TAS_REG_VOL_MASTER */
[0xda]=	{1, 35},	/* TAS_REG_BASS_SET */
[0xdb]=	{1, 36},	/* TAS_REG_BASS_INDEX */
[0xdc]=	{1, 37},	/* TAS_REG_TREBLE_SET */
[0xdd]=	{1, 38},	/* TAS_REG_TREBLE_INDEX */
[0xde]=	{1, 39},	/* TAS_REG_AM_MODE */
[0xdf]=	{1, 40},	/* TAS_REG_PSVC */
[0xe0]=	{1, TAS_CACHE_1_MAX,},	/* TAS_REG_GENERAL_CONTROL */
};

/* location of each variable length segment in the cache array */
#define TAS_CACHE_1_BASE 0
#define TAS_CACHE_2_BASE TAS_CACHE_1_BASE + (TAS_CACHE_1_MAX + 1)
#define TAS_CACHE_3_BASE TAS_CACHE_2_BASE + (TAS_CACHE_2_MAX + 1) * 2
#define TAS_CACHE_4_BASE TAS_CACHE_3_BASE + (TAS_CACHE_3_MAX + 1) * 3
#define TAS_CACHE_5_BASE TAS_CACHE_4_BASE + (TAS_CACHE_4_MAX + 1) * 4
#define TAS_CACHE_8_BASE TAS_CACHE_5_BASE + (TAS_CACHE_5_MAX + 1) * 5
#define TAS_CACHE_MAX TAS_CACHE_8_BASE + (TAS_CACHE_5_MAX + 1) * 8

/* tas5504 driver private data */
struct tas5504_priv {
	struct i2c_client *client;
	struct snd_soc_codec codec;

	/* performance shadow of i2c registers */
	u32 reg_cache[TAS_CACHE_MAX];
	u8 valid[TAS_CACHE_MAX];
};

/* ---------------------------------------------------------------------
 * Register access routines
 */

static u32 *tas5504_get_base(struct tas5504_priv *priv, unsigned int reg)
{
	u32 *base;

	switch (cache_map[reg].size) {
	case 1: base = &priv->reg_cache[TAS_CACHE_1_BASE]; break;
	case 2: base = &priv->reg_cache[TAS_CACHE_2_BASE]; break;
	case 3: base = &priv->reg_cache[TAS_CACHE_3_BASE]; break;
	case 4: base = &priv->reg_cache[TAS_CACHE_4_BASE]; break;
	case 5: base = &priv->reg_cache[TAS_CACHE_5_BASE]; break;
	case 8: base = &priv->reg_cache[TAS_CACHE_8_BASE]; break;
	default: return 0;
	}
	base += cache_map[reg].offset * cache_map[reg].size;

	return base;
}

static void tas5504_update_cache(struct tas5504_priv *priv, u8 reg, u32 *value, unsigned int count)
{
	if (cache_map[reg].offset != 0xFF) /* not volatile, cache it */
		priv->valid[reg] = 1;

	memmove(tas5504_get_base(priv, reg), value, cache_map[reg].size * sizeof *value * count);
}

static void tas5504_reg_read(struct snd_soc_codec *codec, u8 reg)
{
	struct tas5504_priv *priv = codec->private_data;
	u8 value[32];
	int rc;
	struct i2c_msg msgs[2] = {
		{.addr = priv->client->addr, .flags = 0, .len = 1, .buf = &reg},
		{.addr = priv->client->addr, .flags = I2C_M_RD, .buf = &value[0]}
	};

	if (reg < TAS_MAX_8B_REG) /* special case 8 bit regs */
		msgs[1].len = 1;
	else
		msgs[1].len = cache_map[reg].size * 4;

	rc = i2c_transfer(priv->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (rc != 2) {
		dev_err(&priv->client->dev, "%s: rc=%d reg=%02x rc != size\n",
			__func__, rc, reg);
		return;
	}
	if (reg < TAS_MAX_8B_REG) /* convert 8b result to 32b */
		*(u32 *)&value[0] = value[0];

	tas5504_update_cache(priv, reg, (u32*)&value[0], cache_map[reg].size);
}

static unsigned int tas5504_reg_read_cache(struct snd_soc_codec *codec,
					 unsigned int reg)
{
	struct tas5504_priv *priv = codec->private_data;

	if (reg > TAS_REG_MAX)
		return -1;

	/* can only read 32 bit registers, 0xFF is volatile 32b */
	if (!((cache_map[reg].size == 1) || (cache_map[reg].size == 0xFF)))
		return -1;

	if (!priv->valid[reg])
		tas5504_reg_read(codec, reg);

	return *tas5504_get_base(priv, reg);
}

static int tas5504_reg_write_long(struct snd_soc_codec *codec, unsigned int reg,
		           u32 *value, unsigned int count)
{
	struct tas5504_priv *priv = codec->private_data;
	u8 buf[33];
	int rc, size;

	memmove(tas5504_get_base(priv, reg), value, count * sizeof *value);

	buf[0] = reg;
	size = 1;
	if (reg < TAS_MAX_8B_REG) {
		buf[1] = *value;
		size += 1;
	} else {
		size += count * sizeof *value;
		memmove(&buf[1], value, size);
	}
	rc = i2c_master_send(priv->client, buf, size);
	if (rc != size) {
		dev_err(&priv->client->dev,
			"%s: rc=%d reg=%02x size %02x rc != size\n",
			__func__, rc, reg, size);
		return -EIO;
	}
	return 0;
}

static int tas5504_reg_write(struct snd_soc_codec *codec, unsigned int reg,
		           unsigned int value)
{
	if (reg > TAS_REG_MAX)
		return -EINVAL;

	/* can only read 32 bit registers, 0xFF is a volatile 32b */
	if (!((cache_map[reg].size == 1) || (cache_map[reg].size == 0xFF)))
		return -EINVAL;

	return tas5504_reg_write_long(codec, reg, &value, 1);
}

/* ---------------------------------------------------------------------
 * Digital Audio Interface Operations
 */
static int tas5504_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct tas5504_priv *priv = codec->private_data;

	dev_dbg(&priv->client->dev, "tas5504_hw_params(substream=%p, params=%p)\n",
		substream, params);
	dev_dbg(&priv->client->dev, "rate=%i format=%i\n", params_rate(params),
		params_format(params));

	return 0;
}

/**
 * tas5504_mute - Mute control to reduce noise when changing audio format
 */
static int tas5504_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tas5504_priv *priv = codec->private_data;

	dev_dbg(&priv->client->dev, "tas5504_mute(dai=%p, mute=%i)\n",
		dai, mute);

	return 0;
}

/* ---------------------------------------------------------------------
 * Digital Audio Interface Definition
 */

static struct snd_soc_dai_ops tas5504_dai_ops = {
	.hw_params = tas5504_hw_params,
	.digital_mute = tas5504_mute,
};

struct snd_soc_dai tas5504_dai = {
	.name = "tas5504",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TAS5504_RATES,
		.formats = TAS5504_FORMATS,
	},
	.ops = &tas5504_dai_ops,
};
EXPORT_SYMBOL_GPL(tas5504_dai);

/* ---------------------------------------------------------------------
 * ALSA controls
 */

static int tas5504_info_ctl_1(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_1_64(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER64;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_2(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_2_64(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER64;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_3(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 3;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_4(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_5(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 5;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_info_ctl_8(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 8;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = -1;
	uinfo->value.integer.step = 1;
	return 0;
}

static int tas5504_get_reg(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct tas5504_priv *priv = snd_kcontrol_chip(kcontrol);
	struct snd_ctl_elem_info uinfo;
	int reg, count;

	reg = kcontrol->private_value;
	kcontrol->info(kcontrol, &uinfo);
	count = (uinfo.type == SNDRV_CTL_ELEM_TYPE_INTEGER64 ? 2 : 1);
	count *= uinfo.count;

	if (!priv->valid[reg])
		tas5504_reg_read(&priv->codec, reg);

	memmove(tas5504_get_base(priv, reg), &ucontrol->value.integer.value[0], count);
	return 0;
}

static int tas5504_put_reg(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct tas5504_priv *priv = snd_kcontrol_chip(kcontrol);
	struct snd_ctl_elem_info uinfo;
	int reg, count;

	reg = kcontrol->private_value;
	kcontrol->info(kcontrol, &uinfo);
	count = (uinfo.type == SNDRV_CTL_ELEM_TYPE_INTEGER64 ? 2 : 1);
	count *= uinfo.count;

	return tas5504_reg_write_long(&priv->codec, reg, (u32 *)&ucontrol->value.integer.value[0], count);
}

#define TAS_CTL_1(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_1, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_1R(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ, \
  .info = tas5504_info_ctl_1, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_1_64(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_1_64, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_2(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_2, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_2_64(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_2_64, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_3(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_3, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_4(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_4, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_5(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_5, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

#define TAS_CTL_8(xregister, xsubdevice, xindex, xname) \
{ .iface = SNDRV_CTL_ELEM_IFACE_HWDEP, \
  .name = xname, \
  .index = xindex, \
  .device = 0, \
  .subdevice = xsubdevice, \
  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
  .info = tas5504_info_ctl_8, \
  .get = tas5504_get_reg, \
  .put = tas5504_put_reg, \
  .private_value = xregister \
}

static const struct snd_kcontrol_new tas5504_snd_controls[] = {
	TAS_CTL_1R(0x00, 0, 0, "Clock Control"),
	TAS_CTL_1R(0x01, 0, 0, "General Status"),
	TAS_CTL_1(0x02, 0, 0, "Error Status"),
	TAS_CTL_1(0x03, 0, 0, "System Control 1"),
	TAS_CTL_1(0x04, 0, 0, "System Control 2"),
	TAS_CTL_1(0x05, 1, 0, "General Config"),
	TAS_CTL_1(0x06, 2, 0, "General Config"),
	TAS_CTL_1(0x0b, 3, 0, "General Config"),
	TAS_CTL_1(0x0c, 4, 0, "General Config"),
	TAS_CTL_1(0x0d, 0, 0, "Headphone Config"),
	TAS_CTL_1R(0x0e, 0, 0, "Serial Data Interface"),
	TAS_CTL_1(0x0f, 0, 0, "Soft Mute"),
	TAS_CTL_1(0x14, 0, 0, "Auto Mute"),
	TAS_CTL_1(0x15, 0, 0, "Auto Mute PWM"),
	TAS_CTL_1R(0x16, 0, 0, "Modulation"),
	TAS_CTL_1(0x1b, 1, 0, "Interchannel Delay"),
	TAS_CTL_1(0x1c, 2, 0, "Interchannel Delay"),
	TAS_CTL_1(0x21, 3, 0, "Interchannel Delay"),
	TAS_CTL_1(0x22, 4, 0, "Interchannel Delay"),
	TAS_CTL_1(0x23, 0, 0, "Interchannel Offset"),
	TAS_CTL_1(0x40, 4, 0, "Bank Switching"),
	TAS_CTL_8(0x41, 1, 0, "Input Mixer"),
	TAS_CTL_8(0x42, 2, 0, "Input Mixer"),
	TAS_CTL_8(0x47, 3, 0, "Input Mixer"),
	TAS_CTL_8(0x48, 4, 0, "Input Mixer"),
	TAS_CTL_1(0x49, 0, 0, "Bass ipmix_1_to_ch4"),
	TAS_CTL_1(0x4a, 0, 0, "Bass ipmix_2_to_ch4"),
	TAS_CTL_1(0x4b, 0, 0, "Bass ipmix_3_to_ch12"),
	TAS_CTL_1(0x4c, 0, 0, "Bass ch3_bp_bq2"),
	TAS_CTL_1(0x4d, 0, 0, "Bass ch3_bq2"),
	TAS_CTL_1(0x4e, 0, 0, "Bass ipmix_4_to_ch12"),
	TAS_CTL_1(0x4f, 0, 0, "Bass ch4_bp_bq2"),
	TAS_CTL_1(0x50, 0, 0, "Bass ch4_bq2"),
	TAS_CTL_5(0x51, 1, 1, "Biquad"),
	TAS_CTL_5(0x52, 1, 2, "Biquad"),
	TAS_CTL_5(0x53, 1, 3, "Biquad"),
	TAS_CTL_5(0x54, 1, 4, "Biquad"),
	TAS_CTL_5(0x55, 1, 5, "Biquad"),
	TAS_CTL_5(0x56, 1, 6, "Biquad"),
	TAS_CTL_5(0x57, 1, 7, "Biquad"),
	TAS_CTL_5(0x58, 2, 1, "Biquad"),
	TAS_CTL_5(0x59, 2, 2, "Biquad"),
	TAS_CTL_5(0x5a, 2, 3, "Biquad"),
	TAS_CTL_5(0x5b, 2, 4, "Biquad"),
	TAS_CTL_5(0x5c, 2, 5, "Biquad"),
	TAS_CTL_5(0x5d, 2, 6, "Biquad"),
	TAS_CTL_5(0x5e, 2, 7, "Biquad"),
	TAS_CTL_5(0x7b, 3, 1, "Biquad"),
	TAS_CTL_5(0x7c, 3, 2, "Biquad"),
	TAS_CTL_5(0x7d, 3, 3, "Biquad"),
	TAS_CTL_5(0x7e, 3, 4, "Biquad"),
	TAS_CTL_5(0x7f, 3, 5, "Biquad"),
	TAS_CTL_5(0x80, 3, 6, "Biquad"),
	TAS_CTL_5(0x81, 3, 7, "Biquad"),
	TAS_CTL_5(0x82, 4, 1, "Biquad"),
	TAS_CTL_5(0x83, 4, 2, "Biquad"),
	TAS_CTL_5(0x84, 4, 3, "Biquad"),
	TAS_CTL_5(0x85, 4, 4, "Biquad"),
	TAS_CTL_5(0x86, 4, 5, "Biquad"),
	TAS_CTL_5(0x87, 4, 6, "Biquad"),
	TAS_CTL_5(0x88, 4, 7, "Biquad"),
	TAS_CTL_2(0x89, 1, 0, "Bass/Treble Bypass"),
	TAS_CTL_2(0x8a, 2, 0, "Bass/Treble Bypass"),
	TAS_CTL_2(0x8f, 3, 0, "Bass/Treble Bypass"),
	TAS_CTL_2(0x90, 4, 0, "Bass/Treble Bypass"),
	TAS_CTL_1(0x91, 0, 0, "Loudness Log2 Gain"),
	TAS_CTL_1_64(0x92, 0, 0, "Loudness Log2 Offset"),
	TAS_CTL_1(0x93, 0, 0, "Loudness Gain"),
	TAS_CTL_1_64(0x94, 0, 0, "Loudness Offset"),
	TAS_CTL_5(0x95, 0, 0, "Loudness Biquad"),
	TAS_CTL_1(0x96, 0, 0, "DRC Control 1-3"),
	TAS_CTL_1(0x97, 0, 0, "DRC Control 4"),
	TAS_CTL_2(0x97, 0, 0, "DRC Energy"),
	TAS_CTL_2_64(0x97, 0, 0, "DRC Threshold"),
	TAS_CTL_3(0x97, 0, 0, "DRC Slope"),
	TAS_CTL_2_64(0x97, 0, 0, "DRC Offset"),
	TAS_CTL_4(0x97, 0, 0, "DRC Attack/Delay"),
	TAS_CTL_2(0x9d, 4, 0, "DRC Energy"),
	TAS_CTL_2_64(0x9e, 4, 0, "DRC Threshold"),
	TAS_CTL_3(0x9f, 4, 0, "DRC Slope"),
	TAS_CTL_2_64(0xa0, 4, 0, "DRC Offset"),
	TAS_CTL_4(0xa1, 4, 0, "DRC Attack/Delay"),
	TAS_CTL_2(0xa2, 1, 0, "DRC Bypass"),
	TAS_CTL_2(0xa3, 2, 0, "DRC Bypass"),
	TAS_CTL_2(0xa8, 3, 0, "DRC Bypass"),
	TAS_CTL_2(0xa9, 4, 0, "DRC Bypass"),
	TAS_CTL_2(0xaa, 1, 0, "Output Mixer"),
	TAS_CTL_2(0xab, 2, 0, "Output Mixer"),
	TAS_CTL_3(0xb0, 3, 0, "Output Mixer"),
	TAS_CTL_3(0xb1, 4, 0, "Output Mixer"),
	TAS_CTL_5(0xcf, 0, 0, "Volume Biquad"),
	TAS_CTL_1(0xd0, 0, 0, "Volume Slew"),
	TAS_CTL_1(0xd1, 1, 0, "Volume"),
	TAS_CTL_1(0xd2, 2, 0, "Volume"),
	TAS_CTL_1(0xd7, 3, 0, "Volume"),
	TAS_CTL_1(0xd8, 4, 0, "Volume"),
	SOC_SINGLE("PCM Playback Volume", 0xd9, 0, 0x3ff, 1),
	TAS_CTL_1(0xda, 0, 0, "Bass Filter Set"),
	TAS_CTL_1(0xdb, 0, 0, "Bass Filter Index"),
	TAS_CTL_1(0xdc, 0, 0, "Treble Filter Set"),
	TAS_CTL_1(0xdd, 0, 0, "Treble Filter Index"),
	TAS_CTL_1(0xde, 0, 0, "AM Mode"),
	TAS_CTL_1(0xdf, 0, 0, "PSCV Range"),
	TAS_CTL_1(0xe0, 0, 0, "General Control"),
};

/* ---------------------------------------------------------------------
 * SoC CODEC portion of driver: probe and release routines
 */
static int tas5504_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	struct snd_kcontrol *kcontrol;
	struct tas5504_priv *priv;
	int i, ret, err;

	dev_info(&pdev->dev, "Probing tas5504 SoC CODEC driver\n");
	dev_dbg(&pdev->dev, "socdev=%p\n", socdev);
	dev_dbg(&pdev->dev, "codec_data=%p\n", socdev->codec_data);

	/* Fetch the relevant tas5504 private data here (it's already been
	 * stored in the .codec pointer) */
	priv = socdev->codec_data;
	if (priv == NULL) {
		dev_err(&pdev->dev, "tas5504: missing codec pointer\n");
		return -ENODEV;
	}
	codec = &priv->codec;
	socdev->card->codec = codec;

	dev_dbg(&pdev->dev, "Registering PCMs, dev=%p, socdev->dev=%p\n",
		&pdev->dev, socdev->dev);
	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(&pdev->dev, "tas5504: failed to create pcms\n");
		return -ENODEV;
	}

	/* register controls */
	dev_dbg(&pdev->dev, "Registering controls\n");
	for (i = 0; i < ARRAY_SIZE(tas5504_snd_controls); i++) {
		kcontrol = snd_ctl_new1(&tas5504_snd_controls[i], codec);
		err = snd_ctl_add(codec->card, kcontrol);
		if (err < 0)
			dev_err(&pdev->dev, "tas5504: failed to create control %x\n", err);
	}

	/* CODEC is setup, we can register the card now */
	dev_dbg(&pdev->dev, "Registering card\n");
	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "tas5504: failed to register card\n");
		goto card_err;
	}
	return 0;

 card_err:
	snd_soc_free_pcms(socdev);
	return ret;
}

static int tas5504_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	snd_soc_free_pcms(socdev);
	return 0;
}

struct snd_soc_codec_device tas5504_soc_codec_dev = {
	.probe = tas5504_probe,
	.remove = tas5504_remove,
};
EXPORT_SYMBOL_GPL(tas5504_soc_codec_dev);

int tas5504_display_register(struct snd_soc_codec *codec, char *buffer, unsigned int limit, unsigned int reg)
{
	struct tas5504_priv *priv = codec->private_data;
	int i, count;
	u32* base;

	reg &= 0xFF;

	if (reg >= TAS_REG_MAX)
		return 0;

	if (cache_map[reg].size == 0) /* sparse reg does not exist */
		return 0;

	if (!priv->valid[reg])	/* ensure it is valid */
		tas5504_reg_read(codec, reg);

	base = tas5504_get_base(priv, reg);

	count = snprintf(buffer, limit, "%2x: ", reg);
	if (reg < TAS_MAX_8B_REG)
		count += snprintf(buffer + count, limit - count, " %.2x", *base);
	else
		for (i = 0; i < cache_map[reg].size; i++, base++)
			count += snprintf(buffer + count, limit - count, " %.8x", *base);
	count += snprintf(buffer + count, limit - count, "\n");

	return count;
}

/* ---------------------------------------------------------------------
 * i2c device portion of driver: probe and release routines and i2c
 * 				 driver registration.
 */
static int tas5504_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct tas5504_priv *priv;

	dev_dbg(&client->dev, "probing tas5504 i2c device\n");

	/* Allocate driver data */
	priv = kzalloc(sizeof *priv, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Initialize the driver data */
	priv->client = client;
	i2c_set_clientdata(client, priv);

	/* Setup what we can in the codec structure so that the register
	 * access functions will work as expected.  More will be filled
	 * out when it is probed by the SoC CODEC part of this driver */
	priv->codec.private_data = priv;
	priv->codec.name = "tas5504";
	priv->codec.owner = THIS_MODULE;
	priv->codec.dai = &tas5504_dai;
	priv->codec.num_dai = 1;
	priv->codec.read = tas5504_reg_read_cache;
	priv->codec.write = tas5504_reg_write;
	priv->codec.reg_cache_size = TAS_REG_MAX;
	priv->codec.display_register = tas5504_display_register;

	mutex_init(&priv->codec.mutex);
	INIT_LIST_HEAD(&priv->codec.dapm_widgets);
	INIT_LIST_HEAD(&priv->codec.dapm_paths);

	dev_dbg(&client->dev, "I2C device initialized\n");
	return 0;
}

static int tas5504_i2c_remove(struct i2c_client *client)
{
	struct tas5504_priv *priv = dev_get_drvdata(&client->dev);

	kfree(priv);

	return 0;
}

static const struct i2c_device_id tas5504_device_id[] = {
	{ "tas5504", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tas5504_device_id);

static struct i2c_driver tas5504_driver = {
	.driver		= {
		.name	= "tas5504",
		.owner		= THIS_MODULE,
	},
	.probe		= tas5504_i2c_probe,
	.remove		= __devexit_p(tas5504_i2c_remove),
	.id_table	= tas5504_device_id,
};

static __init int tas5504_driver_init(void)
{
	snd_soc_register_dai(&tas5504_dai);
	return i2c_add_driver(&tas5504_driver);
}

static __exit void tas5504_driver_exit(void)
{
	i2c_del_driver(&tas5504_driver);
}

module_init(tas5504_driver_init);
module_exit(tas5504_driver_exit);

MODULE_AUTHOR("Jon Smirl");
MODULE_DESCRIPTION("TAS5504 codec module");
MODULE_LICENSE("GPL");
