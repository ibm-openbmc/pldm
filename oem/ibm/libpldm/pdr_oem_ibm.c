#include "pdr_oem_ibm.h"
#include "pdr.h"

#include <stdio.h>

pldm_pdr_record *pldm_pdr_find_last_local_record(const pldm_pdr *repo)
{
	assert(repo != NULL);
	pldm_pdr_record *curr = repo->first;
	pldm_pdr_record *prev = repo->first;
	pldm_pdr_record *record = curr;
	uint32_t recent_record_handle = curr->record_handle;

	while (curr != NULL) {
		if ((curr->record_handle > 0x00000000) &&
		    (curr->record_handle < 0x00FFFFFF)) {
			if (recent_record_handle < curr->record_handle) {
				recent_record_handle = curr->record_handle;
				record = curr;
			}
		}
		prev = curr;
		curr = curr->next;
	}
	if (curr == NULL && prev != NULL) {
		return record;
	}
	return NULL;
}
