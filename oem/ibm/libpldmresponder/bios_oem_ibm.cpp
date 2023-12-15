#include "bios_oem_ibm.hpp"

#include "host-bmc/dbus/serialize.hpp"

namespace pldm
{
namespace responder
{
namespace oem::ibm::bios
{

/** @brief Method to get the system type information
 *
 *  @return - the system type information
 */
std::optional<std::string>
    pldm::responder::oem::ibm::bios::Handler::getPlatformName()
{
    if (!systemType.empty())
    {
        return systemType;
    }

    static constexpr auto searchpath = "/xyz/openbmc_project/";
    int depth = 0;
    std::vector<std::string> ibmCompatible = {compatibleInterface};
    pldm::utils::GetSubTreeResponse response;
    try
    {
        response = pldm::utils::DBusHandler().getSubtree(searchpath, depth,
                                                         ibmCompatible);
    }
    catch (const sdbusplus::exception_t& e)
    {
        error(
            " getSubtree call failed with, ERROR={ERROR} PATH={PATH} INTERFACE={INTERFACE}",
            "ERROR", e.what(), "PATH", searchpath, "INTERFACE",
            ibmCompatible[0]);
        return std::nullopt;
    }

    for (const auto& [objectPath, serviceMap] : response)
    {
        try
        {
            auto value = pldm::utils::DBusHandler()
                             .getDbusProperty<std::vector<std::string>>(
                                 objectPath.c_str(), namesProperty,
                                 ibmCompatible[0].c_str());
            writeFile(value[0]);
            return value[0];
        }
        catch (const sdbusplus::exception_t& e)
        {
            error(
                " Error getting Names property, ERROR={ERROR} PATH={PATH} INTERFACE={INTERFACE}",
                "ERROR", e.what(), "PATH", searchpath, "INTERFACE",
                ibmCompatible[0]);
        }
    }

    return readFile();
}

/** @brief callback function invoked when interfaces get added from
 *      Entity manager
 *
 *  @param[in] msg - Data associated with subscribed signal
 */
void pldm::responder::oem::ibm::bios::Handler::ibmCompatibleAddedCallback(
    sdbusplus::message::message& msg)
{
    sdbusplus::message::object_path path;

    std::map<std::string,
             std::map<std::string,
                      std::variant<std::string, std::vector<std::string>>>>
        interfaceMap;

    msg.read(path, interfaceMap);

    if (!interfaceMap.contains(compatibleInterface))
    {
        return;
    }
    // Get the "Name" property value of the
    // "xyz.openbmc_project.Configuration.IBMCompatibleSystem" interface
    const auto& properties = interfaceMap.at(compatibleInterface);

    if (!properties.contains(namesProperty))
    {
        return;
    }
    auto names =
        std::get<pldm::utils::Interfaces>(properties.at(namesProperty));

    // get only the first system type
    if (!names.empty())
    {
        systemType = names.front();
        writeFile(systemType);
    }

    if (!systemType.empty())
    {
        ibmCompatibleMatchConfig.reset();
    }
}

void pldm::responder::oem::ibm::bios::Handler::writeFile(std::string systemType)
{
    info("SystemType written in the file : {SYSTEM_TYPE}", "SYSTEM_TYPE",
         systemType);
    pldm::serialize::Serialize::getSerialize().serializeKeyVal("SystemType",
                                                               systemType);
}

std::optional<std::string> pldm::responder::oem::ibm::bios::Handler::readFile()
{
    std::map<std::string, pldm::dbus::PropertyValue> persistedData =
        pldm::serialize::Serialize::getSerialize().getSavedKeyVals();

    if (persistedData.contains("SystemType"))
    {
        info("SystemType read from the file: {SYSTEM_TYPE}", "SYSTEM_TYPE",
             std::get<std::string>(persistedData["SystemType"]));
        return std::get<std::string>(persistedData["SystemType"]);
    }
    error("Error in reading SystemType from the file");
    return std::nullopt;
}

} // namespace oem::ibm::bios
} // namespace responder
} // namespace pldm
