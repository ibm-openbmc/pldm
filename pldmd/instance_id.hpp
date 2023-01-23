#pragma once

#include <bitset>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>

namespace pldm
{

constexpr size_t maxInstanceIds = 32;

/** @class InstanceId
 *  @brief Implementation of PLDM instance id as per DSP0240 v1.0.0
 */
class InstanceId
{
  public:
    /** @brief Get next unused instance id
     *  @return - PLDM instance id
     */
    uint8_t next();

    /** @brief Get the oldest instance id based on timestamp
     *  @return - Oldest PLDM instance id or nullopt
     */
    std::optional<uint8_t> returnOldestId();

    /** @brief Mark an instance id as unused
     *  @param[in] instanceId - PLDM instance id to be freed
     */
    void markFree(uint8_t instanceId)
    {
        id.set(instanceId, false);
        // Clear the time stamp associated with the instance id
        timestamp.erase(instanceId);
    }

  private:
    std::bitset<maxInstanceIds> id;
    std::map<uint8_t, std::chrono::time_point<std::chrono::system_clock>>
        timestamp;
};

} // namespace pldm
