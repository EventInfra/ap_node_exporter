#include <net/if.h>
#include <linux/nl80211.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#define ROOTPAGE  "<html><head><title>Metrics exporter</title></head><body><ul><li><a href=\"/metrics\">metrics</a></li></ul></body></html>"
#define NOT_FOUND_ERROR "<html><head><title>Document not found</title></head><body><h1>404 - Document not found</h1></body></html>"
#define UNUSED(x) (void)(x)

#define BIT(x) (1ULL<<(x))

struct bitrate {
	uint32_t rate;
	uint8_t vht_nss;
	uint8_t channel_width;
	uint8_t mcs;
	uint8_t vht_mcs;
	bool short_gi;
};


struct client_context {
	FILE *stream;
	int nl80211_id;
	struct nl_sock *nls;
	int if_count;
	int if_index[16];
};

static int finish_handler(struct nl_msg *msg, void *arg)
{
	UNUSED(msg);
	int *err = arg;
	*err = 0;
	return NL_SKIP;
}

static int survey_dump_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_SURVEY_INFO_MAX + 1];
	struct client_context *ctx = (struct client_context *)arg;
	FILE *stream = ctx->stream;

	static struct nla_policy survey_policy[NL80211_SURVEY_INFO_MAX + 1] = {
		[NL80211_SURVEY_INFO_FREQUENCY] = { .type = NLA_U32 },
		[NL80211_SURVEY_INFO_NOISE] = { .type = NLA_U8 },
	};

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	char dev[IFNAMSIZ];
	if_indextoname(nla_get_u32(tb[NL80211_ATTR_IFINDEX]), dev);

	if (!tb[NL80211_ATTR_SURVEY_INFO]) {
		fprintf(stderr, "survey data missing!\n");
		return NL_SKIP;
	}

	if (nla_parse_nested(sinfo, NL80211_SURVEY_INFO_MAX,
			     tb[NL80211_ATTR_SURVEY_INFO],
			     survey_policy)) {
		fprintf(stderr, "failed to parse nested attributes!\n");
		return NL_SKIP;
	}
	bool active_freq = 0;
	uint32_t cur_freq = 0;
	if (sinfo[NL80211_SURVEY_INFO_FREQUENCY]) {
		if (sinfo[NL80211_SURVEY_INFO_IN_USE]) {
			active_freq = 1;
		}
		cur_freq = nla_get_u32(sinfo[NL80211_SURVEY_INFO_FREQUENCY]);
	} else {
		return NL_SKIP;
	}
	if (sinfo[NL80211_SURVEY_INFO_NOISE]) {
		fprintf(stream, "wlan_survey_channel_noise_dbm{device=\"%s\",frequency=%d} %d\n",
				dev, cur_freq, (int8_t)nla_get_u8(sinfo[NL80211_SURVEY_INFO_NOISE]));
	}
	if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]) {
		fprintf(stream, "wlan_survey_channel_active_ms{device=\"%s\",frequency=%d} %ju\n",
				dev, cur_freq, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]));
	}
	if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]) {
		fprintf(stream, "wlan_survey_channel_busy_ms{device=\"%s\",frequency=%d} %ju\n",
				dev, cur_freq, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]));
	}
	if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY]) {
		fprintf(stream, "wlan_survey_channel_ext_busy_ms{device=\"%s\",frequency=%d} %ju\n",
				dev, cur_freq, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY]));
	}
	if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]) {
		fprintf(stream,"wlan_survey_channel_rx_time_ms{device=\"%s\",frequency=%d} %ju\n",
				dev, cur_freq, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]));
	}
	if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]) {
		fprintf(stream, "wlan_survey_channel_tx_time_ms{device=\"%s\",frequency=%d} %ju\n",
				dev, cur_freq, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]));
	}
	if (active_freq) {
		fprintf(stream, "wlan_active_frequency{device=\"%s\"} %ju\n",
				dev, (uintmax_t)cur_freq);
		if (sinfo[NL80211_SURVEY_INFO_NOISE]) {
			fprintf(stream, "wlan_active_channel_noise_dbm{device=\"%s\"} %d\n",
					dev, (int8_t)nla_get_u8(sinfo[NL80211_SURVEY_INFO_NOISE]));
		}
		if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]) {
			fprintf(stream, "wlan_active_channel_active_ms{device=\"%s\"} %ju\n",
					dev, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME]));
		}
		if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]) {
			fprintf(stream, "wlan_active_channel_busy_ms{device=\"%s\"} %ju\n",
					dev, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_BUSY]));
		}
		if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY]) {
			fprintf(stream, "wlan_active_channel_ext_busy_ms{device=\"%s\"} %ju\n",
					dev, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_EXT_BUSY]));
		}
		if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]) {
			fprintf(stream,"wlan_active_channel_rx_time_ms{device=\"%s\"} %ju\n",
					dev, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_RX]));
		}
		if (sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]) {
			fprintf(stream, "wlan_active_channel_tx_time_ms{device=\"%s\"} %ju\n",
					dev, (uintmax_t)nla_get_u64(sinfo[NL80211_SURVEY_INFO_CHANNEL_TIME_TX]));
		}
	}
	return NL_SKIP;
}

