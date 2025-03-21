// SPDX-License-Identifier: GPL-2.0
//
// simple-card-utils.c
//
// Copyright (c) 2016 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/simple_card_utils.h>

static void asoc_simple_fixup_sample_fmt(struct asoc_simple_data *data,
					 struct snd_pcm_hw_params *params)
{
	int i;
	struct snd_mask *mask = hw_param_mask(params,
					      SNDRV_PCM_HW_PARAM_FORMAT);
	struct {
		char *fmt;
		u32 val;
	} of_sample_fmt_table[] = {
		{ "s8",		SNDRV_PCM_FORMAT_S8},
		{ "s16_le",	SNDRV_PCM_FORMAT_S16_LE},
		{ "s24_le",	SNDRV_PCM_FORMAT_S24_LE},
		{ "s24_3le",	SNDRV_PCM_FORMAT_S24_3LE},
		{ "s32_le",	SNDRV_PCM_FORMAT_S32_LE},
	};

	for (i = 0; i < ARRAY_SIZE(of_sample_fmt_table); i++) {
		if (!strcmp(data->convert_sample_format,
			    of_sample_fmt_table[i].fmt)) {
			snd_mask_none(mask);
			snd_mask_set(mask, of_sample_fmt_table[i].val);
			break;
		}
	}
}

void asoc_simple_convert_fixup(struct asoc_simple_data *data,
			       struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	if (data->convert_rate)
		rate->min =
		rate->max = data->convert_rate;

	if (data->convert_channels)
		channels->min =
		channels->max = data->convert_channels;

	if (data->convert_sample_format)
		asoc_simple_fixup_sample_fmt(data, params);
}
EXPORT_SYMBOL_GPL(asoc_simple_convert_fixup);

void asoc_simple_parse_convert(struct device_node *np,
			       char *prefix,
			       struct asoc_simple_data *data)
{
	char prop[128];

	if (!prefix)
		prefix = "";

	/* sampling rate convert */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-rate");
	of_property_read_u32(np, prop, &data->convert_rate);

	/* channels transfer */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-channels");
	of_property_read_u32(np, prop, &data->convert_channels);

	/* convert sample format */
	snprintf(prop, sizeof(prop), "%s%s", prefix, "convert-sample-format");
	of_property_read_string(np, prop, &data->convert_sample_format);
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_convert);

/**
 * asoc_simple_is_convert_required() - Query if HW param conversion was requested
 * @data: Link data.
 *
 * Returns true if any HW param conversion was requested for this DAI link with
 * any "convert-xxx" properties.
 */
bool asoc_simple_is_convert_required(const struct asoc_simple_data *data)
{
	return data->convert_rate ||
	       data->convert_channels ||
	       data->convert_sample_format;
}
EXPORT_SYMBOL_GPL(asoc_simple_is_convert_required);

int asoc_simple_parse_daifmt(struct device *dev,
			     struct device_node *node,
			     struct device_node *codec,
			     char *prefix,
			     unsigned int *retfmt)
{
	struct device_node *bitclkmaster = NULL;
	struct device_node *framemaster = NULL;
	unsigned int daifmt;

	daifmt = snd_soc_daifmt_parse_format(node, prefix);

	snd_soc_daifmt_parse_clock_provider_as_phandle(node, prefix, &bitclkmaster, &framemaster);
	if (!bitclkmaster && !framemaster) {
		/*
		 * No dai-link level and master setting was not found from
		 * sound node level, revert back to legacy DT parsing and
		 * take the settings from codec node.
		 */
		dev_dbg(dev, "Revert to legacy daifmt parsing\n");

		daifmt |= snd_soc_daifmt_parse_clock_provider_as_flag(codec, NULL);
	} else {
		daifmt |= snd_soc_daifmt_clock_provider_from_bitmap(
				((codec == bitclkmaster) << 4) | (codec == framemaster));
	}

	of_node_put(bitclkmaster);
	of_node_put(framemaster);

	*retfmt = daifmt;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_daifmt);

int asoc_simple_parse_link_direction(struct device *dev, struct device_node *node, char *prefix,
				     bool *playback_only, bool *capture_only)
{
	bool is_playback_only = false;
	bool is_capture_only = false;

	if (!prefix)
		prefix = "";

	is_playback_only = of_property_read_bool(node, "playback-only");
	is_capture_only = of_property_read_bool(node, "capture-only");

	if (is_playback_only && is_capture_only) {
		dev_err(dev, "Invalid configuration, both playback-only / capture-only are set\n");
		return -EINVAL;
	}

	*playback_only = is_playback_only;
	*capture_only = is_capture_only;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_link_direction);

