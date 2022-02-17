#include "common/instance_id.hpp"
#include "common/utils.hpp"
#include "softoff.hpp"

#include <getopt.h>

#include <phosphor-logging/lg2.hpp>

PHOSPHOR_LOG2_USING;
int main(int argc, char* argv[])
{
    bool noTimeOut = false;
    static struct option long_options[] = {{"notimeout", no_argument, 0, 't'},
                                           {0, 0, 0, 0}};

    auto argflag = getopt_long(argc, argv, "t", long_options, nullptr);
    switch (argflag)
    {
        case 't':
            noTimeOut = true;
            info("Not applying any time outs");
            break;
        case -1:
            break;
        default:
            exit(EXIT_FAILURE);
    }

    // Get a default event loop
    auto event = sdeventplus::Event::get_default();

    // Get a handle to system D-Bus.
    auto& bus = pldm::utils::DBusHandler::getBus();

    // Obtain the instance database
    pldm::InstanceIdDb instanceIdDb;

    // Attach the bus to sd_event to service user requests
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

    pldm::SoftPowerOff softPower(bus, event.get(), instanceIdDb, noTimeOut);

    if (softPower.isError())
    {
        error(
            "Host failed to gracefully shutdown, exiting pldm-softpoweroff app");
        return -1;
    }

    if (softPower.isCompleted())
    {
        error(
            "Host current state is not Running, exiting pldm-softpoweroff app");
        return 0;
    }

    // Send the gracefully shutdown request to the host and
    // wait the host gracefully shutdown.
    if (softPower.hostSoftOff(event))
    {
        error(
            "pldm-softpoweroff:Failure in sending soft off request to the host. Exiting pldm-softpoweroff app");
        return -1;
    }

    if (softPower.isTimerExpired() && softPower.isReceiveResponse())
    {
        pldm::utils::reportError(
            "pldm soft off: Waiting for the host soft off timeout");

        auto method = bus.new_method_call(
            "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump/bmc",
            "xyz.openbmc_project.Dump.Create", "CreateDump");
        method.append(
            std::vector<
                std::pair<std::string, std::variant<std::string, uint64_t>>>());
        try
        {
            bus.call_noreply(method);
        }
        catch (const sdbusplus::exception::exception& e)
        {
            error("SoftPowerOff:Failed to create BMC dump, ERROR={ERR_EXCEP}",
                  "ERR_EXCEP", e.what());
        }

        error(
            "PLDM host soft off: ERROR! Wait for the host soft off timeout. Exit the pldm-softpoweroff");
        return -1;
    }

    return 0;
}
