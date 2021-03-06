#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"

address_t *parse_ga(char *ga)
{
	char *string = strdup(ga);
	char *tofree = string;
	char *token;
	uint8_t astart = 0, aend = 0;
	uint8_t lstart = 0, lend = 0;
	uint8_t mstart = 0, mend = 0;
	uint16_t acount = 0, lcount = 0, mcount = 0;

	char *area_s = strsep(&string, "/");
	if (area_s == NULL)
	{
		printf("error parsing GA\n");
		exit(EXIT_FAILURE);
	}
	char *line_s = strsep(&string, "/");
	if (line_s == NULL)
	{
		printf("error parsing GA\n");
		exit(EXIT_FAILURE);
	}
	char *member_s = strsep(&string, "/");

	// Area
	if (area_s[0] == '[')
	{
		// Range
		char *start_s = strsep(&area_s, "-");
		start_s++;
		char *end_s = strsep(&area_s, "-");
		end_s[strlen(end_s)-1] = '\0';
		printf("%s - %s\n", start_s, end_s);
		astart = strtol(start_s, NULL, 10);
		aend = strtol(end_s, NULL, 10);
		acount = mend - mstart + 1;
	}
	else if (area_s[0] == '*')
	{
		// Wildard
		astart = 0;
		aend = 31;
		acount = 32;
	}
	else
	{
		astart = strtol(area_s, NULL, 10);
		aend = astart;
		acount = 1;
	}

	// Line
	if (line_s[0] == '[')
	{
		// Range
		char *start_s = strsep(&line_s, "-");
		start_s++;
		char *end_s = strsep(&line_s, "-");
		end_s[strlen(end_s)-1] = '\0';
		printf("%s - %s\n", start_s, end_s);
		lstart = strtol(start_s, NULL, 10);
		lend = strtol(end_s, NULL, 10);
		lcount = mend - mstart + 1;
	}
	else if (line_s[0] == '*')
	{
		// Wildard
		lstart = 0;
		lend = 7;
		lcount = 8;
	}
	else
	{
		lstart = strtol(line_s, NULL, 10);
		lend = lstart;
		lcount = 1;
	}

	// Member
	if (member_s[0] == '[')
	{
		// Range
		char *start_s = strsep(&member_s, "-");
		start_s++;
		char *end_s = strsep(&member_s, "-");
		end_s[strlen(end_s)-1] = '\0';
		printf("%s - %s\n", start_s, end_s);
		mstart = strtol(start_s, NULL, 10);
		mend = strtol(end_s, NULL, 10);
		mcount = mend - mstart + 1;
	}
	else if (member_s[0] == '*')
	{
		// Wildard
		mstart = 0;
		mend = 255;
		mcount = 256;
	}
	else
	{
		mstart = strtol(member_s, NULL, 10);
		mend = mstart;
		mcount = 1;
	}

	free(tofree);
	// Allocate enough + 1 for end marker
	address_t *addr = calloc(acount * lcount * mcount + 1, sizeof(address_t));

	address_t *cur = addr;
	for (uint16_t a = astart; a <= aend; ++a)
		for (uint16_t l = lstart; l <= lend; ++l)
			for (uint16_t m = mstart; m <= mend; ++m)
			{
				cur->ga.area = a;
				cur->ga.line = l;
				cur->ga.member = m;
				cur++;
			}

	cur = addr;
	while (cur->value != 0)
	{
		printf("%u/%u/%u\n", cur->ga.area, cur->ga.line, cur->ga.member);
		cur++;
	}

	printf("----------\n");
	return addr;
}

