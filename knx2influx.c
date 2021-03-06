#define _GNU_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>

#include <curl/curl.h>

#include "knx.h"
#include "conversion.h"
#include "config.h"

#define MULTICAST_PORT            3671 // [Default 3671]
#define MULTICAST_IP              "224.0.23.12" // [Default IPAddress(224, 0, 23, 12)]


///////////////////////////////////////////////////////////////////////////////

static config_t config;
static int socket_fd;
static struct ip_mreq command = {};

void exithandler()
{
	int loop = 1;
	if (setsockopt(socket_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		perror("setsockopt (IP_DROP_MEMBERSHIP): ");
	}
	close(socket_fd);
}

size_t curl_write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
   return size * nmemb;
}

void post(char const *data)
{
	CURLcode ret;
	CURL *hnd;

	hnd = curl_easy_init();
	char host[1024];
	host[0] = 0;
	strcat(host, config.host);
	strcat(host, "/write?db=");
	strcat(host, config.database);
	curl_easy_setopt(hnd, CURLOPT_URL, host);
	curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(hnd, CURLOPT_HEADER, 1L);
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt(hnd, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)strlen(data));
	curl_easy_setopt(hnd, CURLOPT_USERAGENT, "knx2influx");
	curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 50L);
	curl_easy_setopt(hnd, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 0L);

	curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, curl_write_data);

	ret = curl_easy_perform(hnd);
	curl_easy_cleanup(hnd);
}

