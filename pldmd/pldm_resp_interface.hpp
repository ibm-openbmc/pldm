#pragma once

#include "common/flight_recorder.hpp"
#include "common/transport.hpp"
#include "common/utils.hpp"

#include <phosphor-logging/lg2.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <vector>
using namespace pldm;
using namespace pldm::utils;
using namespace pldm::flightrecorder;

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace response_api
{

/** @class AltResponse
 *
 *  @brief This class performs the necessary operation in pldm for
 *         responding remote pldm. This class is mostly designed in special case
 *         when pldm need to send reply to host after FILE IO operation
 *         completed.
 */
class AltResponse
{
  public:
    /** @brief Response constructor
     */
    AltResponse() = delete;
    AltResponse(const AltResponse&) = delete;
    AltResponse(PldmTransport* pldmTransport, pldm_tid_t& TID,
                bool verbose = false) :
        pldmTransport(pldmTransport),
        TID(TID), verbose(verbose)
    {}

    /** @brief method to send response to remote pldm using Response interface
     *  @param response - response of each request
     *  @returns returns 0 if success else error number
     */
    int sendPLDMRespMsg(auto response)
    {
        FlightRecorder::GetInstance().saveRecord(response, true);
        if (verbose)
        {
            printBuffer(Tx, response);
        }

        int rc =
            (*pldmTransport).sendMsg(TID, response.data(), response.size());
        if (rc < 0)
        {
            rc = errno;
            warning("sendto system call failed, errno= {ERRNO}", "ERRNO", rc);
        }
        return rc;
    }

  private:
    PldmTransport* pldmTransport; //!< PLDM transport object
    pldm_tid_t TID;               //!< PLDM hostEID reference
    bool verbose;
};

struct ResponseInterface
{
    std::unique_ptr<AltResponse> responseObj;
};

} // namespace response_api

} // namespace pldm
