#ifndef PDR_OEM_IBM_H
#define PDR_OEM_IBM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pdr_data.h"
#include "platform.h"
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

/** @brief method to check if the record handle is within the HostBoot range
 *  or not
 *
 *  @param[in] record_handle - record handle of the pdr
 */
bool isHBRange(const uint32_t record_handle);

/** @brief find the container ID of the contained entity
 *
 *  @param[in] repo - opaque pointer acting as a PDR repo handle
 *  @param[in] entityType - entity type
 *  @param[in] entityInstance - instance of the entity
 */
uint16_t pldm_find_container_id(const pldm_pdr *repo, uint16_t entityType,
				uint16_t entityInstance);
#ifdef __cplusplus
}
#endif

#endif /* PDR_OEM_IBM_H */
