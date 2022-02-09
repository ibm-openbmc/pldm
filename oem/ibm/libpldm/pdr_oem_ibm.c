#include "pdr_oem_ibm.h"
#include "pdr.h"

pldm_pdr_record *pldm_pdr_find_last_local_record(const pldm_pdr *repo)
{
	assert(repo != NULL);
	pldm_pdr_record *curr = repo->first;
	pldm_pdr_record *prev = repo->first;

	while (curr != NULL) {
		if (curr->record_handle > 0x00000000 &&
		    curr->record_handle < 0x00FFFFFF &&
		    curr->next->record_handle > 0x00FFFFFF) {
			return curr;
		}
		prev = curr;
		curr = curr->next;
	}
	if (curr == NULL) {
		return prev;
	}
	return NULL;
}