int asoc_simple_parse_tdm_width_map(struct device *dev, struct device_node *np,
				    struct asoc_simple_dai *dai)
{
	u32 *array_values, *p;
	int n, i, ret;

	if (!of_property_read_bool(np, "dai-tdm-slot-width-map"))
		return 0;

	n = of_property_count_elems_of_size(np, "dai-tdm-slot-width-map", sizeof(u32));
	if (n % 3) {
		dev_err(dev, "Invalid number of cells for dai-tdm-slot-width-map\n");
		return -EINVAL;
	}

	dai->tdm_width_map = devm_kcalloc(dev, n, sizeof(*dai->tdm_width_map), GFP_KERNEL);
	if (!dai->tdm_width_map)
		return -ENOMEM;

	array_values = kcalloc(n, sizeof(*array_values), GFP_KERNEL);
	if (!array_values)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "dai-tdm-slot-width-map", array_values, n);
	if (ret < 0) {
		dev_err(dev, "Could not read dai-tdm-slot-width-map: %d\n", ret);
		goto out;
	}

	p = array_values;
	for (i = 0; i < n / 3; ++i) {
		dai->tdm_width_map[i].sample_bits = *p++;
		dai->tdm_width_map[i].slot_width = *p++;
		dai->tdm_width_map[i].slot_count = *p++;
	}

	dai->n_tdm_widths = i;
	ret = 0;
out:
	kfree(array_values);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_tdm_width_map);

