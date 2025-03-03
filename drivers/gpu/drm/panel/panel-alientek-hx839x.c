// SPDX-License-Identifier: GPL-2.0+

/*
 * 	ALIENTEK MIPI DSI LCD 720x1280 and 1080x1920
 *  Copyright (C) ALIENTEK 2023
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

/* User Define command set */
#define UD_SETADDRESSMODE	0x36 /* Set address mode */
#define UD_SETSEQUENCE		0xB0 /* Set sequence */
#define UD_SETPOWER			0xB1 /* Set power */
#define UD_SETDISP			0xB2 /* Set display related register */
#define UD_SETCYC			0xB4 /* Set display waveform cycles */
#define UD_SETVCOM			0xB6 /* Set VCOM voltage */
#define UD_SETTE			0xB7 /* Set internal TE function */
#define UD_SETSENSOR		0xB8 /* Set temperature sensor */
#define UD_SETEXTC			0xB9 /* Set extension command */
#define UD_SETMIPI			0xBA /* Set MIPI control */
#define UD_SETOTP			0xBB /* Set OTP */
#define UD_SETREGBANK		0xBD /* Set register bank */
#define UD_SETDGCLUT		0xC1 /* Set DGC LUT */
#define UD_SETID			0xC3 /* Set ID */
#define UD_SETDDB			0xC4 /* Set DDB */
#define UD_SETCABC			0xC9 /* Set CABC control */
#define UD_SETCABCGAIN		0xCA
#define UD_SETPANEL			0xCC
#define UD_SETOFFSET		0xD2
#define UD_SETGIP0			0xD3 /* Set GIP Option0 */
#define UD_SETGIP1			0xD5 /* Set GIP Option1 */
#define UD_SETGIP2			0xD6 /* Set GIP Option2 */
#define UD_SETGIP3			0xD8 /* Set GIP Option2 */
#define UD_SETGPO			0xD9
#define UD_SETSCALING		0xDD
#define UD_SETIDLE			0xDF
#define UD_SETGAMMA			0xE0 /* Set gamma curve related setting */
#define UD_SETCHEMODE_DYN	0xE4
#define UD_SETCHE			0xE5
#define UD_SETCESEL			0xE6 /* Enable color enhance */
#define UD_SET_SP_CMD		0xE9
#define UD_SETREADINDEX		0xFE /* Set SPI Read Index */
#define UD_GETSPIREAD		0xFF /* SPI Read Command Data */

struct hx839x {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	bool prepared;
	bool enabled;
};

/* atk add */
enum alientek_lcd_select {
    atk_imx93_mipi_dsi_5x5_720x1280 = 2,
	atk_imx93_mipi_dsi_5x5_1080x1920 = 3,
};
/* atk add end */

