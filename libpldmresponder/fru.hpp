#pragma once

#include "config.h"
#include "handler.hpp"

#include <vector>

#include "libpldm/fru.h"

namespace pldm
{
namespace responder
{
namespace fru
{

class Handler : public CmdHandler
{
  public:
    Handler()
    {
        handlers.emplace(PLDM_GET_FRU_RECORD_TABLE_METADATA,
                         [this](const pldm_msg* request, size_t payloadLength) {
                             return this->getFRURecordTableMetadata(request, payloadLength);
                         });
    }

    /** @brief Handler for GetFRURecordTableMetadata
     *
     *  @param[in] request - Request message payload
     *  @param[in] payloadLength - Request payload length
     *  @param[out] Response - Response message written here
     */
    Response getFRURecordTableMetadata(const pldm_msg* request, size_t payloadLength);
};

} // namespace fru
} // namespace responder
} // namespace pldm