int asoc_simple_set_dailink_name(struct device *dev,
				 struct snd_soc_dai_link *dai_link,
				 const char *fmt, ...)
{
	va_list ap;
	char *name = NULL;
	int ret = -ENOMEM;

	va_start(ap, fmt);
	name = devm_kvasprintf(dev, GFP_KERNEL, fmt, ap);
	va_end(ap);

	if (name) {
		ret = 0;

		dai_link->name		= name;
		dai_link->stream_name	= name;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_set_dailink_name);

int asoc_simple_parse_card_name(struct snd_soc_card *card,
				char *prefix)
{
	int ret;

	if (!prefix)
		prefix = "";

	/* Parse the card name from DT */
	ret = snd_soc_of_parse_card_name(card, "label");
	if (ret < 0 || !card->name) {
		char prop[128];

		snprintf(prop, sizeof(prop), "%sname", prefix);
		ret = snd_soc_of_parse_card_name(card, prop);
		if (ret < 0)
			return ret;
	}

	if (!card->name && card->dai_link)
		card->name = card->dai_link->name;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_card_name);

static int asoc_simple_clk_enable(struct asoc_simple_dai *dai)
{
	if (dai)
		return clk_prepare_enable(dai->clk);

	return 0;
}

static void asoc_simple_clk_disable(struct asoc_simple_dai *dai)
{
	if (dai)
		clk_disable_unprepare(dai->clk);
}

int asoc_simple_parse_clk(struct device *dev,
			  struct device_node *node,
			  struct asoc_simple_dai *simple_dai,
			  struct snd_soc_dai_link_component *dlc)
{
	struct clk *clk;
	u32 val;

	/*
	 * Parse dai->sysclk come from "clocks = <&xxx>"
	 * (if system has common clock)
	 *  or "system-clock-frequency = <xxx>"
	 *  or device's module clock.
	 */
	clk = devm_get_clk_from_child(dev, node, NULL);
	simple_dai->clk_fixed = of_property_read_bool(
		node, "system-clock-fixed");
	if (!IS_ERR(clk)) {
		simple_dai->sysclk = clk_get_rate(clk);

		simple_dai->clk = clk;
	} else if (!of_property_read_u32(node, "system-clock-frequency", &val)) {
		simple_dai->sysclk = val;
		simple_dai->clk_fixed = true;
	} else {
		clk = devm_get_clk_from_child(dev, dlc->of_node, NULL);
		if (!IS_ERR(clk))
			simple_dai->sysclk = clk_get_rate(clk);
	}

	if (of_property_read_bool(node, "system-clock-direction-out"))
		simple_dai->clk_direction = SND_SOC_CLOCK_OUT;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_clk);

static int asoc_simple_check_fixed_sysclk(struct device *dev,
					  struct asoc_simple_dai *dai,
					  unsigned int *fixed_sysclk)
{
	if (dai->clk_fixed) {
		if (*fixed_sysclk && *fixed_sysclk != dai->sysclk) {
			dev_err(dev, "inconsistent fixed sysclk rates (%u vs %u)\n",
				*fixed_sysclk, dai->sysclk);
			return -EINVAL;
		}
		*fixed_sysclk = dai->sysclk;
	}

	return 0;
}

int asoc_simple_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *props = simple_priv_to_props(priv, rtd->num);
	struct asoc_simple_dai *dai;
	unsigned int fixed_sysclk = 0;
	int i1, i2, i;
	int ret;

	for_each_prop_dai_cpu(props, i1, dai) {
		ret = asoc_simple_clk_enable(dai);
		if (ret)
			goto cpu_err;
		ret = asoc_simple_check_fixed_sysclk(rtd->dev, dai, &fixed_sysclk);
		if (ret)
			goto cpu_err;
	}

	for_each_prop_dai_codec(props, i2, dai) {
		ret = asoc_simple_clk_enable(dai);
		if (ret)
			goto codec_err;
		ret = asoc_simple_check_fixed_sysclk(rtd->dev, dai, &fixed_sysclk);
		if (ret)
			goto codec_err;
	}

	if (fixed_sysclk && props->mclk_fs) {
		unsigned int fixed_rate = fixed_sysclk / props->mclk_fs;

		if (fixed_sysclk % props->mclk_fs) {
			dev_err(rtd->dev, "fixed sysclk %u not divisible by mclk_fs %u\n",
				fixed_sysclk, props->mclk_fs);
			return -EINVAL;
		}
		ret = snd_pcm_hw_constraint_minmax(substream->runtime, SNDRV_PCM_HW_PARAM_RATE,
			fixed_rate, fixed_rate);
		if (ret < 0)
			goto codec_err;
	}

	return 0;

codec_err:
	for_each_prop_dai_codec(props, i, dai) {
		if (i >= i2)
			break;
		asoc_simple_clk_disable(dai);
	}
cpu_err:
	for_each_prop_dai_cpu(props, i, dai) {
		if (i >= i1)
			break;
		asoc_simple_clk_disable(dai);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(asoc_simple_startup);

void asoc_simple_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *props = simple_priv_to_props(priv, rtd->num);
	struct asoc_simple_dai *dai;
	int i;

	for_each_prop_dai_cpu(props, i, dai) {
		struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, i);

		if (props->mclk_fs && !dai->clk_fixed && !snd_soc_dai_active(cpu_dai))
			snd_soc_dai_set_sysclk(cpu_dai,
					       0, 0, SND_SOC_CLOCK_OUT);

		asoc_simple_clk_disable(dai);
	}
	for_each_prop_dai_codec(props, i, dai) {
		struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, i);

		if (props->mclk_fs && !dai->clk_fixed && !snd_soc_dai_active(codec_dai))
			snd_soc_dai_set_sysclk(codec_dai,
					       0, 0, SND_SOC_CLOCK_IN);

		asoc_simple_clk_disable(dai);
	}
}
EXPORT_SYMBOL_GPL(asoc_simple_shutdown);

static int asoc_simple_set_clk_rate(struct device *dev,
				    struct asoc_simple_dai *simple_dai,
				    unsigned long rate)
{
	if (!simple_dai)
		return 0;

	if (simple_dai->clk_fixed && rate != simple_dai->sysclk) {
		dev_err(dev, "dai %s invalid clock rate %lu\n", simple_dai->name, rate);
		return -EINVAL;
	}

	if (!simple_dai->clk)
		return 0;

	if (clk_get_rate(simple_dai->clk) == rate)
		return 0;

	return clk_set_rate(simple_dai->clk, rate);
}

static int asoc_simple_set_tdm(struct snd_soc_dai *dai,
				struct asoc_simple_dai *simple_dai,
				struct snd_pcm_hw_params *params)
{
	int sample_bits = params_width(params);
	int slot_width, slot_count;
	int i, ret;

	if (!simple_dai || !simple_dai->tdm_width_map)
		return 0;