/* atk add */
static const struct drm_display_mode atk_imx93_mipi_dsi_5x5_720x1280_mode = {
	.clock = 65000,					/* pixclk	*/
	.hdisplay = 720,				/* 720x1280	*/
	.hsync_start = 720 + 52,		/* LCD X 轴+hbp的像素个数 */
	.hsync_end = 720 + 52 + 8,		/* LCD X 轴+hbp+hspw的像素个数 */
	.htotal = 720 + 52 + 8 + 48,	/* LCD X 轴+hbp+hspw+hfp的像素个数 */
	.vdisplay = 1280,
	.vsync_start = 1280 + 15,		/* LCD Y 轴+vbp的像素个数 */
	.vsync_end = 1280 + 15 + 6,		/* LCD Y 轴+vbp+vspw的像素个数 */
	.vtotal = 1280 + 15 + 6 + 16,	/* LCD Y 轴+vbp+vspw+vfp的像素个数 */
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct drm_display_mode atk_imx93_mipi_dsi_5x5_1080x1920_mode = {
	.clock = 121000,				/* pixclk	55 Hz*/
	.hdisplay = 1080,				/* 1080x1920	*/
	.hsync_start = 1080 + 22,		/* LCD X 轴+hbp 的像素个数 */
	.hsync_end = 1080 + 22 + 20,		/* LCD X 轴+hbp+hspw的像素个数 */
	.htotal = 1080 + 22 + 20 + 22,	/* LCD X 轴+hbp+hspw+hfp的像素个数 */
	.vdisplay = 1920,
	.vsync_start = 1920 + 9,		/* LCD Y 轴+vbp的像素个数 */
	.vsync_end = 1920 + 9 + 7,		/* LCD Y 轴+vbp+vspw的像素个数 */
	.vtotal = 1920 + 9 + 7 + 7,	/* LCD Y 轴+vbp+vspw+vfp的像素个数 */
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};
/* atk add end */

static inline struct hx839x *panel_to_hx839x(struct drm_panel *panel)
{
	return container_of(panel, struct hx839x, panel);
}

static void hx839x_dcs_write_buf(struct hx839x *ctx, const void *data,
				  size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int err;

	err = mipi_dsi_dcs_write_buffer(dsi, data, len);
	if (err < 0)
		dev_err_ratelimited(ctx->dev, "MIPI DSI DCS write buffer failed: %d\n", err);
}

#define dcs_write_seq(ctx, seq...)				\
({								\
	static const u8 d[] = { seq };				\
								\
	hx839x_dcs_write_buf(ctx, d, ARRAY_SIZE(d));		\
})

/* atk add */
static void hx839x_init_sequence_720p(struct hx839x *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	u8 mipi_data[] = {UD_SETMIPI, 0x60, 0x03, 0x68, 0x6B, 0xB2, 0xC0};

	dcs_write_seq(ctx, UD_SETADDRESSMODE, 0x01);

	dcs_write_seq(ctx, UD_SETEXTC, 0xFF, 0x83, 0x94);

	/* SETMIPI */
	mipi_data[1] = 0x60 | (dsi->lanes - 1);
	hx839x_dcs_write_buf(ctx, mipi_data, ARRAY_SIZE(mipi_data));

	dcs_write_seq(ctx, UD_SETPOWER, 0x48, 0x12, 0x72, 0x09, 0x32, 0x54,
		      0x71, 0x71, 0x57, 0x47);

	dcs_write_seq(ctx, UD_SETDISP, 0x00, 0x80, 0x64, 0x0C, 0x0D, 0x2F);

	dcs_write_seq(ctx, UD_SETCYC, 0x73, 0x74, 0x73, 0x74, 0x73, 0x74, 0x01,
		      0x0C, 0x86, 0x75, 0x00, 0x3F, 0x73, 0x74, 0x73, 0x74,
		      0x73, 0x74, 0x01, 0x0C, 0x86);

	dcs_write_seq(ctx, UD_SETGIP0, 0x00, 0x00, 0x07, 0x07, 0x40, 0x07, 0x0C,
		      0x00, 0x08, 0x10, 0x08, 0x00, 0x08, 0x54, 0x15, 0x0A,
		      0x05, 0x0A, 0x02, 0x15, 0x06, 0x05, 0x06, 0x47, 0x44,
		      0x0A, 0x0A, 0x4B, 0x10, 0x07, 0x07, 0x0C, 0x40);

	dcs_write_seq(ctx, UD_SETGIP1, 0x1C, 0x1C, 0x1D, 0x1D, 0x00, 0x01, 0x02,
		      0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
		      0x24, 0x25, 0x18, 0x18, 0x26, 0x27, 0x18, 0x18, 0x18,
		      0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		      0x18, 0x18, 0x18, 0x18, 0x20, 0x21, 0x18, 0x18, 0x18,
		      0x18);

	dcs_write_seq(ctx, UD_SETGIP2, 0x1C, 0x1C, 0x1D, 0x1D, 0x07, 0x06, 0x05,
		      0x04, 0x03, 0x02, 0x01, 0x00, 0x0B, 0x0A, 0x09, 0x08,
		      0x21, 0x20, 0x18, 0x18, 0x27, 0x26, 0x18, 0x18, 0x18,
		      0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
		      0x18, 0x18, 0x18, 0x18, 0x25, 0x24, 0x18, 0x18, 0x18,
		      0x18);

	dcs_write_seq(ctx, UD_SETVCOM, 0x6E, 0x6E);

	dcs_write_seq(ctx, UD_SETGAMMA, 0x00, 0x0A, 0x15, 0x1B, 0x1E, 0x21,
		      0x24, 0x22, 0x47, 0x56, 0x65, 0x66, 0x6E, 0x82, 0x88,
		      0x8B, 0x9A, 0x9D, 0x98, 0xA8, 0xB9, 0x5D, 0x5C, 0x61,
		      0x66, 0x6A, 0x6F, 0x7F, 0x7F, 0x00, 0x0A, 0x15, 0x1B,
		      0x1E, 0x21, 0x24, 0x22, 0x47, 0x56, 0x65, 0x65, 0x6E,
		      0x81, 0x87, 0x8B, 0x98, 0x9D, 0x99, 0xA8, 0xBA, 0x5D,
		      0x5D, 0x62, 0x67, 0x6B, 0x72, 0x7F, 0x7F);
	dcs_write_seq(ctx, 0xC0, 0x1F, 0x31);
	dcs_write_seq(ctx, UD_SETPANEL, 0x03);
	dcs_write_seq(ctx, 0xD4, 0x02);
	dcs_write_seq(ctx, UD_SETREGBANK, 0x02);
	dcs_write_seq(ctx, 0xD8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		      0xFF, 0xFF, 0xFF, 0xFF);
	dcs_write_seq(ctx, UD_SETREGBANK, 0x00);
	dcs_write_seq(ctx, UD_SETREGBANK, 0x01);
	dcs_write_seq(ctx, UD_SETPOWER, 0x00);
	dcs_write_seq(ctx, UD_SETREGBANK, 0x00);
	dcs_write_seq(ctx, 0xBF, 0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01);
	dcs_write_seq(ctx, 0xC6, 0xED);
}

static void hx839x_init_sequence_1080p(struct hx839x *ctx)
{
    dcs_write_seq(ctx, UD_SETEXTC, 0xFF, 0x83, 0x99);
    dcs_write_seq(ctx, UD_SETOFFSET, 0x77);
	dcs_write_seq(ctx, UD_SETPANEL, 0x04);
    dcs_write_seq(ctx, UD_SETPOWER, 0X02, 0X04, 0X74, 0X94, 0X01, 0X32,
                       0X33, 0X11, 0X11, 0XAB, 0X4D, 0X56, 0X73, 0X02, 0X02);
    dcs_write_seq(ctx, UD_SETDISP, 0x00, 0x80, 0x80, 0xAE, 0x05, 0x07, 0X5A, 
					   0X11, 0X00, 0X00, 0X10, 0X1E, 0X70, 0X03, 0XD4);
    dcs_write_seq(ctx, UD_SETCYC, 0X00, 0XFF, 0X02, 0XC0, 0X02, 0XC0, 0X00, 0X00, 0X08, 0X00, 0X04, 0X06, 0X00,
					   0X32, 0X04, 0X0A, 0X08, 0X21, 0X03, 0X01, 0X00, 0X0F, 0XB8, 0X8B, 0X02, 0XC0, 0X02,
					   0XC0, 0X00, 0X00, 0X08, 0X00, 0X04, 0X06, 0X00, 0X32, 0X04, 0X0A, 0X08, 0X01, 0X00,
					   0X0F, 0XB8, 0X01);
    dcs_write_seq(ctx, UD_SETGIP0, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X06, 0X00, 0X00, 0X10, 0X04, 0X00, 0X04,
					   0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X00, 0X01, 0X00, 0X05, 0X05,
					   0X07, 0X00, 0X00, 0X00, 0X05, 0X40);
    dcs_write_seq(ctx, UD_SETGIP1, 0X18, 0X18, 0X19, 0X19, 0X18, 0X18, 0X21, 0X20, 0X01, 0X00, 0X07, 0X06, 0X05,
					   0X04, 0X03, 0X02, 0X18, 0X18, 0X18, 0X18, 0X18, 0X18, 0X2F, 0X2F, 0X30, 0X30, 0X31,
					   0X31, 0X18, 0X18, 0X18, 0X18);
    dcs_write_seq(ctx, UD_SETGIP2, 0X18, 0X18, 0X19, 0X19, 0X40, 0X40, 0X20, 0X21, 0X02, 0X03, 0X04, 0X05, 0X06,
					   0X07, 0X00, 0X01, 0X40, 0X40, 0X40, 0X40, 0X40, 0X40, 0X2F, 0X2F, 0X30, 0X30, 0X31,
					   0X31, 0X40, 0X40, 0X40, 0X40);
	dcs_write_seq(ctx, UD_SETGIP3, 0XA2, 0XAA, 0X02, 0XA0, 0XA2, 0XA8, 0X02, 0XA0, 0XB0, 0X00, 0X00, 0X00, 0XB0,
					   0X00, 0X00, 0X00);
	dcs_write_seq(ctx, UD_SETREGBANK, 0x01);
	dcs_write_seq(ctx, UD_SETGIP3, 0XB0, 0X00, 0X00, 0X00, 0XB0, 0X00, 0X00, 0X00, 0XE2, 0XAA, 0X03, 0XF0, 0XE2,
					   0XAA, 0X03, 0XF0);
	dcs_write_seq(ctx, UD_SETREGBANK, 0x02);
	dcs_write_seq(ctx, UD_SETGIP3, 0XE2, 0XAA, 0X03, 0XF0, 0XE2, 0XAA, 0X03, 0XF0);	
	dcs_write_seq(ctx, UD_SETREGBANK, 0x00);
    dcs_write_seq(ctx, UD_SETVCOM, 0X8D, 0X8D);
    dcs_write_seq(ctx, UD_SETGAMMA, 0X00, 0X0E, 0X19, 0X13, 0X2E, 0X39, 0X48, 0X44, 0X4D, 0X57, 0X5F, 0X66, 0X6C,
					   0X76, 0X7F, 0X85, 0X8A, 0X95, 0X9A, 0XA4, 0X9B, 0XAB, 0XB0, 0X5C, 0X58, 0X64, 0X77,
					   0X00, 0X0E, 0X19, 0X13, 0X2E, 0X39, 0X48, 0X44, 0X4D, 0X57, 0X5F, 0X66, 0X6C, 0X76,
					   0X7F, 0X85, 0X8A, 0X95, 0X9A, 0XA4, 0X9B, 0XAB, 0XB0, 0X5C, 0X58, 0X64, 0X77);
}
/* atk add end */

static int hx839x_disable(struct drm_panel *panel)
{
	struct hx839x *ctx = panel_to_hx839x(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->enabled)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret)
		dev_warn(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret)
		dev_warn(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(120);

	ctx->enabled = false;

	return 0;
}

static int hx839x_unprepare(struct drm_panel *panel)
{
	struct hx839x *ctx = panel_to_hx839x(panel);
	int ret;

	if (!ctx->prepared)
		return 0;

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
	}

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to disable supplies: %d\n", ret);
		return ret;
	}

	ctx->prepared = false;

	return 0;
}

