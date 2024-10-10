/**
 * @file rewrite_nit.c
 * @brief Patches NIT frequencies
 * in order to know which programs are actually being broadcast, we need to reference an external
 * configuration file. This config needs to be provided by the user in the format defined as follows:
 * for each frequency to be kept:
 * rewrite_nit=1
 * freq_patch=0x03300000->0x07700000
 * freq_patch=0x04500000->0x07780000
 * freq_patch=0x04660000->0x07860000 # TSID 10000
 * freq_patch=0x05620000->0x07940000 # TSID 10017
 *
 * frequency entries without corresponding new mapping are discarded along with their entries
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "autoconf.h"
#include "crc32.h"
#include "errors.h"
#include "log.h"
#include "mumudvb.h"
#include "rewrite.h"
#include "ts.h" /* nit_t*/

static char *log_module = "NIT rewrite: ";


typedef enum {
	QAM_NOT_DEFINED, /* 0x0 */
	QAM16,           /* 0x01 */
	QAM32,           /* 0x02 */
	QAM64,           /* 0x03 */
	QAM128,          /* 0x04 */
	QAM256,          /* 0x05 */
	QAM_RESERVED     /* for future use: 0x06 to 0xFF */
} modrate_t;


typedef struct nit_freq_map {
	uint32_t oldfreq;     /* in MHz, BCD coded */
	uint32_t newfreq;     /* in MHz, BCD coded */
	modrate_t modulation; /* QAM modulation */
	uint32_t symbrate;    /* in kSymb/sec */
} nit_freq_map_t;


nit_freq_map_t *freq_map = NULL;
size_t frequency_cnt = 0;


/**
 * @brief Read a line of the configuration file to check if there is a rewrite nit parameter
 * @details Sets the frequency map for the rewrite nit module
 * @param substring The current line
 * @return -1 on error, 0 on success
 */
int read_rewrite_nit_config(const char *substring)
{
	if (!strcmp(substring, "freq_patch")) {
		substring = strtok(NULL, "=");
		if (substring == NULL) {
			return -1;
		}
		uint32_t old_freq = 0;
		uint32_t new_freq = 0;
		char *endptr = NULL;
		errno = 0;
		old_freq = strtoul(substring, &endptr, 16);
		if (errno != 0) {
			log_message(log_module, MSG_ERROR, "Invalid config: %s", strerror(errno));
			return -1;
		}
		if (*endptr != '-' || *(endptr+1) != '>') {
			return -1;
		}
		endptr += 2;

		errno = 0;
		new_freq = strtoul(endptr, &endptr, 16);
		if (errno != 0) {
			log_message(log_module, MSG_ERROR, "Invalid config: %s", strerror(errno));
			return -1;
		}

		if (old_freq == 0 || new_freq == 0) {
			log_message(log_module, MSG_ERROR, "Missing config frequency");
			return -1;
		}

		frequency_cnt++;
		freq_map = reallocarray(freq_map, frequency_cnt, sizeof(nit_freq_map_t));
		if (freq_map == NULL) {
			return -1;
		}
		freq_map[frequency_cnt - 1].oldfreq = old_freq;
		freq_map[frequency_cnt - 1].newfreq = new_freq;
		log_message(log_module, MSG_INFO, "Added frequency patch: old: 0x%08x new: 0x%08x", old_freq, new_freq);

	} else {
		return 0;
	}
	return 1;
}

ts_header_t nit_ts_header =  {
	.sync_byte = TS_SYNC_BYTE,
	.transport_error_indicator = 0,
	.payload_unit_start_indicator = 0,
	.transport_priority = 0,
	.pid_hi = 0x0010 >> 8,
	.pid_lo = 0x0010,
	.transport_scrambling_control = 0,
	.adaptation_field_control = 0b01
};

#define NIT_TS_HEADER_LEN 4

const ts_header_t NULL_TS_HEADER = {
	.sync_byte = TS_SYNC_BYTE,
	.transport_error_indicator = 0,
	.payload_unit_start_indicator = 0,
	.transport_priority = 0,
	.pid_hi = 0x1FFF >> 8,
	.pid_lo = 0x1FFFF & 0x00FF,
	.transport_scrambling_control = 0,
	.adaptation_field_control = 0b01
};

/**
 * @brief Rewrites frequencies according to @c freq_map
 * @param cable_descriptor @c cable_delivery_system_descriptor that should be rewritten
 * @return 1 on rewrite
 * @return 0 if freq was not rewritten
 */