	slot_width = simple_dai->slot_width;
	slot_count = simple_dai->slots;

	if (slot_width == 0)
		slot_width = sample_bits;

	for (i = 0; i < simple_dai->n_tdm_widths; ++i) {
		if (simple_dai->tdm_width_map[i].sample_bits == sample_bits) {
			slot_width = simple_dai->tdm_width_map[i].slot_width;
			slot_count = simple_dai->tdm_width_map[i].slot_count;
			break;
		}
	}

	ret = snd_soc_dai_set_tdm_slot(dai,
				       simple_dai->tx_slot_mask,
				       simple_dai->rx_slot_mask,
				       slot_count,
				       slot_width);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dai->dev, "simple-card: set_tdm_slot error: %d\n", ret);
		return ret;
	}

	return 0;
}

int asoc_simple_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct asoc_simple_dai *pdai;
	struct snd_soc_dai *sdai;
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *props = simple_priv_to_props(priv, rtd->num);
	unsigned int mclk, mclk_fs = 0;
	int i, ret;

	if (props->mclk_fs)
		mclk_fs = props->mclk_fs;

	if (mclk_fs) {
		struct snd_soc_component *component;
		mclk = params_rate(params) * mclk_fs;

		for_each_prop_dai_codec(props, i, pdai) {
			ret = asoc_simple_set_clk_rate(rtd->dev, pdai, mclk);
			if (ret < 0)
				return ret;
		}

		for_each_prop_dai_cpu(props, i, pdai) {
			ret = asoc_simple_set_clk_rate(rtd->dev, pdai, mclk);
			if (ret < 0)
				return ret;
		}

		/* Ensure sysclk is set on all components in case any
		 * (such as platform components) are missed by calls to
		 * snd_soc_dai_set_sysclk.
		 */
		for_each_rtd_components(rtd, i, component) {
			ret = snd_soc_component_set_sysclk(component, 0, 0,
							   mclk, SND_SOC_CLOCK_IN);
			if (ret && ret != -ENOTSUPP)
				return ret;
		}

		for_each_rtd_codec_dais(rtd, i, sdai) {
			ret = snd_soc_dai_set_sysclk(sdai, 0, mclk, SND_SOC_CLOCK_IN);
			if (ret && ret != -ENOTSUPP)
				return ret;
		}

		for_each_rtd_cpu_dais(rtd, i, sdai) {
			ret = snd_soc_dai_set_sysclk(sdai, 0, mclk, SND_SOC_CLOCK_OUT);
			if (ret && ret != -ENOTSUPP)
				return ret;
		}
	}

	for_each_prop_dai_codec(props, i, pdai) {
		sdai = asoc_rtd_to_codec(rtd, i);
		ret = asoc_simple_set_tdm(sdai, pdai, params);
		if (ret < 0)
			return ret;
	}

	for_each_prop_dai_cpu(props, i, pdai) {
		sdai = asoc_rtd_to_cpu(rtd, i);
		ret = asoc_simple_set_tdm(sdai, pdai, params);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_hw_params);

int asoc_simple_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				   struct snd_pcm_hw_params *params)
{
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *dai_props = simple_priv_to_props(priv, rtd->num);

	asoc_simple_convert_fixup(&dai_props->adata, params);

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_be_hw_params_fixup);

static int asoc_simple_init_dai(struct snd_soc_dai *dai,
				     struct asoc_simple_dai *simple_dai)
{
	int ret;

	if (!simple_dai)
		return 0;

	if (simple_dai->sysclk) {
		ret = snd_soc_dai_set_sysclk(dai, 0, simple_dai->sysclk,
					     simple_dai->clk_direction);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "simple-card: set_sysclk error\n");
			return ret;
		}
	}

	if (simple_dai->slots) {
		ret = snd_soc_dai_set_tdm_slot(dai,
					       simple_dai->tx_slot_mask,
					       simple_dai->rx_slot_mask,
					       simple_dai->slots,
					       simple_dai->slot_width);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dai->dev, "simple-card: set_tdm_slot error\n");
			return ret;
		}
	}

	return 0;
}

static inline int asoc_simple_component_is_codec(struct snd_soc_component *component)
{
	return component->driver->endianness;
}

static int asoc_simple_init_for_codec2codec(struct snd_soc_pcm_runtime *rtd,
					    struct simple_dai_props *dai_props)
{
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_component *component;
	struct snd_soc_pcm_stream *params;
	struct snd_pcm_hardware hw;
	int i, ret, stream;