int parse_config(config_t *config)
{
	int status = 0;

	// Open config file
	FILE *f = fopen("knx2influx.json", "rb");
	if (f == NULL)
	{
		printf("Could not find config file knx2influx.json!\n");
		status = -1;
		return status;
	}
	fseek(f, 0, SEEK_END);
	uint64_t fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	// Read file
	char *json_str = malloc(fsize + 1);
	fread(json_str, fsize, 1, f);
	fclose(f);
	json_str[fsize] = '\0';

	char *error_ptr;
	// And parse
	cJSON *json = cJSON_Parse(json_str);

	if (json == NULL)
	{
		error_ptr = (char *)cJSON_GetErrorPtr();
		goto error;
	}

	cJSON *interface = cJSON_GetObjectItemCaseSensitive(json, "interface");
	if (cJSON_IsString(interface) && (interface->valuestring != NULL))
	{
		config->interface = strdup(interface->valuestring);
	}
	else
	{
		error_ptr = "No interface given in config!";
		goto error;
	}

	cJSON *host = cJSON_GetObjectItemCaseSensitive(json, "host");
	if (cJSON_IsString(host) && (host->valuestring != NULL))
	{
		config->host = strdup(host->valuestring);
	}
	else
	{
		error_ptr = "No host given in config!";
		goto error;
	}

	cJSON *database = cJSON_GetObjectItemCaseSensitive(json, "database");
	if (cJSON_IsString(database) && (database->valuestring != NULL))
	{
		config->database = strdup(database->valuestring);
	}
	else
	{
		error_ptr = "No database given in config!";
		goto error;
	}
	cJSON *user = cJSON_GetObjectItemCaseSensitive(json, "user");
	if (user && cJSON_IsString(user) && (user->valuestring != NULL))
	{
		config->database = strdup(user->valuestring);
	}
	cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
	if (password && cJSON_IsString(password) && (password->valuestring != NULL))
	{
		config->password = strdup(password->valuestring);
	}

	// Sender tags
	cJSON *sender_tags = cJSON_GetObjectItemCaseSensitive(json, "sender_tags");
	// sender_tags is optional
	if (sender_tags)
	{
		if (!cJSON_IsObject(sender_tags))
		{
			error_ptr = "Expected object, got something else for 'sender_tags'";
			goto error;
		}

		cJSON *sender = NULL;
		cJSON_ArrayForEach(sender, sender_tags)
		{
			uint32_t area, line, member;
			sscanf(sender->string, "%u.%u.%u", &area, &line, &member);

			address_t pa = {.pa = {line, area, member}};
			sender_tags_t *_sender_tag = calloc(1, sizeof(sender_tags_t));

			if (config->sender_tags[pa.value] == NULL)
			{
				config->sender_tags[pa.value] = _sender_tag;
			}
			else
			{
				sender_tags_t *entry = config->sender_tags[pa.value];
				while (entry->next != NULL)
					entry = entry->next;
				entry->next = _sender_tag;
			}

			if (!cJSON_IsArray(sender))
			{
				error_ptr = "Expected array as value, got something else for entry in 'sender_tags'";
				goto error;
			}

			_sender_tag->tags_len = cJSON_GetArraySize(sender);
			_sender_tag->tags = calloc(_sender_tag->tags_len, sizeof(char *));
			int i = 0;

			cJSON *tag = NULL;
			cJSON_ArrayForEach(tag, sender)
			{
				if (!cJSON_IsString(tag))
				{
					error_ptr = "Expected string for tag entry in 'sender_tags'";
					goto error;
				}
				if (tag->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a tag entry in 'sender_tags'";
					goto error;
				}

				_sender_tag->tags[i] = strdup(tag->valuestring);

				++i;

			}


		}
	}

	// Group addresses
	cJSON *gas = cJSON_GetObjectItemCaseSensitive(json, "gas");
	if (!cJSON_IsArray(gas))
	{
		error_ptr = "Expected array, got something else for 'gas'";
		goto error;
	}
	cJSON *ga_obj = NULL;

	cJSON_ArrayForEach(ga_obj, gas)
	{
		if (!cJSON_IsObject(ga_obj))
		{
			error_ptr = "Expected array of ojects, got something that is not object in 'gas'";
			goto error;
		}
		cJSON *ga = cJSON_GetObjectItemCaseSensitive(ga_obj, "ga");
		if (!cJSON_IsString(ga))
		{
			error_ptr = "'ga' is not a string!";
			goto error;
		}
		if (ga->valuestring == NULL)
		{
			error_ptr = "'ga' must not be empty!";
			goto error;
		}
		//printf("GA: %s", ga->valuestring);

		cJSON *series = cJSON_GetObjectItemCaseSensitive(ga_obj, "series");
		if (!cJSON_IsString(series))
		{
			error_ptr = "'series' is not a string!";
			goto error;
		}
		if (ga->valuestring == NULL)
		{
			error_ptr = "'series' must not be empty!";
			goto error;
		}
		//printf("Series: %s\n", series->valuestring);

		// Read out DPT
		cJSON *dpt = cJSON_GetObjectItemCaseSensitive(ga_obj, "dpt");
		if (!cJSON_IsNumber(dpt))
		{
			error_ptr = "'dpt' is not a number!";
			goto error;
		}

		// If DPT is 1, find out if we should convert to int
		cJSON *convert_to_int = cJSON_GetObjectItemCaseSensitive(ga_obj, "convert_to_int");
		uint8_t convert_dpt1_to_int = 0;
		if (convert_to_int != NULL)
		{
			if (!cJSON_IsBool(convert_to_int))
			{
				error_ptr = "'convert_to_int' is not a bool!";
				goto error;
			}

			convert_dpt1_to_int = convert_to_int->type == cJSON_True ? 1 : 0;
		}

		ga_t _ga = {};

		_ga.dpt = (uint8_t)dpt->valueint;
		_ga.convert_dpt1_to_int = convert_dpt1_to_int;
		_ga.series = strdup(series->valuestring);

		cJSON *ignored_senders = cJSON_GetObjectItemCaseSensitive(ga_obj, "ignored_senders");
		if (ignored_senders)
		{
			if (!cJSON_IsArray(ignored_senders))
			{
				error_ptr = "'ignored_senders' is not an array!";
				goto error;
			}
			_ga.ignored_senders_len = cJSON_GetArraySize(ignored_senders);
			_ga.ignored_senders = calloc(_ga.ignored_senders_len, sizeof(address_t));
			int i = 0;
			cJSON *ignored_sender = NULL;
			cJSON_ArrayForEach(ignored_sender, ignored_senders)
			{
				if (!cJSON_IsString(ignored_sender))
				{
					error_ptr = "Expected array of strings, got something that is not a string in 'ignored_senders'";
					goto error;
				}
				if (ignored_sender->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a physical address in 'ignored_senders'";
					goto error;
				}
				uint32_t area, line, member;
				sscanf(ignored_sender->valuestring, "%u.%u.%u", &area, &line, &member);
				_ga.ignored_senders[i].pa.area = area;
				_ga.ignored_senders[i].pa.line = line;
				_ga.ignored_senders[i].pa.member = member;
				++i;
			}
		}

		cJSON *tags = cJSON_GetObjectItemCaseSensitive(ga_obj, "tags");
		if (tags)
		{
			if (!cJSON_IsArray(tags))
			{
				error_ptr = "'tags' is not an array!";
				goto error;
			}
			_ga.tags_len = cJSON_GetArraySize(tags);
			_ga.tags = calloc(_ga.tags_len, sizeof(char *));
			int i = 0;
			cJSON *tag = NULL;
			cJSON_ArrayForEach(tag, tags)
			{
				if (!cJSON_IsString(tag))
				{
					error_ptr = "Expected array of string, got something that is not a string in 'tags'";
					goto error;
				}
				if (tag->valuestring == NULL)
				{
					error_ptr = "Got empty string instead of a key=value pair in 'tags'";
					goto error;
				}
				_ga.tags[i] = strdup(tag->valuestring);
				++i;
			}
		}
		else
		{
			_ga.tags_len = 0;
			_ga.tags = NULL;
		}

		address_t *addrs = parse_ga(ga->valuestring);

		address_t *ga_addr = addrs;
		while (ga_addr->value != NULL)
		{
			ga_t *entry = config->gas[ga_addr->value];

			ga_t *__ga = calloc(1, sizeof(ga_t));
			memcpy(__ga, &_ga, sizeof(ga_t));

			if (entry == NULL)
			{
				config->gas[ga_addr->value] = __ga;
			}
			else
			{
				while (entry->next != NULL)
				{
					entry = entry->next;
				}

				entry->next = __ga;
			}

			ga_addr++;
		}

		free(addrs);
	}

	goto end;

error:
	printf("JSON error: %s\n", error_ptr);
	status = -1;
end:
	cJSON_Delete(json);
	free(json_str);
	return status;
}
