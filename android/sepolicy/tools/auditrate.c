/*
 * auditrate: sets the kernel audit rate limit and backlog through NETLINK_AUDIT (AUDIT_SET), like "auditctl -r N -b M".
 * logd limits SELinux denials to 5 per second, so most of them are lost while a policy is being written.
 * usage: auditrate RATE [BACKLOG]   (RATE 0 = unlimited; prints the status before and after)
 */
#include <linux/audit.h>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int talk(int fd, int type, const void *data, int len, struct audit_status *st)
{
	struct {
		struct nlmsghdr h;
		char buf[1024];
	} req, rep;
	struct sockaddr_nl to = { .nl_family = AF_NETLINK };

	memset(&req, 0, sizeof(req));
	req.h.nlmsg_len = NLMSG_LENGTH(len);
	req.h.nlmsg_type = type;
	req.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.h.nlmsg_seq = type;
	memcpy(NLMSG_DATA(&req.h), data, len);
	if (sendto(fd, &req, req.h.nlmsg_len, 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
		perror("sendto");
		return -1;
	}
	for (int i = 0; i < 4; i++) {
		int n = recv(fd, &rep, sizeof(rep), 0);

		if (n < 0) {
			perror("recv");
			return -1;
		}
		if (rep.h.nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *e = NLMSG_DATA(&rep.h);

			if (e->error) {
				fprintf(stderr, "audit netlink error %d\n", e->error);
				return -1;
			}
			if (type != AUDIT_GET)
				return 0;
		} else if (rep.h.nlmsg_type == AUDIT_GET && st) {
			memcpy(st, NLMSG_DATA(&rep.h), sizeof(*st));
			return 0;
		}
	}
	return 0;
}

static void show(int fd, const char *when)
{
	struct audit_status st = { 0 };

	if (!talk(fd, AUDIT_GET, &st, 0, &st))
		printf("%s: enabled %u pid %u rate_limit %u backlog_limit %u lost %u backlog %u\n", when, st.enabled, st.pid,
		       st.rate_limit, st.backlog_limit, st.lost, st.backlog);
}

int main(int argc, char **argv)
{
	struct audit_status st = { 0 };
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_AUDIT);

	if (argc < 2) {
		fprintf(stderr, "usage: %s RATE [BACKLOG]\n", argv[0]);
		return 2;
	}
	if (fd < 0) {
		perror("socket");
		return 1;
	}
	show(fd, "before");
	st.mask = AUDIT_STATUS_RATE_LIMIT;
	st.rate_limit = strtoul(argv[1], NULL, 0);
	if (argc > 2) {
		st.mask |= AUDIT_STATUS_BACKLOG_LIMIT;
		st.backlog_limit = strtoul(argv[2], NULL, 0);
	}
	if (talk(fd, AUDIT_SET, &st, sizeof(st), NULL))
		return 1;
	show(fd, "after");
	close(fd);
	return 0;
}