	/* Do nothing if it already has Codec2Codec settings */
	if (dai_link->params)
		return 0;

	/* Do nothing if it was DPCM :: BE */
	if (dai_link->no_pcm)
		return 0;

	/* Only Codecs */
	for_each_rtd_components(rtd, i, component) {
		if (!asoc_simple_component_is_codec(component))
			return 0;
	}

	/* Assumes the capabilities are the same for all supported streams */
	for_each_pcm_streams(stream) {
		ret = snd_soc_runtime_calc_hw(rtd, &hw, stream);
		if (ret == 0)
			break;
	}

	if (ret < 0) {
		dev_err(rtd->dev, "simple-card: no valid dai_link params\n");
		return ret;
	}

	params = devm_kzalloc(rtd->dev, sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->formats = hw.formats;
	params->rates = hw.rates;
	params->rate_min = hw.rate_min;
	params->rate_max = hw.rate_max;
	params->channels_min = hw.channels_min;
	params->channels_max = hw.channels_max;

	dai_link->params = params;
	dai_link->num_params = 1;

	return 0;
}

int asoc_simple_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(rtd->card);
	struct simple_dai_props *props = simple_priv_to_props(priv, rtd->num);
	struct asoc_simple_dai *dai;
	int i, ret;

	for_each_prop_dai_codec(props, i, dai) {
		ret = asoc_simple_init_dai(asoc_rtd_to_codec(rtd, i), dai);
		if (ret < 0)
			return ret;
	}
	for_each_prop_dai_cpu(props, i, dai) {
		ret = asoc_simple_init_dai(asoc_rtd_to_cpu(rtd, i), dai);
		if (ret < 0)
			return ret;
	}

	ret = asoc_simple_init_for_codec2codec(rtd, props);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_dai_init);

void asoc_simple_canonicalize_platform(struct snd_soc_dai_link_component *platforms,
				       struct snd_soc_dai_link_component *cpus)
{
	/* Assumes platform == cpu */
	if (!platforms->of_node)
		platforms->of_node = cpus->of_node;
}
EXPORT_SYMBOL_GPL(asoc_simple_canonicalize_platform);

void asoc_simple_canonicalize_cpu(struct snd_soc_dai_link_component *cpus,
				  int is_single_links)
{
	/*
	 * In soc_bind_dai_link() will check cpu name after
	 * of_node matching if dai_link has cpu_dai_name.
	 * but, it will never match if name was created by
	 * fmt_single_name() remove cpu_dai_name if cpu_args
	 * was 0. See:
	 *	fmt_single_name()
	 *	fmt_multiple_name()
	 */
	if (is_single_links)
		cpus->dai_name = NULL;
}
EXPORT_SYMBOL_GPL(asoc_simple_canonicalize_cpu);

void asoc_simple_clean_reference(struct snd_soc_card *card)
{
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_dai_link_component *cpu;
	struct snd_soc_dai_link_component *codec;
	int i, j;

	for_each_card_prelinks(card, i, dai_link) {
		for_each_link_cpus(dai_link, j, cpu)
			of_node_put(cpu->of_node);
		for_each_link_codecs(dai_link, j, codec)
			of_node_put(codec->of_node);
	}
}
EXPORT_SYMBOL_GPL(asoc_simple_clean_reference);

int asoc_simple_parse_routing(struct snd_soc_card *card,
			      char *prefix)
{
	struct device_node *node = card->dev->of_node;
	char prop[128];

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "routing");

	if (!of_property_read_bool(node, prop))
		return 0;

	return snd_soc_of_parse_audio_routing(card, prop);
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_routing);

int asoc_simple_parse_widgets(struct snd_soc_card *card,
			      char *prefix)
{
	struct device_node *node = card->dev->of_node;
	char prop[128];

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "widgets");

	if (of_property_read_bool(node, prop))
		return snd_soc_of_parse_audio_simple_widgets(card, prop);

	/* no widgets is not error */
	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_widgets);

int asoc_simple_parse_pin_switches(struct snd_soc_card *card,
				   char *prefix)
{
	char prop[128];

	if (!prefix)
		prefix = "";

	snprintf(prop, sizeof(prop), "%s%s", prefix, "pin-switches");

	return snd_soc_of_parse_pin_switches(card, prop);
}
EXPORT_SYMBOL_GPL(asoc_simple_parse_pin_switches);

