#include "instance_id.hpp"

#include <stdexcept>

namespace pldm
{

uint8_t InstanceId::next()
{
    uint8_t idx = 0;
    while (idx < id.size() && id.test(idx))
    {
        ++idx;
    }

    if (idx == id.size())
    {
        // check all the instance ids and free up the one
        // that is acquired oldest
        std::cerr << "all the Instance ids are exhausted \n";

        auto instance = returnOldestId();
        if (instance.has_value())
        {
            idx = instance.value();
            // forcefully release the instance id
            markFree(idx);
        }
        else
        {
            throw std::runtime_error(
                "Instance Id older than instance id expiration time could not be found");
        }
    }

    id.set(idx);
    timestamp[idx] = std::chrono::system_clock::now();
    return idx;
}

std::optional<uint8_t> InstanceId::returnOldestId()
{
    uint8_t idx = 0;
    bool skipInstance = true;
    auto now = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsedtime{};

    for (const auto& [instanceId, timepoint] : timestamp)
    {
        std::chrono::duration<double> instanceTime = now - timepoint;
        if (instanceTime >
            std::chrono::seconds(INSTANCE_ID_EXPIRATION_INTERVAL))
        {
            skipInstance = false;
        }
        if (instanceTime > elapsedtime)
        {
            elapsedtime = instanceTime;
            idx = instanceId;
        }
    }
    if (skipInstance)
    {
        std::cerr
            << "None of the instance id's are older then the pldm instance id expiration time\n";
        return std::nullopt;
    }
    const std::time_t t_c =
        std::chrono::system_clock::to_time_t(timestamp[idx]);
    std::cerr << "Forcefully releasing Instance ID :" << (unsigned)idx
              << " Last used at timestamp : "
              << std::put_time(std::localtime(&t_c), "%F %T\n") << std::flush;
    return idx;
}

} // namespace pldm