static void print_bss_param(struct nlattr *bss_param_attr, FILE *stream, char *dev, char *sta)
{
	struct nlattr *info[NL80211_STA_BSS_PARAM_MAX + 1];
	static struct nla_policy bss_policy[NL80211_STA_BSS_PARAM_MAX + 1] = {
		[NL80211_STA_BSS_PARAM_CTS_PROT] = { .type = NLA_FLAG },
		[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE] = { .type = NLA_FLAG },
		[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME] = { .type = NLA_FLAG },
		[NL80211_STA_BSS_PARAM_DTIM_PERIOD] = { .type = NLA_U8 },
		[NL80211_STA_BSS_PARAM_BEACON_INTERVAL] = { .type = NLA_U16 },
	};

	if (nla_parse_nested(info, NL80211_STA_BSS_PARAM_MAX,
			     bss_param_attr, bss_policy)) {
		printf("failed to parse nested bss param attributes!");
	}

	if (info[NL80211_STA_BSS_PARAM_DTIM_PERIOD]) {
		fprintf(stream, "wlan_station_bss_dtim_period{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, nla_get_u8(info[NL80211_STA_BSS_PARAM_DTIM_PERIOD]));
	}
	if (info[NL80211_STA_BSS_PARAM_BEACON_INTERVAL]) {
		fprintf(stream, "wlan_station_bss_beacon_interval{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, nla_get_u16(info[NL80211_STA_BSS_PARAM_BEACON_INTERVAL]));
	}
	if (info[NL80211_STA_BSS_PARAM_CTS_PROT]) {
		fprintf(stream, "wlan_station_bss_cts_protection{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, nla_get_u16(info[NL80211_STA_BSS_PARAM_CTS_PROT]));
	}
	if (info[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE]) {
		fprintf(stream, "wlan_station_bss_short_preamble{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, nla_get_u16(info[NL80211_STA_BSS_PARAM_SHORT_PREAMBLE]));
	}
	if (info[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME]) {
		fprintf(stream, "wlan_station_bss_short_slot_time{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, nla_get_u16(info[NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME]));
	}
}

static void print_tid_stats(struct nlattr *tid_stats_attr, FILE *stream, char *dev, char *sta)
{
	struct nlattr *stats_info[NL80211_TID_STATS_MAX + 1], *tidattr, *info;
	static struct nla_policy stats_policy[NL80211_TID_STATS_MAX + 1] = {
		[NL80211_TID_STATS_RX_MSDU] = { .type = NLA_U64 },
		[NL80211_TID_STATS_TX_MSDU] = { .type = NLA_U64 },
		[NL80211_TID_STATS_TX_MSDU_RETRIES] = { .type = NLA_U64 },
		[NL80211_TID_STATS_TX_MSDU_FAILED] = { .type = NLA_U64 },
	};
	int rem, i = 0;

	nla_for_each_nested(tidattr, tid_stats_attr, rem) {
		if (nla_parse_nested(stats_info, NL80211_TID_STATS_MAX,
				     tidattr, stats_policy)) {
			fprintf(stderr, "failed to parse nested stats attributes!");
			return;
		}
		if (stats_info[NL80211_TID_STATS_RX_MSDU]) {
			fprintf(stream, "wlan_station_tid_rx_msdu{device=\"%s\",station=\"%s\",tid=%d} %ju\n",
					dev, sta, i, (uintmax_t)nla_get_u64(stats_info[NL80211_TID_STATS_RX_MSDU]));
		}
		if (stats_info[NL80211_TID_STATS_TX_MSDU]) {
			fprintf(stream, "wlan_station_tid_tx_msdu{device=\"%s\",station=\"%s\",tid=%d} %ju\n",
					dev, sta, i, (uintmax_t)nla_get_u64(stats_info[NL80211_TID_STATS_TX_MSDU]));
		}
		if (stats_info[NL80211_TID_STATS_TX_MSDU_RETRIES]) {
			fprintf(stream, "wlan_station_tid_tx_msdu_retries{device=\"%s\",station=\"%s\",tid=%d} %ju\n",
					dev, sta, i, (uintmax_t)nla_get_u64(stats_info[NL80211_TID_STATS_TX_MSDU_RETRIES]));
		}
		if (stats_info[NL80211_TID_STATS_TX_MSDU_FAILED]) {
			fprintf(stream, "wlan_station_tid_tx_msdu_failed{device=\"%s\",station=\"%s\",tid=%d} %ju\n",
					dev, sta, i, (uintmax_t)nla_get_u64(stats_info[NL80211_TID_STATS_TX_MSDU_FAILED]));
		}
		i++;
	}
}

static void print_bitrate(struct nlattr *bitrate_attr, const char *direction, FILE *stream, char *dev, char *sta)
{
	int rate = 0;
	struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
	static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
		[NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
		[NL80211_RATE_INFO_BITRATE32] = { .type = NLA_U32 },
		[NL80211_RATE_INFO_MCS] = { .type = NLA_U8 },
		[NL80211_RATE_INFO_VHT_MCS] = { .type = NLA_U8 },
		[NL80211_RATE_INFO_40_MHZ_WIDTH] = { .type = NLA_FLAG },
		[NL80211_RATE_INFO_SHORT_GI] = { .type = NLA_FLAG },
	};

	if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
			     bitrate_attr, rate_policy)) {
		fprintf(stderr, "failed to parse nested rate attributes!");
		return;
	}

	if (rinfo[NL80211_RATE_INFO_BITRATE32]) {
		fprintf(stream, "wlan_station_%s_bitrate{device=\"%s\",station=\"%s\"} %ju\n",
				direction, dev, sta, (uintmax_t)nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]) * 100000);
	} else if (rinfo[NL80211_RATE_INFO_BITRATE]) {
		fprintf(stream, "wlan_station_%s_bitrate{device=\"%s\",station=\"%s\"} %ju\n",
				direction, dev, sta, (uintmax_t)nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]) * 100000);
	}
	if (rinfo[NL80211_RATE_INFO_MCS]) {
		fprintf(stream, "wlan_station_%s_bitrate_mcs{device=\"%s\",station=\"%s\"} %ju\n",
				direction, dev, sta, (uintmax_t)nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]));
	}
	if (rinfo[NL80211_RATE_INFO_VHT_MCS]) {
		fprintf(stream, "wlan_station_%s_bitrate_vht_mcs{device=\"%s\",station=\"%s\"} %ju\n",
				direction, dev, sta, (uintmax_t)nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_MCS]));
	}
	uint8_t channel_width;
	if (rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH]) {
		channel_width = 160;
	} else if (rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH]) {
		channel_width = 160; /* FIXME: Need way of telling controller that this is 80+80, not 160
		                             But then again.. nobody should be using 160MHz wide channels anyway */
	} else if (rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH]) {
		channel_width = 80;
	} else if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH]) {
		channel_width = 40;
	} else {
		channel_width = 20;
	}
	fprintf(stream, "wlan_station_%s_bitrate_channel_width{device=\"%s\",station=\"%s\"} %u\n",
			direction, dev, sta, channel_width);

	fprintf(stream, "wlan_station_%s_bitrate_short_gi{device=\"%s\",station=\"%s\"} %u\n",
			direction, dev, sta, rinfo[NL80211_RATE_INFO_SHORT_GI] ? 1 : 0);

	if (rinfo[NL80211_RATE_INFO_VHT_NSS]) {
		fprintf(stream, "wlan_station_%s_bitrate_vht_nss{device=\"%s\",station=\"%s\"} %u\n",
				direction, dev, sta, nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_NSS]));
	}
}


