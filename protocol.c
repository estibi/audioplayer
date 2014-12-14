#import "protocol.h"

int
send_packet(int fd, info_t info, char *s)
{
	int len;
	unsigned int str_size, buf_size;
	const int MAX_STRING = 256;
	char *p, *raw_buf;

	struct pkt_header pkt_hdr;

	pkt_hdr.info = htonl(info);

	if (s) {
		str_size = strnlen(s, MAX_STRING);
		pkt_hdr.size = htonl(str_size + 1);
		buf_size = sizeof (pkt_hdr) + str_size + 1;
	} else {
		str_size = 0;
		pkt_hdr.size = htonl(0);
		buf_size = sizeof (pkt_hdr);
	}

	raw_buf = malloc(buf_size);
	if (!raw_buf) {
		printf("malloc error: %s\n", strerror(errno));
		return (-1);
	}

	// copy packet header
	p = raw_buf;
	memcpy(p, &pkt_hdr, sizeof (pkt_hdr));

	if (s) {
		// copy command string
		p += sizeof (pkt_hdr);
		memcpy(p, s, str_size);

		// terminate string with 0
		p += str_size;
		memset(p, '\0', 1);
	}

	len = write(fd, raw_buf, buf_size);
	if (len != buf_size) {
		printf("write error: %s\n", strerror(errno));
		printf("len: %d\n", len);
		printf("buf_size: %d\n", (unsigned int) buf_size);
		free(raw_buf);
		return (-1);
	}
	free(raw_buf);
	return (0);
}