int asoc_simple_init_jack(struct snd_soc_card *card,
			  struct asoc_simple_jack *sjack,
			  int is_hp, char *prefix,
			  char *pin)
{
	struct device *dev = card->dev;
	struct gpio_desc *desc;
	char prop[128];
	char *pin_name;
	char *gpio_name;
	int mask;
	int error;

	if (!prefix)
		prefix = "";

	sjack->gpio.gpio = -ENOENT;

    if (is_hp == 1) {
        snprintf(prop, sizeof(prop), "%shp-det", prefix);
        pin_name    = pin ? pin : "Headphones";
        gpio_name    = "Headphone detection";
        mask        = SND_JACK_HEADPHONE;
    } else if (is_hp == 0){
        snprintf(prop, sizeof(prop), "%smic-det", prefix);
        pin_name    = pin ? pin : "Mic Jack";
        gpio_name    = "Mic detection";
        mask        = SND_JACK_MICROPHONE;
    } else {
        snprintf(prop, sizeof(prop), "%sspk-det", prefix);
        pin_name    = pin ? pin : "Spk Jack";
        gpio_name    = "Speaker detection";
        mask        = SND_JACK_SPEAKER;
    }

	// if (is_hp) {
	// 	snprintf(prop, sizeof(prop), "%shp-det", prefix);
	// 	pin_name	= pin ? pin : "Headphones";
	// 	gpio_name	= "Headphone detection";
	// 	mask		= SND_JACK_HEADPHONE;
	// } else {
	// 	snprintf(prop, sizeof(prop), "%smic-det", prefix);
	// 	pin_name	= pin ? pin : "Mic Jack";
	// 	gpio_name	= "Mic detection";
	// 	mask		= SND_JACK_MICROPHONE;
	// }

    if (is_hp != 2) {
        desc = gpiod_get_optional(dev, prop, GPIOD_IN);
    } else {
        desc = gpiod_get_optional(dev, prop, GPIOD_OUT_HIGH);
    }

	// desc = gpiod_get_optional(dev, prop, GPIOD_IN);

	error = PTR_ERR_OR_ZERO(desc);
	if (error)
		return error;

	if (desc) {
		error = gpiod_set_consumer_name(desc, gpio_name);
		if (error)
			return error;

		sjack->pin.pin		= pin_name;
		sjack->pin.mask		= mask;

		sjack->gpio.name	= gpio_name;
		sjack->gpio.report	= mask;
		sjack->gpio.desc	= desc;
		sjack->gpio.debounce_time = 150;

		snd_soc_card_jack_new_pins(card, pin_name, mask, &sjack->jack,
					   &sjack->pin, 1);

		if (is_hp != 2) {
			snd_soc_jack_add_gpios(&sjack->jack, 1, &sjack->gpio);
		}	
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_init_jack);

int asoc_simple_init_priv(struct asoc_simple_priv *priv,
			  struct link_info *li)
{
	struct snd_soc_card *card = simple_priv_to_card(priv);
	struct device *dev = simple_priv_to_dev(priv);
	struct snd_soc_dai_link *dai_link;
	struct simple_dai_props *dai_props;
	struct asoc_simple_dai *dais;
	struct snd_soc_dai_link_component *dlcs;
	struct snd_soc_codec_conf *cconf = NULL;
	int i, dai_num = 0, dlc_num = 0, cnf_num = 0;

	dai_props = devm_kcalloc(dev, li->link, sizeof(*dai_props), GFP_KERNEL);
	dai_link  = devm_kcalloc(dev, li->link, sizeof(*dai_link),  GFP_KERNEL);
	if (!dai_props || !dai_link)
		return -ENOMEM;

	/*
	 * dais (= CPU+Codec)
	 * dlcs (= CPU+Codec+Platform)
	 */
	for (i = 0; i < li->link; i++) {
		int cc = li->num[i].cpus + li->num[i].codecs;

		dai_num += cc;
		dlc_num += cc + li->num[i].platforms;

		if (!li->num[i].cpus)
			cnf_num += li->num[i].codecs;
	}

	dais = devm_kcalloc(dev, dai_num, sizeof(*dais), GFP_KERNEL);
	dlcs = devm_kcalloc(dev, dlc_num, sizeof(*dlcs), GFP_KERNEL);
	if (!dais || !dlcs)
		return -ENOMEM;

	if (cnf_num) {
		cconf = devm_kcalloc(dev, cnf_num, sizeof(*cconf), GFP_KERNEL);
		if (!cconf)
			return -ENOMEM;
	}

	dev_dbg(dev, "link %d, dais %d, ccnf %d\n",
		li->link, dai_num, cnf_num);

	/* dummy CPU/Codec */
	priv->dummy.of_node	= NULL;
	priv->dummy.dai_name	= "snd-soc-dummy-dai";
	priv->dummy.name	= "snd-soc-dummy";

	priv->dai_props		= dai_props;
	priv->dai_link		= dai_link;
	priv->dais		= dais;
	priv->dlcs		= dlcs;
	priv->codec_conf	= cconf;

	card->dai_link		= priv->dai_link;
	card->num_links		= li->link;
	card->codec_conf	= cconf;
	card->num_configs	= cnf_num;

	for (i = 0; i < li->link; i++) {
		if (li->num[i].cpus) {
			/* Normal CPU */
			dai_props[i].cpus	=
			dai_link[i].cpus	= dlcs;
			dai_props[i].num.cpus	=
			dai_link[i].num_cpus	= li->num[i].cpus;
			dai_props[i].cpu_dai	= dais;

			dlcs += li->num[i].cpus;
			dais += li->num[i].cpus;
		} else {
			/* DPCM Be's CPU = dummy */
			dai_props[i].cpus	=
			dai_link[i].cpus	= &priv->dummy;
			dai_props[i].num.cpus	=
			dai_link[i].num_cpus	= 1;
		}

		if (li->num[i].codecs) {
			/* Normal Codec */
			dai_props[i].codecs	=
			dai_link[i].codecs	= dlcs;
			dai_props[i].num.codecs	=
			dai_link[i].num_codecs	= li->num[i].codecs;
			dai_props[i].codec_dai	= dais;

			dlcs += li->num[i].codecs;
			dais += li->num[i].codecs;

			if (!li->num[i].cpus) {
				/* DPCM Be's Codec */
				dai_props[i].codec_conf = cconf;
				cconf += li->num[i].codecs;
			}
		} else {
			/* DPCM Fe's Codec = dummy */
			dai_props[i].codecs	=
			dai_link[i].codecs	= &priv->dummy;
			dai_props[i].num.codecs	=
			dai_link[i].num_codecs	= 1;
		}

		if (li->num[i].platforms) {
			/* Have Platform */
			dai_props[i].platforms		=
			dai_link[i].platforms		= dlcs;
			dai_props[i].num.platforms	=
			dai_link[i].num_platforms	= li->num[i].platforms;

			dlcs += li->num[i].platforms;
		} else {
			/* Doesn't have Platform */
			dai_props[i].platforms		=
			dai_link[i].platforms		= NULL;
			dai_props[i].num.platforms	=
			dai_link[i].num_platforms	= 0;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_init_priv);

int asoc_simple_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	asoc_simple_clean_reference(card);

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_simple_remove);

int asoc_graph_card_probe(struct snd_soc_card *card)
{
	struct asoc_simple_priv *priv = snd_soc_card_get_drvdata(card);
	int ret;

	ret = asoc_simple_init_hp(card, &priv->hp_jack, NULL);
	if (ret < 0)
		return ret;

	ret = asoc_simple_init_mic(card, &priv->mic_jack, NULL);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(asoc_graph_card_probe);

int asoc_graph_is_ports0(struct device_node *np)
{
	struct device_node *port, *ports, *ports0, *top;
	int ret;

	/* np is "endpoint" or "port" */
	if (of_node_name_eq(np, "endpoint")) {
		port = of_get_parent(np);
	} else {
		port = np;
		of_node_get(port);
	}

	ports	= of_get_parent(port);
	top	= of_get_parent(ports);
	ports0	= of_get_child_by_name(top, "ports");

	ret = ports0 == ports;

	of_node_put(port);
	of_node_put(ports);
	of_node_put(ports0);
	of_node_put(top);

	return ret;
}
EXPORT_SYMBOL_GPL(asoc_graph_is_ports0);

/* Module information */
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_DESCRIPTION("ALSA SoC Simple Card Utils");
MODULE_LICENSE("GPL v2");