static void print_chain_signal(struct nlattr *attr_list, char *metric_name, FILE *stream, char *dev, char *sta)
{
	struct nlattr *attr;
	static char buf[64];
	char *cur = buf;
	int i = 0, rem;
	const char *prefix;

	if (!attr_list)
		return;

	nla_for_each_nested(attr, attr_list, rem) {
		fprintf(stream, "%s{device=\"%s\",station=\"%s\",chain=%d} %d\n",
			metric_name, dev, sta, i, (int8_t) nla_get_u8(attr));
		i++;
	}
}

static int station_dump_handler(struct nl_msg *msg, void *arg)
{
	struct client_context *ctx = (struct client_context *)arg;
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
		[NL80211_STA_INFO_CONNECTED_TIME] = { .type = NLA_U32 },
		[NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_RX_BYTES64] = { .type = NLA_U64 },
		[NL80211_STA_INFO_TX_BYTES64] = { .type = NLA_U64 },
		[NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
		[NL80211_STA_INFO_BEACON_RX] = { .type = NLA_U64},
		[NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
		[NL80211_STA_INFO_T_OFFSET] = { .type = NLA_U64 },
		[NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_RX_BITRATE] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
		[NL80211_STA_INFO_BEACON_LOSS] = { .type = NLA_U32},
		[NL80211_STA_INFO_RX_DROP_MISC] = { .type = NLA_U64},
		[NL80211_STA_INFO_STA_FLAGS] =
	{ .minlen = sizeof(struct nl80211_sta_flag_update) },
		[NL80211_STA_INFO_CHAIN_SIGNAL] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_CHAIN_SIGNAL_AVG] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_TID_STATS] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_BSS_PARAM] = { .type = NLA_NESTED },
		[NL80211_STA_INFO_RX_DURATION] = { .type = NLA_U64 },
	};
	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb_msg[NL80211_ATTR_STA_INFO]) {
		fprintf(stderr, "sta stats missing!\n");
		return NL_SKIP;
	}
	if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
				tb_msg[NL80211_ATTR_STA_INFO],
				stats_policy)) {
		fprintf(stderr, "failed to parse nested attributes!\n");
		return NL_SKIP;
	}
	char dev[IFNAMSIZ];
	if_indextoname(nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]), dev);
	char sta[ETH_ALEN*3];
	snprintf(sta, ETH_ALEN*3, "%02x:%02x:%02x:%02x:%02x:%02x",
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[0],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[1],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[2],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[3],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[4],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[5]);
	FILE *stream = ctx->stream;

	if (sinfo[NL80211_STA_INFO_CONNECTED_TIME]) {
		fprintf(stream, "wlan_station_connected_time_s{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_CONNECTED_TIME]));
	}
	if (sinfo[NL80211_STA_INFO_INACTIVE_TIME]) {
		fprintf(stream, "wlan_station_inactive_time_ms{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]));
	}
	if (sinfo[NL80211_STA_INFO_RX_BYTES64]) {
		fprintf(stream, "wlan_station_rx_bytes{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u64(sinfo[NL80211_STA_INFO_RX_BYTES64]));
	}
	else if (sinfo[NL80211_STA_INFO_RX_BYTES]) {
		fprintf(stream, "wlan_station_rx_bytes{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]));
	}
	if (sinfo[NL80211_STA_INFO_RX_PACKETS]) {
		fprintf(stream, "wlan_station_rx_packets{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]));
	}
	if (sinfo[NL80211_STA_INFO_TX_BYTES64]) {
		fprintf(stream, "wlan_station_tx_bytes{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u64(sinfo[NL80211_STA_INFO_TX_BYTES64]));
	} else if (sinfo[NL80211_STA_INFO_TX_BYTES]) {
		fprintf(stream, "wlan_station_tx_bytes{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]));
	}
	if (sinfo[NL80211_STA_INFO_TX_PACKETS]) {
		fprintf(stream, "wlan_station_tx_packets{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]));
	}
	if (sinfo[NL80211_STA_INFO_TX_RETRIES]) {
		fprintf(stream, "wlan_station_tx_retries{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]));
	}
	if (sinfo[NL80211_STA_INFO_TX_FAILED]) {
		fprintf(stream, "wlan_station_tx_failed{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]));
	}
	if (sinfo[NL80211_STA_INFO_BEACON_LOSS]) {
		fprintf(stream, "wlan_station_beacon_loss{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_BEACON_LOSS]));
	}
	if (sinfo[NL80211_STA_INFO_BEACON_RX]) {
		fprintf(stream, "wlan_station_rx_beacons{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_BEACON_RX]));
	}
	if (sinfo[NL80211_STA_INFO_RX_DROP_MISC]) {
		fprintf(stream, "wlan_station_rx_drop_misc{device=\"%s\",station=\"%s\"} %ju\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_RX_DROP_MISC]));
	}

	print_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL], "wlan_station_chain_signal_dbm", stream, dev, sta);
	if (sinfo[NL80211_STA_INFO_SIGNAL]) {
		fprintf(stream, "wlan_station_signal_dbm{device=\"%s\",station=\"%s\"} %d\n",
				dev, sta, (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]));
	}
	print_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG], "wlan_station_chain_signal_avg_dbm", stream, dev, sta);
	if (sinfo[NL80211_STA_INFO_SIGNAL_AVG]) {
		fprintf(stream, "wlan_station_signal_avg_dbm{device=\"%s\",station=\"%s\"} %d\n",
				dev, sta, (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]));
	}

	if (sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]) {
		fprintf(stream, "wlan_station_beacon_signal_avg_dbm{device=\"%s\",station=\"%s\"} %d\n",
				dev, sta, (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]));
	}
	if (sinfo[NL80211_STA_INFO_T_OFFSET]) {
		fprintf(stream, "wlan_station_time_offset_ms{device=\"%s\",station=\"%s\"} %jd\n",
				dev, sta, (intmax_t)nla_get_u64(sinfo[NL80211_STA_INFO_T_OFFSET]));
	}

	if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
		print_bitrate(sinfo[NL80211_STA_INFO_TX_BITRATE], "tx", stream, dev, sta);
	}

	if (sinfo[NL80211_STA_INFO_RX_BITRATE]) {
		print_bitrate(sinfo[NL80211_STA_INFO_RX_BITRATE], "rx", stream, dev, sta);
	}

	if (sinfo[NL80211_STA_INFO_RX_DURATION]) {
		fprintf(stream, "wlan_station_rx_duration{device=\"%s\",station=\"%s\"} %jd\n",
				dev, sta, (uintmax_t)nla_get_u64(sinfo[NL80211_STA_INFO_RX_DURATION]));
	}

	if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) {
		fprintf(stream, "wlan_station_expected_throughput{device=\"%s\",station=\"%s\"} %jd\n",
				dev, sta, (uintmax_t)nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) * 1000);
	}

	if (sinfo[NL80211_STA_INFO_STA_FLAGS]) {
		struct nl80211_sta_flag_update *sta_flags = (struct nl80211_sta_flag_update *)
			    nla_data(sinfo[NL80211_STA_INFO_STA_FLAGS]);

		fprintf(stream, "wlan_station_authorized{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_AUTHORIZED)));
		fprintf(stream, "wlan_station_authenticated{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_AUTHENTICATED)));
		fprintf(stream, "wlan_station_associated{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_ASSOCIATED)));
		fprintf(stream, "wlan_station_short_preamble{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE)));
		fprintf(stream, "wlan_station_wme{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_WME)));
		fprintf(stream, "wlan_station_mfp{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_MFP)));
		fprintf(stream, "wlan_station_tdls_peer{device=\"%s\",station=\"%s\"} %u\n",
				dev, sta, !!(sta_flags->set & BIT(NL80211_STA_FLAG_TDLS_PEER)));
	}

	if (sinfo[NL80211_STA_INFO_TID_STATS]) {
		print_tid_stats(sinfo[NL80211_STA_INFO_TID_STATS], stream, dev, sta);
	}
	if (sinfo[NL80211_STA_INFO_BSS_PARAM]) {
		print_bss_param(sinfo[NL80211_STA_INFO_BSS_PARAM], stream, dev, sta);
	}
	return NL_SKIP;
}


static int list_interface_handler(struct nl_msg *in_msg, void *arg)
{
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(in_msg));
	struct client_context *ctx = (struct client_context *)arg;
	uint32_t if_index;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_IFINDEX]) {
		ctx->if_index[ctx->if_count] = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
		ctx->if_count++;
	}

	return NL_SKIP;
}

int show_metrics(FILE *stream) {
	/* Set up the netlink socket */
	struct client_context ctx = {0};
	ctx.stream = stream;
	ctx.nls = nl_socket_alloc();
	if (!ctx.nls) {
		return -ENOLINK;
	}
	nl_socket_set_buffer_size(ctx.nls, 16384, 16384);
	if (genl_connect(ctx.nls)) {
		fprintf(stderr, "Failed to connect to generic netlink.\n");
		nl_socket_free(ctx.nls);
		return -ENOLINK;
	}
	ctx.nl80211_id = genl_ctrl_resolve(ctx.nls, "nl80211");
	if (ctx.nl80211_id < 0) {
		fprintf(stderr, "nl80211 not found.\n");
		nl_socket_free(ctx.nls);
		return -ENOENT;
	}

	struct nl_msg *msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Failed to allocate netlink message.\n");
		nl_socket_free(ctx.nls);
		return -ENOMEM;
	}
	struct nl_cb *cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callback.\n");
		nlmsg_free(msg);
		nl_socket_free(ctx.nls);
		return -ENOMEM;
	}

	/* Send a GET_INTERFACE command */
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, list_interface_handler, &ctx);
	genlmsg_put(msg, 0, 0, ctx.nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
	nl_send_auto_complete(ctx.nls, msg);

	/* Wait for shit to finish */
	int err = 1;
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	while (err > 0)
		nl_recvmsgs(ctx.nls, cb);

	/* Free the shite */
	nlmsg_free(msg);
	nl_cb_put(cb);

	for (int i = 0; i < ctx.if_count; i++) {
		/* Allocate new message */
		msg = nlmsg_alloc();
		if (!msg) {
			fprintf(stderr, "Failed to allocate netlink message.\n");
			nl_socket_free(ctx.nls);
			return -ENOMEM;
		}
		cb = nl_cb_alloc(NL_CB_CUSTOM);
		if (!cb) {
			fprintf(stderr, "Failed to allocate netlink callback.\n");
			nlmsg_free(msg);
			nl_socket_free(ctx.nls);
			return -ENOMEM;
		}

		/* Send a GET_STATION command */
		nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, station_dump_handler, &ctx);
		genlmsg_put(msg, 0, 0, ctx.nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
		nla_put_u32(msg, NL80211_ATTR_IFINDEX, ctx.if_index[i]);
		nl_send_auto_complete(ctx.nls, msg);


		/* Wait for shit to finish */
		err = 1;
		nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
		while (err > 0)
			nl_recvmsgs(ctx.nls, cb);

		/* Free the shite */
		nlmsg_free(msg);
		nl_cb_put(cb);
	
		/* Allocate new message */
		msg = nlmsg_alloc();
		if (!msg) {
			fprintf(stderr, "Failed to allocate netlink message.\n");
			nl_socket_free(ctx.nls);
			return -ENOMEM;
		}
		cb = nl_cb_alloc(NL_CB_CUSTOM);
		if (!cb) {
			fprintf(stderr, "Failed to allocate netlink callback.\n");
			nlmsg_free(msg);
			nl_socket_free(ctx.nls);
			return -ENOMEM;
		}

		/* Send a GET_SURVEY command */
		nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, survey_dump_handler, &ctx);
		genlmsg_put(msg, 0, 0, ctx.nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_SURVEY, 0);
		nla_put_u32(msg, NL80211_ATTR_IFINDEX, ctx.if_index[i]);
		nl_send_auto_complete(ctx.nls, msg);


		/* Wait for shit to finish */
		err = 1;
		nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
		while (err > 0)
			nl_recvmsgs(ctx.nls, cb);

		/* Free the shite */
		nlmsg_free(msg);
		nl_cb_put(cb);
	}
	nl_socket_free(ctx.nls);
	return 0;
}

/* Single function HTTP/1.0 web server */
static void http_handler(FILE *stream) {
	char status[80] = {0};
	char *rv = fgets(status, sizeof(status) - 1, stream);
	if (rv != status) {
		fprintf(stderr, "fgets error: %s\n", strerror(errno));
		return;
	}
	char *saveptr;
	char *method = strtok_r(status, " \t\r\n", &saveptr);
	if (strncmp(method, "GET", 4) != 0) {
		fputs("HTTP/1.0 405 Method Not Allowed\r\n", stream);
		return;
	}
	char *request_uri = strtok_r(NULL, " \t", &saveptr);
	char *protocol = strtok_r(NULL, " \t\r\n", &saveptr);
	if (strncmp(protocol, "HTTP/1.", 7) != 0) {
		fputs("HTTP/1.0 400 Bad Request\r\n", stream);
		return;
	}
	/* Read the other headers */
	for (;;) {
		char header[1024];
		char *rv = fgets(header, sizeof(header) - 1, stream);
		if (rv != header) {
			fprintf(stderr, "fgets error: %s\n", strerror(errno));
			return;
		}
		if (header[0] == '\n' || header[1] == '\n') {
			break;
		}
	}
	if (strcmp(request_uri, "/") == 0) {
		fprintf(stream, "HTTP/1.0 200 OK\r\nContent-Length: %lu\r\n\r\n", strlen(ROOTPAGE));
		fputs(ROOTPAGE, stream);
		return;
	}
	if (strcmp(request_uri, "/metrics") != 0) {
		fprintf(stream, "HTTP/1.0 404 Not Found\r\nContent-Length: %lu\r\n\r\n", strlen(NOT_FOUND_ERROR));
		fputs(NOT_FOUND_ERROR, stream);
		return;
	}
	fputs("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n", stream);
	show_metrics(stream);
}

/* Generic TCP server set-up with multiple sockets */
static void start_listen(char *node, char *service, int *fd, int max_fd)
{
	struct addrinfo hints, *res, *p;
	int numfds = 0;

	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(node, service, &hints, &res) != 0) {
		perror ("getaddrinfo() error");
		exit(1);
	}

	for (p = res; p != NULL && numfds < max_fd; p = p->ai_next) {
		int listenfd = socket (p->ai_family, p->ai_socktype, 0);
		if (listenfd == -1) {
			continue;
		}
		int enable = 1;
		int rv = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
		char address[INET6_ADDRSTRLEN];
		if (rv < 0) {
			perror("Failed setsockopt");
		}
		if (p->ai_family == AF_INET6) {
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *)p->ai_addr)->sin6_addr, address, INET6_ADDRSTRLEN);
			printf("IPv6 Address [%s]:%d\n", address, ntohs(((struct sockaddr_in *)p->ai_addr)->sin_port));
			rv = setsockopt(listenfd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&enable, sizeof(enable));
			if (rv < 0) {
				perror("Failed setsockopt");
			}
		} else if (p->ai_family == AF_INET) {
			char str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &((struct sockaddr_in *)p->ai_addr)->sin_addr, address, INET6_ADDRSTRLEN);
			printf("IPv4 Address %s:%d\n", address, ntohs(((struct sockaddr_in *)p->ai_addr)->sin_port));
		}
		if (bind(listenfd, p->ai_addr, p->ai_addrlen) < 0) {
			fprintf(stderr, "bind() error for %s: %s\n", address, strerror(errno));
		}
		if ( listen (listenfd, 1000000) != 0 ) {
			fprintf(stderr, "listen() error for %s: %s\n", address, strerror(errno));
		}
		fd[numfds] = listenfd;
		numfds++;
	}

	freeaddrinfo(res);

	return;
}

