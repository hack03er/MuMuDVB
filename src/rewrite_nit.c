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


void nit_rewrite_new_global_packet(unsigned char *ts_packet, rewrite_parameters_t *rewrite_vars) { return; }