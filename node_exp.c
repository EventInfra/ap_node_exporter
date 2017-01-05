#include <microhttpd.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define ROOTPAGE  "<html><head><title>Metrics exporter</title></head><body><ul><li><a href=\"/metrics\">metrics</a></li></ul></body></html>"
#define NOT_FOUND_ERROR "<html><head><title>Document not found</title></head><body><h1>404 - Document not found</h1></body></html>"
#define UNUSED(x) (void)(x)

#define BIT(x) (1ULL<<(x))

struct config {
	int nl80211_id;
	struct nl_sock *nls;
	int if_index[16];
	int if_count;
};

static int finish_handler(struct nl_msg *msg, void *arg)
{
	UNUSED(msg);
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

/* TODO: Split into separate metrics */
void parse_bitrate(struct nlattr *bitrate_attr, char *buf, int buflen)
{
	int rate = 0;
	char *pos = buf;
	struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
	static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
		[NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
		[NL80211_RATE_INFO_BITRATE32] = { .type = NLA_U32 },
		[NL80211_RATE_INFO_MCS] = { .type = NLA_U8 },
		[NL80211_RATE_INFO_40_MHZ_WIDTH] = { .type = NLA_FLAG },
		[NL80211_RATE_INFO_SHORT_GI] = { .type = NLA_FLAG },
	};

	if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX,
			     bitrate_attr, rate_policy)) {
		snprintf(buf, buflen, "failed to parse nested rate attributes!");
		return;
	}

	if (rinfo[NL80211_RATE_INFO_BITRATE32])
		rate = nla_get_u32(rinfo[NL80211_RATE_INFO_BITRATE32]);
	else if (rinfo[NL80211_RATE_INFO_BITRATE])
		rate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
	if (rate > 0)
		pos += snprintf(pos, buflen - (pos - buf),
				"%d.%d MBit/s", rate / 10, rate % 10);

	if (rinfo[NL80211_RATE_INFO_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_MCS]));
	if (rinfo[NL80211_RATE_INFO_VHT_MCS])
		pos += snprintf(pos, buflen - (pos - buf),
				" VHT-MCS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_MCS]));
	if (rinfo[NL80211_RATE_INFO_40_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 40MHz");
	if (rinfo[NL80211_RATE_INFO_80_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 80MHz");
	if (rinfo[NL80211_RATE_INFO_80P80_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 80P80MHz");
	if (rinfo[NL80211_RATE_INFO_160_MHZ_WIDTH])
		pos += snprintf(pos, buflen - (pos - buf), " 160MHz");
	if (rinfo[NL80211_RATE_INFO_SHORT_GI])
		pos += snprintf(pos, buflen - (pos - buf), " short GI");
	if (rinfo[NL80211_RATE_INFO_VHT_NSS])
		pos += snprintf(pos, buflen - (pos - buf),
				" VHT-NSS %d", nla_get_u8(rinfo[NL80211_RATE_INFO_VHT_NSS]));
}


/* TODO: Split into separate metrics */
static char *get_chain_signal(struct nlattr *attr_list)
{
	struct nlattr *attr;
	static char buf[64];
	char *cur = buf;
	int i = 0, rem;
	const char *prefix;

	if (!attr_list)
		return "";

	nla_for_each_nested(attr, attr_list, rem) {
		if (i++ > 0)
			prefix = ", ";
		else
			prefix = "[";

		cur += snprintf(cur, sizeof(buf) - (cur - buf), "%s%d", prefix,
				(int8_t) nla_get_u8(attr));
	}

	if (i)
		snprintf(cur, sizeof(buf) - (cur - buf), "] ");

	return buf;
}

/* TODO: Put metrics in web page */
static int station_dump_handler(struct nl_msg *msg, void *arg)
{
	UNUSED(arg);
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
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
		[NL80211_STA_INFO_LLID] = { .type = NLA_U16 },
		[NL80211_STA_INFO_PLID] = { .type = NLA_U16 },
		[NL80211_STA_INFO_PLINK_STATE] = { .type = NLA_U8 },
		[NL80211_STA_INFO_TX_RETRIES] = { .type = NLA_U32 },
		[NL80211_STA_INFO_TX_FAILED] = { .type = NLA_U32 },
		[NL80211_STA_INFO_BEACON_LOSS] = { .type = NLA_U32},
		[NL80211_STA_INFO_RX_DROP_MISC] = { .type = NLA_U64},
		[NL80211_STA_INFO_STA_FLAGS] =
	{ .minlen = sizeof(struct nl80211_sta_flag_update) },
		[NL80211_STA_INFO_LOCAL_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_PEER_PM] = { .type = NLA_U32},
		[NL80211_STA_INFO_NONPEER_PM] = { .type = NLA_U32},
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
	char mac_addr[ETH_ALEN*3];
	snprintf(mac_addr, ETH_ALEN*3, "%02d:%02d:%02d:%02d:%02d:%02d",
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[0],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[1],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[2],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[3],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[4],
			((uint8_t *)nla_data(tb_msg[NL80211_ATTR_MAC]))[5]);

	char dev[IF_NAMESIZE];
	if_indextoname(nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]), dev);
	printf("Station %s (on %s)\n", mac_addr, dev);

	if (sinfo[NL80211_STA_INFO_INACTIVE_TIME]) {
		printf("\n\tinactive time:\t%u ms",
			nla_get_u32(sinfo[NL80211_STA_INFO_INACTIVE_TIME]));
	}
	if (sinfo[NL80211_STA_INFO_RX_BYTES64]) {
		printf("\n\trx bytes:\t%llu",
		       (unsigned long long)nla_get_u64(sinfo[NL80211_STA_INFO_RX_BYTES64]));
	}
	else if (sinfo[NL80211_STA_INFO_RX_BYTES]) {
		printf("\n\trx bytes:\t%u",
		       nla_get_u32(sinfo[NL80211_STA_INFO_RX_BYTES]));
	}
	if (sinfo[NL80211_STA_INFO_RX_PACKETS]) {
		printf("\n\trx packets:\t%u",
			nla_get_u32(sinfo[NL80211_STA_INFO_RX_PACKETS]));
	}
	if (sinfo[NL80211_STA_INFO_TX_BYTES64]) {
		printf("\n\ttx bytes:\t%llu",
		       (unsigned long long)nla_get_u64(sinfo[NL80211_STA_INFO_TX_BYTES64]));
	} else if (sinfo[NL80211_STA_INFO_TX_BYTES]) {
		printf("\n\ttx bytes:\t%u",
		       nla_get_u32(sinfo[NL80211_STA_INFO_TX_BYTES]));
	}
	if (sinfo[NL80211_STA_INFO_TX_PACKETS]) {
		printf("\n\ttx packets:\t%u",
			nla_get_u32(sinfo[NL80211_STA_INFO_TX_PACKETS]));
	}
	if (sinfo[NL80211_STA_INFO_TX_RETRIES]) {
		printf("\n\ttx retries:\t%u",
			nla_get_u32(sinfo[NL80211_STA_INFO_TX_RETRIES]));
	}
	if (sinfo[NL80211_STA_INFO_TX_FAILED]) {
		printf("\n\ttx failed:\t%u",
			nla_get_u32(sinfo[NL80211_STA_INFO_TX_FAILED]));
	}
	if (sinfo[NL80211_STA_INFO_BEACON_LOSS]) {
		printf("\n\tbeacon loss:\t%u",
		       nla_get_u32(sinfo[NL80211_STA_INFO_BEACON_LOSS]));
	}
	if (sinfo[NL80211_STA_INFO_BEACON_RX]) {
		printf("\n\tbeacon rx:\t%llu",
		       (unsigned long long)nla_get_u64(sinfo[NL80211_STA_INFO_BEACON_RX]));
	}
	if (sinfo[NL80211_STA_INFO_RX_DROP_MISC]) {
		printf("\n\trx drop misc:\t%llu",
		       (unsigned long long)nla_get_u64(sinfo[NL80211_STA_INFO_RX_DROP_MISC]));
	}

	char *chain = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL]);
	if (sinfo[NL80211_STA_INFO_SIGNAL]) {
		printf("\n\tsignal:  \t%d %sdBm",
			(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]),
			chain);
	}

	chain = get_chain_signal(sinfo[NL80211_STA_INFO_CHAIN_SIGNAL_AVG]);
	if (sinfo[NL80211_STA_INFO_SIGNAL_AVG]) {
		printf("\n\tsignal avg:\t%d %sdBm",
			(int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL_AVG]),
			chain);
	}
	if (sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]) {
		printf("\n\tbeacon signal avg:\t%d dBm",
		       nla_get_u8(sinfo[NL80211_STA_INFO_BEACON_SIGNAL_AVG]));
	}
	if (sinfo[NL80211_STA_INFO_T_OFFSET]) {
		printf("\n\tToffset:\t%llu us",
		       (unsigned long long)nla_get_u64(sinfo[NL80211_STA_INFO_T_OFFSET]));
	}

	if (sinfo[NL80211_STA_INFO_TX_BITRATE]) {
		char buf[100];

		parse_bitrate(sinfo[NL80211_STA_INFO_TX_BITRATE], buf, sizeof(buf));
		printf("\n\ttx bitrate:\t%s", buf);
	}

	if (sinfo[NL80211_STA_INFO_RX_BITRATE]) {
		char buf[100];

		parse_bitrate(sinfo[NL80211_STA_INFO_RX_BITRATE], buf, sizeof(buf));
		printf("\n\trx bitrate:\t%s", buf);
	}

	if (sinfo[NL80211_STA_INFO_RX_DURATION]) {
		printf("\n\trx duration:\t%lld us",
		       (unsigned long long)nla_get_u64(sinfo[NL80211_STA_INFO_RX_DURATION]));
	}

	if (sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]) {
		uint32_t thr;

		thr = nla_get_u32(sinfo[NL80211_STA_INFO_EXPECTED_THROUGHPUT]);
		/* convert in Mbps but scale by 1000 to save kbps units */
		thr = thr * 1000 / 1024;

		printf("\n\texpected throughput:\t%u.%uMbps",
		       thr / 1000, thr % 1000);
	}

	if (sinfo[NL80211_STA_INFO_STA_FLAGS]) {
		struct nl80211_sta_flag_update *sta_flags = (struct nl80211_sta_flag_update *)
			    nla_data(sinfo[NL80211_STA_INFO_STA_FLAGS]);

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
			printf("\n\tauthorized:\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_AUTHORIZED))
				printf("yes");
			else
				printf("no");
		}

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_AUTHENTICATED)) {
			printf("\n\tauthenticated:\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_AUTHENTICATED))
				printf("yes");
			else
				printf("no");
		}

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_ASSOCIATED)) {
			printf("\n\tassociated:\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_ASSOCIATED))
				printf("yes");
			else
				printf("no");
		}

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE)) {
			printf("\n\tpreamble:\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_SHORT_PREAMBLE))
				printf("short");
			else
				printf("long");
		}

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_WME)) {
			printf("\n\tWMM/WME:\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_WME))
				printf("yes");
			else
				printf("no");
		}

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_MFP)) {
			printf("\n\tMFP:\t\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_MFP))
				printf("yes");
			else
				printf("no");
		}

		if (sta_flags->mask & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
			printf("\n\tTDLS peer:\t");
			if (sta_flags->set & BIT(NL80211_STA_FLAG_TDLS_PEER))
				printf("yes");
			else
				printf("no");
		}
	}

	if (sinfo[NL80211_STA_INFO_TID_STATS] && arg != NULL &&
	    !strcmp((char *)arg, "-v")) {
		//parse_tid_stats(sinfo[NL80211_STA_INFO_TID_STATS]);
	}
	if (sinfo[NL80211_STA_INFO_BSS_PARAM]) {
		//parse_bss_param(sinfo[NL80211_STA_INFO_BSS_PARAM]);
	}
	if (sinfo[NL80211_STA_INFO_CONNECTED_TIME]) {
		printf("\n\tconnected time:\t%u seconds\n",
			nla_get_u32(sinfo[NL80211_STA_INFO_CONNECTED_TIME]));
	}
	return NL_SKIP;
}