int rewrite_cable_delivery_system_descriptor(descr_cable_delivery_t *cable_descriptor)
{
	uint32_t freq = (cable_descriptor->frequency_4 << 24) + (cable_descriptor->frequency_3 << 16)
	                + (cable_descriptor->frequency_2 << 8) + cable_descriptor->frequency_1;

	for (size_t i = 0; i < frequency_cnt; ++i) {
		if (freq_map[i].oldfreq == freq) {
			cable_descriptor->frequency_4 = freq_map[i].newfreq >> 24;
			cable_descriptor->frequency_3 = freq_map[i].newfreq >> 16;
			cable_descriptor->frequency_2 = freq_map[i].newfreq >> 8;
			cable_descriptor->frequency_1 = freq_map[i].newfreq & 0xFF;
			log_message(log_module, MSG_DEBUG, "Patched freq %x with new freq %x", freq,
			            freq_map[i].newfreq);
			return 1;
		}
	}
	return 0;
}

/**
 * @brief Iterates through all transport streams and rewrites the frequencies
 * @note only @c cable_delivery_system_descriptor are patched for now
 * @param ts_stream pointer to the first transport stream
 * @param stream_loop_len @c transport_stream_loop_length
 * @return the number of removed transport streams in bytes
 */
size_t rewrite_transport_stream(unsigned char *ts_stream, size_t stream_loop_len)
{
	if (stream_loop_len == 0) {
		log_message(log_module, MSG_FLOOD, "- transport stream loop len is 0");
		return 0;
	}
	size_t removed_bytes = 0;

	while (stream_loop_len > 0) {
		int descriptors_loop_length = HILO(((nit_ts_t *)ts_stream)->transport_descriptors_length);

		size_t stream_length = NIT_TS_LEN + descriptors_loop_length;

		if (descriptors_loop_length == 0) break;
		descr_cable_delivery_t *descriptor = (descr_cable_delivery_t *)(ts_stream + NIT_TS_LEN);

		bool remove_stream = 0;
		while (descriptors_loop_length > 0) {
			switch (descriptor->descriptor_tag) {
			case 0x44:
				remove_stream = rewrite_cable_delivery_system_descriptor(descriptor) == 0;
				break;
			default:
				log_message(log_module, MSG_FLOOD,
				            "- did not rewrite transport stream descriptor with tag: 0x%x",
				            descriptor->descriptor_tag);
				break;
			}
			// descriptor length is data length + length(tag + len)
			descriptor += descriptor->descriptor_length + 2;
			descriptors_loop_length -= (descriptor->descriptor_length + 2);
		}
		stream_loop_len -= stream_length;
		size_t rest_len = stream_loop_len + 4; // rest of loop + CRC

		if (remove_stream) {
			log_message(log_module, MSG_DEBUG, "Removed redundant stream from NIT");
			memmove(ts_stream, ts_stream + stream_length, rest_len);
			removed_bytes += stream_length;
		} else {
			ts_stream += stream_length;
		}
	}
	return removed_bytes;
}

/**
 * @brief Rewrites frequencies in a nit section according to @c freq_map
 * @note only @c cable_delivery_system_descriptor are patched for now
 * @param full_nit_section pointer to the start of a nit section
 */
void rewrite_nit_section(unsigned char *full_nit_section)
{
	nit_t *nit = (nit_t *)full_nit_section;
	nit_mid_t *nit_mid = (nit_mid_t *)(full_nit_section + NIT_LEN + HILO(nit->network_descriptor_length));

	size_t removed_bytes = rewrite_transport_stream((unsigned char *)nit_mid + SIZE_NIT_MID,
	                         HILO(nit_mid->transport_stream_loop_length));
	// adjust transport stream loop length and section length
	size_t new_ts_loop_length = HILO(nit_mid->transport_stream_loop_length) - removed_bytes;
	nit_mid->transport_stream_loop_length_hi = (new_ts_loop_length >> 8) & 0x0f;
	nit_mid->transport_stream_loop_length_lo = new_ts_loop_length & 0xff;

	size_t new_section_length = HILO(nit->section_length) - removed_bytes;
	nit->section_length_hi = (new_section_length >> 8) & 0x0f;
	nit->section_length_lo = new_section_length & 0xff;

	ts_display_nit(log_module, (unsigned char *)nit);

	setCRC32(full_nit_section);
}


void nit_rewrite_new_global_packet(unsigned char *ts_packet, rewrite_parameters_t *rewrite_vars) { return; }