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







void nit_rewrite_new_global_packet(unsigned char *ts_packet, rewrite_parameters_t *rewrite_vars) { return; }