static int hx839x_prepare(struct drm_panel *panel)
{
	struct hx839x *ctx = panel_to_hx839x(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret) {
		dev_err(ctx->dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(55);
	}

	ctx->prepared = true;

	return 0;
}

static int hx839x_enable(struct drm_panel *panel)
{
	struct hx839x *ctx = panel_to_hx839x(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;
/* atk add */
    struct device_node *np = NULL;
    int err;
    u32 tmp;
/* atk add end */

	if (ctx->enabled)
		return 0;

/* atk add +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
    np = of_find_node_by_name( NULL, "lcd_id");

    err = of_property_read_u32(np,"select_id",&tmp);
	if(err < 0) 
		return -ENODEV;
//	printk("tmp = %d\n", tmp);
	if(tmp == 2)
	{
		hx839x_init_sequence_720p(ctx);
	}
	else if(tmp == 3)
	{
		hx839x_init_sequence_1080p(ctx);
	} 
/* atk add end +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
//	hx839x_init_sequence(ctx);

	/* Set tear ON */
	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to set tear ON (%d)\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret)
		return ret;

	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	msleep(50);

	ctx->enabled = true;

	return 0;
}

static int hx839x_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct drm_display_mode *mode;
/* atk add */
    struct device_node *np = NULL;
    int err;
    u32 tmp;

    np = of_find_node_by_name( NULL, "lcd_id");

    err = of_property_read_u32(np,"select_id",&tmp);
	if(err < 0) 
		return -ENODEV;
//	printk("tmp = %d\n", tmp);

	if(tmp == 2)
	{
	mode = drm_mode_duplicate(connector->dev, &atk_imx93_mipi_dsi_5x5_720x1280_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			atk_imx93_mipi_dsi_5x5_720x1280_mode.hdisplay, atk_imx93_mipi_dsi_5x5_720x1280_mode.vdisplay,
			drm_mode_vrefresh(&atk_imx93_mipi_dsi_5x5_720x1280_mode));
		return -ENOMEM;
		}
	}

	else if(tmp == 3)
	{
	mode = drm_mode_duplicate(connector->dev, &atk_imx93_mipi_dsi_5x5_1080x1920_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			atk_imx93_mipi_dsi_5x5_1080x1920_mode.hdisplay, atk_imx93_mipi_dsi_5x5_1080x1920_mode.vdisplay,
			drm_mode_vrefresh(&atk_imx93_mipi_dsi_5x5_1080x1920_mode));
		return -ENOMEM;
		}		
	}

	else {
		return 0;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs hx839x_drm_funcs = {
	.disable = hx839x_disable,
	.unprepare = hx839x_unprepare,
	.prepare = hx839x_prepare,
	.enable = hx839x_enable,
	.get_modes = hx839x_get_modes,
};

static int hx839x_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx839x *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "failed to get reset GPIO: %d\n", ret);
		return ret;
	}

	ctx->supplies[0].supply = "vcc";
	ctx->supplies[1].supply = "iovcc";
	ret = devm_regulator_bulk_get(dev, 2, ctx->supplies);
	if (ret < 0)
		return ret;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	ret = of_property_read_u32(dev->of_node, "himax,dsi-lanes",
				   &dsi->lanes);
	if (ret) {
		dev_err(dev, "failed to get himax,dsi-lanes property: %d\n",
			ret);
		return ret;
	}

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &hx839x_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach() failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void hx839x_remove(struct mipi_dsi_device *dsi)
{
	struct hx839x *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx839x_of_match[] = {
	{ .compatible = "alientek,hx839x" },
	{ }
};
MODULE_DEVICE_TABLE(of, hx839x_of_match);

static struct mipi_dsi_driver hx839x_driver = {
	.probe = hx839x_probe,
	.remove = hx839x_remove,
	.driver = {
		.name = "panel-alientek-hx839x",
		.of_match_table = hx839x_of_match,
	},
};
module_mipi_dsi_driver(hx839x_driver);

MODULE_AUTHOR("ALIENTEK");
MODULE_DESCRIPTION("DRM Driver for ALIENTEK Himax839X MIPI DSI panel");
MODULE_LICENSE("GPL v2");