/* Socket epoll, linux-specific */
union my_sockaddr {
	struct sockaddr_storage storage;
	struct sockaddr addr;
	struct sockaddr_in sin_addr;
	struct sockaddr_in6 sin6_addr;
};
int main (int argc, char **argv)
{
	UNUSED(argc);
	UNUSED(argv);
	int fd[2] = {0};
	start_listen(NULL, "9100", fd, 2);
	int epollfd = epoll_create1(0);
	for (int i = 0; i < 2; i++) {
		struct epoll_event ev = {0};
		ev.events = EPOLLIN;
		ev.data.fd = fd[i];
		int rv = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd[i], &ev);
		if (rv == -1) {
			fprintf(stderr, "epoll add failed for socket %d (fd %d): %s\n", i, fd[i], strerror(errno));
		}
	}
	for(;;) {
		struct epoll_event events[10] = {0};
		int nfds = epoll_wait(epollfd, events, 10, -1);
		if (nfds == -1) {
			fprintf(stderr, "epoll wait failed: %s", strerror(errno));
		}
		for (int i = 0; i < nfds; i++) {
			union my_sockaddr addr = {0};
			socklen_t addrlen = {0};
			int conn_sock = accept(events[i].data.fd, &addr.addr, &addrlen);
			if (conn_sock < 0) {
				fprintf(stderr, "Accept failed: %s", strerror(errno));
				continue;
			}
			FILE *stream = fdopen(conn_sock, "r+");
			if (stream == NULL) {
				printf("Error: %s\n", strerror(errno));
			}
			if (!fork()) { http_handler(stream); fclose(stream); exit(0); }
			fclose(stream);
		}
	}
	
	return 0;
}