static int list_interface_handler(struct nl_msg *in_msg, void *arg)
{
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(in_msg));
	struct config *cfg = (struct config *)arg;
	uint32_t if_index;

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_IFNAME]) {
		printf("Interface name: %s\n", nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
	}
	if (tb_msg[NL80211_ATTR_IFINDEX]) {
		cfg->if_index[cfg->if_count] = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
		printf("Interface no: %d\n", cfg->if_index[cfg->if_count]);
		cfg->if_count++;
	}

	return NL_SKIP;
}


static int ahc_echo (void *cls,
		struct MHD_Connection *connection,
		const char *url,
		const char *method,
		const char *version,
		const char *upload_data, size_t * upload_data_size, void **ptr)
{
	UNUSED(cls);
	UNUSED(version);
	UNUSED(upload_data);
	static int dummy;
	struct MHD_Response *response;
	int ret;
	struct config config = {0};
	/* Unexpected method */
	if (strcmp(method, "GET") != 0) {
		return MHD_NO;
	}
	if (*ptr != &dummy) {
		/*
		 * The first time only the headers are valid, do not respond in
		 * the first round...
		 */
		*ptr = &dummy;
		return MHD_YES;
	}

	/* upload data in a GET!? */
	if (*upload_data_size != 0) {
		return MHD_NO;
	}

	/* clear context pointer */
	*ptr = NULL;

	/* Handle / (root) */
	if (strcmp(url, "/") == 0) {
		response = MHD_create_response_from_buffer (strlen (ROOTPAGE),
				ROOTPAGE, MHD_RESPMEM_PERSISTENT);
		ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
		MHD_destroy_response (response);
		return ret;
	}
	/* Give a 404 if somebody requests anything but / or /metrics */
	if (strcmp(url, "/metrics") != 0) {
		response = MHD_create_response_from_buffer (strlen (NOT_FOUND_ERROR),
				(void *) NOT_FOUND_ERROR, MHD_RESPMEM_PERSISTENT);
		ret = MHD_queue_response (connection, MHD_HTTP_NOT_FOUND, response);
		MHD_destroy_response (response);
		return ret;
	}

	/* Set up the netlink socket */
	config.nls = nl_socket_alloc();
	if (!config.nls) {
		response = MHD_create_response_from_buffer (strlen ("Shucks. Could not alloc netlink socket."),
				(void *) "Shucks. Could not alloc netlink socket.", MHD_RESPMEM_PERSISTENT);
		ret = MHD_queue_response (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
		MHD_destroy_response (response);
		return ret;
	}
	nl_socket_set_buffer_size(config.nls, 16384, 16384);
	if (genl_connect(config.nls)) {
		fprintf(stderr, "Failed to connect to generic netlink.\n");
		nl_socket_free(config.nls);
		return -ENOLINK;
	}
	config.nl80211_id = genl_ctrl_resolve(config.nls, "nl80211");
	if (config.nl80211_id < 0) {
		fprintf(stderr, "nl80211 not found.\n");
		nl_socket_free(config.nls);
		return -ENOENT;
	}

	struct nl_msg *msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Failed to allocate netlink message.\n");
		return -ENOMEM;
	}
	struct nl_cb *cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callback.\n");
		nlmsg_free(msg);
		return -ENOMEM;
	}

	/* Send a GET_INTERFACE command */
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, list_interface_handler, &config);
	genlmsg_put(msg, 0, 0, config.nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
	nl_send_auto_complete(config.nls, msg);

	/* Wait for shit to finish */
	int err = 1;
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	while (err > 0)
		nl_recvmsgs(config.nls, cb);

	/* Free the shite */
	nlmsg_free(msg);
	nl_cb_put(cb);


	/* Maybe wait here */
	struct nl_msg *out_msg = nlmsg_alloc();
	if (!out_msg) {
		fprintf(stderr, "Failed to allocate netlink message.\n");
		return -ENOMEM;
	}
	cb = nl_cb_alloc(NL_CB_CUSTOM);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callback.\n");
		nlmsg_free(out_msg);
		return -ENOMEM;
	}

	/* Send a GET_STATION command */
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, station_dump_handler, NULL);
	genlmsg_put(out_msg, 0, 0, config.nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
	nla_put_u32(out_msg, NL80211_ATTR_IFINDEX, config.if_index[0]);
	nl_send_auto_complete(config.nls, out_msg);


	/* Wait for shit to finish */
	err = 1;
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	while (err > 0)
		nl_recvmsgs(config.nls, cb);

	/* Free the shite */
	nlmsg_free(out_msg);
	nl_cb_put(cb);

	nl_socket_free(config.nls);

	response = MHD_create_response_from_buffer (strlen ("blub"),
			"blub", MHD_RESPMEM_PERSISTENT);
	ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
	MHD_destroy_response (response);
	return ret;
}

int main (int argc, char **argv)
{
	UNUSED(argc);
	UNUSED(argv);
	struct MHD_Daemon *d;
	d = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY,
			9100, NULL, NULL, &ahc_echo, NULL, MHD_OPTION_END);
	if (d == NULL)
		return 1;
	(void) getc (stdin);
	MHD_stop_daemon (d);
	return 0;
}
