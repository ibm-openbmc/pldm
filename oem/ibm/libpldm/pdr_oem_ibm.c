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

bool isHBRange(const uint32_t record_handle)
{
	if (record_handle >= 0x01000000 && record_handle < 0x01FFFFFF) {
		return true;
	}
	return false;
}

uint16_t pldm_find_container_id(const pldm_pdr *repo, uint16_t entityType,
				uint16_t entityInstance)
{
	assert(repo != NULL);

	pldm_pdr_record *record = repo->first;

	while (record != NULL) {
		struct pldm_pdr_hdr *hdr = (struct pldm_pdr_hdr *)record->data;
		if (hdr->type == PLDM_PDR_ENTITY_ASSOCIATION &&
		    !(isHBRange(record->record_handle))) {
			struct pldm_pdr_entity_association *pdr =
			    (struct pldm_pdr_entity_association
				 *)((uint8_t *)record->data +
				    sizeof(struct pldm_pdr_hdr));
			struct pldm_entity *child =
			    (struct pldm_entity *)(&pdr->children[0]);
			for (int i = 0; i < pdr->num_children; ++i) {
				if (pdr->container.entity_type == entityType &&
				    pdr->container.entity_instance_num ==
					entityInstance) {
					uint16_t id =
					    child->entity_container_id;
					return id;
				}
			}
		}
		record = record->next;
	}
	return 0;
}
