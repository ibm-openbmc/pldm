#ifndef PDR_OEM_IBM_H
#define PDR_OEM_IBM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pdr_data.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Find the last local record
 *
 *  @param[in] repo - opaque pointer acting as a PDR repo handle
 *
 *  @return opaque pointer to the PDR record,will be NULL if record was not
 * found
 */
pldm_pdr_record *pldm_pdr_find_last_local_record(const pldm_pdr *repo);

#ifdef __cplusplus
}
#endif

#endif /* PDR_OEM_IBM_H */
