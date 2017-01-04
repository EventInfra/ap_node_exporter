#include <microhttpd.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#define ROOTPAGE  "<html><head><title>Metrics exporter</title></head><body><ul><li><a href=\"/metrics\">metrics</a></li></ul></body></html>"
#define NOT_FOUND_ERROR "<html><head><title>Document not found</title></head><body><h1>404 - Document not found</h1></body></html>"
#define UNUSED(x) (void)(x)

static int finish_handler(struct nl_msg *msg, void *arg)
{
	UNUSED(msg);
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static int list_interface_handler(struct nl_msg *msg, void *arg)
{
	UNUSED(arg);
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));

	nla_parse(tb_msg, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
			genlmsg_attrlen(gnlh, 0), NULL);

	if (tb_msg[NL80211_ATTR_IFNAME]) {
		printf("Interface: %s\n", nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
	}

	return NL_SKIP;
}

static int station_dump_handler(struct nl_msg *msg, void *arg)
{
	UNUSED(arg);
	struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
	static struct nla_policy stats_policy[NL80211_TID_STATS_MAX + 1] = {
		[NL80211_TID_STATS_RX_MSDU] = { .type = NLA_U64 },
		[NL80211_TID_STATS_TX_MSDU] = { .type = NLA_U64 },
		[NL80211_TID_STATS_TX_MSDU_RETRIES] = { .type = NLA_U64 },
		[NL80211_TID_STATS_TX_MSDU_FAILED] = { .type = NLA_U64 },
	};

	printf("Shucks. What happened?\n");
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

	/* Handle / */
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
	struct nl_sock *nls;
	nls = nl_socket_alloc();
	if (!nls) {
		response = MHD_create_response_from_buffer (strlen ("Shucks. Could not alloc netlink socket."),
				(void *) "Shucks. Could not alloc netlink socket.", MHD_RESPMEM_PERSISTENT);
		ret = MHD_queue_response (connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
		MHD_destroy_response (response);
		return ret;
	}
	nl_socket_set_buffer_size(nls, 16384, 16384);
	if (genl_connect(nls)) {
		fprintf(stderr, "Failed to connect to generic netlink.\n");
		nl_socket_free(nls);
		return -ENOLINK;
	}
	int nl80211_id = genl_ctrl_resolve(nls, "nl80211");
	if (nl80211_id < 0) {
		fprintf(stderr, "nl80211 not found.\n");
		nl_socket_free(nls);
		return -ENOENT;
	}

	struct nl_msg *msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Failed to allocate netlink message.\n");
		return -ENOMEM;
	}
	struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callback.\n");
		nlmsg_free(msg);
		return -ENOMEM;
	}

	/* Send a GET_INTERFACE command */
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, list_interface_handler, NULL);
	genlmsg_put(msg, 0, 0, nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_INTERFACE, 0);
	nl_send_auto_complete(nls, msg);

	/* Wait for shit to finish */
	int err = 1;
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	while (err > 0)
		nl_recvmsgs(nls, cb);

	/* Free the shite */
	nlmsg_free(msg);
	nl_cb_put(cb);

	/* Test */
	msg = nlmsg_alloc();
	if (!msg) {
		fprintf(stderr, "Failed to allocate netlink message.\n");
		return -ENOMEM;
	}
	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) {
		fprintf(stderr, "Failed to allocate netlink callback.\n");
		nlmsg_free(msg);
		return -ENOMEM;
	}
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, station_dump_handler, NULL);
	genlmsg_put(msg, 0, 0, nl80211_id, 0, NLM_F_DUMP, NL80211_CMD_GET_STATION, 0);
	nl_send_auto_complete(nls, msg);


	/* Wait for shit to finish */
	err = 1;
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	while (err > 0)
		nl_recvmsgs(nls, cb);

	/* Free the shite */
	nlmsg_free(msg);
	nl_cb_put(cb);

	nl_socket_free(nls);

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
