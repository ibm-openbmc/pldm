#ifndef PDR_DATA_H
#define PDR_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct pldm_pdr_record {
	uint32_t record_handle;
	uint32_t size;
	uint8_t *data;
	struct pldm_pdr_record *next;
	bool is_remote;
	uint16_t terminus_handle;
} pldm_pdr_record;

typedef struct pldm_pdr {
	uint32_t record_count;
	uint32_t size;
	pldm_pdr_record *first;
	pldm_pdr_record *last;
} pldm_pdr;

/** @struct pldm_pdr
 *  *  opaque structure that acts as a handle to a PDR repository
 *   */
typedef struct pldm_pdr pldm_pdr;

/** @struct pldm_pdr_record
 *  *  opaque structure that acts as a handle to a PDR record
 *   */
typedef struct pldm_pdr_record pldm_pdr_record;
#ifdef __cplusplus
}
#endif

#endif /* PDR_DATA_H */