void format_dpt(ga_t *entry, char *_post, uint8_t *data)
{
	switch (entry->dpt)
	{
		case 1:
		{
			bool val = data_to_bool(data);
			strcat(_post, "value=");
			if (entry->convert_dpt1_to_int == 1)
			{
				strcat(_post, val ? "1" : "0");
			}
			else
			{
				strcat(_post, val ? "t" : "f");
			}
			break;
		}
		case 2:
		{
			bool val = data_to_bool(data);
			strcat(_post, "value=");
			strcat(_post, val ? "t" : "f");
			uint8_t other_bit = data[0] >> 1;
			bool control = data_to_bool(&other_bit);
			strcat(_post, ",control=");
			strcat(_post, control ? "t" : "f");
			break;
		}
		case 5:
		{
			uint8_t val = data_to_1byte_uint(data);
			char buf[4];
			snprintf(buf, 4, "%u", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 6:
		{
			int8_t val = data_to_1byte_int(data);
			char buf[5];
			snprintf(buf, 5, "%d", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 7:
		{
			uint16_t val = data_to_2byte_uint(data);
			char buf[6];
			snprintf(buf, 6, "%u", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 8:
		{
			int16_t val = data_to_2byte_int(data);
			char buf[7];
			snprintf(buf, 7, "%d", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 9:
		{
			float val = data_to_2byte_float(data);
			char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
			snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			break;
		}
		case 12:
		{
			uint32_t val = data_to_4byte_uint(data);
			char buf[11];
			snprintf(buf, 11, "%u", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 13:
		{
			int32_t val = data_to_4byte_int(data);
			char buf[12];
			snprintf(buf, 12, "%d", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			strcat(_post, "i");
			break;
		}
		case 14:
		{
			float val = data_to_4byte_float(data);
			char buf[3 + DBL_MANT_DIG - DBL_MIN_EXP + 1];
			snprintf(buf, 3 + DBL_MANT_DIG - DBL_MIN_EXP + 1, "%f", val);
			strcat(_post, "value=");
			strcat(_post, buf);
			break;
		}
	}

}

void process_packet(uint8_t *buf, size_t len)
{
	knx_ip_pkt_t *knx_pkt = (knx_ip_pkt_t *)buf;

	if (knx_pkt->header_len != 0x06 && knx_pkt->protocol_version != 0x10 && knx_pkt->service_type != KNX_ST_ROUTING_INDICATION)
		return;

	cemi_msg_t *cemi_msg = (cemi_msg_t *)knx_pkt->pkt_data;

	if (cemi_msg->message_code != KNX_MT_L_DATA_IND)
		return;

	cemi_service_t *cemi_data = &cemi_msg->data.service_information;

	if (cemi_msg->additional_info_len > 0)
		cemi_data = (cemi_service_t *)(((uint8_t *)cemi_data) + cemi_msg->additional_info_len);

	if (cemi_data->control_2.bits.dest_addr_type != 0x01)
		return;

	knx_command_type_t ct = (knx_command_type_t)(((cemi_data->data[0] & 0xC0) >> 6) | ((cemi_data->pci.apci & 0x03) << 2));

	// Only accept writes
	if (ct != KNX_CT_WRITE)
		return;

	ga_t *entry = config.gas[cemi_data->destination.value];
	while(entry != NULL)
	{
		uint8_t data[cemi_data->data_len];

		// Check if sender is blacklisted
		for (int i = 0; i < entry->ignored_senders_len; ++i)
		{
			address_t a_cur = entry->ignored_senders[i];
			if (a_cur.value == cemi_data->source.value)
			{
				printf("Ignoring sender %u.%u.%u for %u/%u/%u\n", cemi_data->source.pa.area, cemi_data->source.pa.line, cemi_data->source.pa.member, cemi_data->destination.ga.area, cemi_data->destination.ga.line, cemi_data->destination.ga.member);
				goto next;
			}
		}


		memcpy(data, cemi_data->data, cemi_data->data_len);
		data[0] = data[0] & 0x3F;

		char _post[1024];
		memset(_post, 0, 1024);
		strcat(_post, entry->series);

		// Add tags, first the ones from the GA entries
		strcat(_post, ",sender=");
		char sbuf[2+1+2+1+3+1];
		snprintf(sbuf, 2+1+2+1+3+1, "%u.%u.%u", cemi_data->source.pa.area, cemi_data->source.pa.line, cemi_data->source.pa.member);
		strcat(_post, sbuf);
		for (size_t i = 0; i < entry->tags_len; ++i)
		{
			strcat(_post, ",");
			strcat(_post, entry->tags[i]);
		}
		// Now the sender tags
		sender_tags_t *sender_tag = config.sender_tags[cemi_data->source.value];
		while (sender_tag != NULL)
		{
			for (size_t i = 0; i < sender_tag->tags_len; ++i)
			{
				strcat(_post, ",");
				strcat(_post, sender_tag->tags[i]);
			}

			sender_tag = sender_tag->next;
		}

		// Seperate tags from values
		strcat(_post, " ");

		format_dpt(entry, _post, data);

		printf("%s\n", _post);
		post(_post);

next:
		entry = entry->next;
	}
}

void print_config()
{
	// Sender tags
	for (uint16_t i = 0; i < UINT16_MAX; ++i)
	{
		if (config.sender_tags[i] != NULL)
		{
			address_t a = {.value = i};
			printf("%02u.%02u.%03u ", a.pa.area, a.pa.line, a.pa.member);
			bool first = true;
			sender_tags_t *entry = config.sender_tags[i];
			while (entry != NULL)
			{
				if (first)
				{
					first = false;
				}
				else
				{
					printf("          ");
				}
				printf("+ [");
				bool first_tag = true;
				for (size_t i = 0; i < entry->tags_len; ++i)
				{
					if (first_tag)
					{
						first_tag = false;
					}
					else
					{
						printf(", ");
					}
					printf("%s", entry->tags[i]);
				}
				printf("]\n");
				entry = entry->next;
			}
		}
	}
	printf("\n");
	// Group addresses
	for (uint16_t i = 0; i < UINT16_MAX; ++i)
	{
		if (config.gas[i] != NULL)
		{
			address_t a = {.value = i};
			printf("%02u/%02u/%03u ", a.ga.area, a.ga.line, a.ga.member);
			ga_t *entry = config.gas[i];
			bool first = true;
			while (entry != NULL)
			{
				if (first)
				{
					first = false;
				}
				else
				{
					printf("          ");
				}
				printf("-> %s (%u%s) ", entry->series, entry->dpt, entry->convert_dpt1_to_int == 1 ? " conv to int" : "");

				bool first_tag = true;
				printf("[");
				for (size_t i = 0; i < entry->tags_len; ++i)
				{
					if (first_tag)
					{
						first_tag = false;
					}
					else
					{
						printf(", ");
					}
					printf("%s", entry->tags[i]);
				}
				printf("]\n");

				entry = entry->next;
			}
		}
	}

	exit(0);
}

int main(int argc, char **argv)
{
	// Init config
	memset(&config, 0, sizeof(config_t));

	// Parse config first
	if (parse_config(&config) < 0)
	{
		printf("Error parsing JSON.\n");
		exit(EXIT_FAILURE);
	}

	// Print config
	//print_config();

	printf("Sending data to %s database %s\n", config.host, config.database);

	struct sockaddr_in sin = {};
	sin.sin_family = PF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(MULTICAST_PORT);
	if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("socket: ");
		exit(EXIT_FAILURE);
	}
	//printf("Our socket fd is %d\n", socket_fd);

	int loop = 1;
	if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &loop, sizeof(loop)) < 0)
	{
		perror("setsockopt (SO_REUSEADDR): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	if (bind(socket_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		perror("bind: ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	loop = 1;
	if (setsockopt(socket_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
	{
		perror("setsockopt (IP_MULTICAST_LOOP): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	command.imr_multiaddr.s_addr = inet_addr(MULTICAST_IP);
	command.imr_interface.s_addr = inet_addr(config.interface);

	if (command.imr_multiaddr.s_addr == -1)
	{
		perror(MULTICAST_IP" is not a valid multicast address: ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	if (setsockopt(socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &command, sizeof(command)) < 0)
	{
		perror("setsockopt (IP_ADD_MEMBERSHIP): ");
		close(socket_fd);
		exit(EXIT_FAILURE);
	}

	atexit(exithandler);

	uint8_t buf[512];
	ssize_t rec = 0;

	while(1)
	{
		int sin_len = sizeof(sin);
		if ((rec = recvfrom(socket_fd, buf, 512, 0, (struct sockaddr *) &sin, &sin_len)) == -1)
		{
			perror("recfrom: ");
			break;
		}
		/*
		printf("Got %d bytes: ", rec);
		for (ssize_t i = 0; i < rec; ++i)
			printf("%02x ", buf[i]);
		printf("\n");
		*/
		process_packet(buf, rec);
	}

	return 0;
}

