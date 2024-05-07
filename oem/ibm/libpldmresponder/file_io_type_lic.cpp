#include "file_io_type_lic.hpp"

#include "libpldm/base.h"
#include "libpldm/oem/ibm/file_io.h"

#include "common/utils.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cstdint>
#include <iostream>

PHOSPHOR_LOG2_USING;

namespace pldm
{
using namespace pldm::responder::utils;
using namespace pldm::utils;
using Json = nlohmann::json;
const Json emptyJson{};

namespace responder
{
static constexpr auto codFilePath = "/var/lib/ibm/cod/";
static constexpr auto licFilePath = "/var/lib/pldm/license/";
constexpr auto newLicenseFile = "new_license.bin";
constexpr auto newLicenseJsonFile = "new_license.json";

int LicenseHandler::updateBinFileAndLicObjs(const fs::path& newLicJsonFilePath)
{
    int rc = PLDM_SUCCESS;
    fs::path newLicFilePath(fs::path(licFilePath) / newLicenseFile);
    std::ifstream jsonFileNew(newLicJsonFilePath);

    auto dataNew = Json::parse(jsonFileNew, nullptr, false);
    if (dataNew.is_discarded())
    {
        error("Failed to parse the new license json file '{NEW_LIC_JSON}'",
              "NEW_LIC_JSON", newLicJsonFilePath);
        throw InternalFailure();
    }

    // Store the json data in a file with binary format
    convertJsonToBinaryFile(dataNew, newLicFilePath);

    // Create or update the license objects
    rc = createOrUpdateLicenseObjs();
    if (rc != PLDM_SUCCESS)
    {
        error("Failed to create or update license objs with rc as {RC}", "RC",
              rc);
        return rc;
    }
    return PLDM_SUCCESS;
}

void LicenseHandler::writeFromMemory(
    uint32_t offset, uint32_t length, uint64_t address,
    oem_platform::Handler* /*oemPlatformHandler*/,
    SharedAIORespData& sharedAIORespDataobj, sdeventplus::Event& event)
{
    namespace fs = std::filesystem;
    if (!fs::exists(licFilePath))
    {
        fs::create_directories(licFilePath);
        fs::permissions(licFilePath,
                        fs::perms::others_read | fs::perms::owner_write);
    }
    fs::path newLicJsonFilePath(fs::path(licFilePath) / newLicenseJsonFile);
    std::ofstream licJsonFile(newLicJsonFilePath,
                              std::ios::out | std::ios::binary);
    if (!licJsonFile)
    {
        error(
            "Failed to create license json file '{NEW_LIC_JSON}' while write from memory",
            "NEW_LIC_JSON", newLicJsonFilePath);
        FileHandler::dmaResponseToRemoteTerminus(sharedAIORespDataobj,
                                                 PLDM_ERROR, 0);
        FileHandler::deleteAIOobjects(nullptr, sharedAIORespDataobj);
        return;
    }

    transferFileData(newLicJsonFilePath, false, offset, length, address,
                     sharedAIORespDataobj, event);
}

void LicenseHandler::postDataTransferCallBack(bool IsWriteToMemOp,
                                              uint32_t length)
{
    if (IsWriteToMemOp)
    {
        fs::path newLicJsonFilePath(fs::path(licFilePath) / newLicenseJsonFile);

        if (length == licLength)
        {
            int rc = updateBinFileAndLicObjs(newLicJsonFilePath);
            if (rc != PLDM_SUCCESS)
            {
                error(
                    "Failed to update bin file and license objs with rc as {RC} while post data transfer callback",
                    "RC", rc);
                return;
            }
        }
    }
}

int LicenseHandler::write(const char* buffer, uint32_t /*offset*/,
                          uint32_t& length,
                          oem_platform::Handler* /*oemPlatformHandler*/)
{
    int rc = PLDM_SUCCESS;

    fs::path newLicJsonFilePath(fs::path(licFilePath) / newLicenseJsonFile);
    std::ofstream licJsonFile(newLicJsonFilePath,
                              std::ios::out | std::ios::binary | std::ios::app);
    if (!licJsonFile)
    {
        error(
            "Failed to create license json file '{NEW_LIC_JSON}' while in write",
            "NEW_LIC_JSON", newLicJsonFilePath);
        return -1;
    }

    if (buffer)
    {
        licJsonFile.write(buffer, length);
    }
    licJsonFile.close();

    updateBinFileAndLicObjs(newLicJsonFilePath);
    if (rc != PLDM_SUCCESS)
    {
        error("Failed to update bin file and license objs with rc as {RC}",
              "RC", rc);
        return rc;
    }

    return rc;
}

int LicenseHandler::newFileAvailable(uint64_t length)
{
    licLength = length;
    return PLDM_SUCCESS;
}

int LicenseHandler::read(uint32_t offset, uint32_t& length, Response& response,
                         oem_platform::Handler* /*oemPlatformHandler*/)
{
    std::string filePath = codFilePath;
    filePath += "licFile";

    if (licType != PLDM_FILE_TYPE_COD_LICENSE_KEY)
    {
        return PLDM_ERROR_INVALID_DATA;
    }
    auto rc = readFile(filePath.c_str(), offset, length, response);
    fs::remove(filePath);
    if (rc)
    {
        return PLDM_ERROR;
    }
    return PLDM_SUCCESS;
}

int LicenseHandler::fileAckWithMetaData(uint8_t /*fileStatus*/,
                                        uint32_t metaDataValue1,
                                        uint32_t /*metaDataValue2*/,
                                        uint32_t /*metaDataValue3*/,
                                        uint32_t /*metaDataValue4*/)
{
    DBusMapping dbusMapping;
    dbusMapping.objectPath = "/com/ibm/license";
    dbusMapping.interface = "com.ibm.License.LicenseManager";
    dbusMapping.propertyName = "LicenseActivationStatus";
    dbusMapping.propertyType = "string";

    pldm_license_install_status status =
        static_cast<pldm_license_install_status>(metaDataValue1);

    if (status == pldm_license_install_status::PLDM_LIC_INVALID_LICENSE)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.InvalidLicense";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set invalid license status due to the ERROR:{ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else if (status == pldm_license_install_status::PLDM_LIC_ACTIVATED)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.Activated";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set license activated status due to the ERROR:{ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else if (status == pldm_license_install_status::PLDM_LIC_PENDING)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.Pending";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set license pending status due to the ERROR:{ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else if (status == pldm_license_install_status::PLDM_LIC_ACTIVATION_FAILED)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.ActivationFailed";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set license activation failure status due to the ERROR:{ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else if (status == pldm_license_install_status::PLDM_LIC_INCORRECT_SYSTEM)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.IncorrectSystem";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set incorrect system status to license manager due to the ERROR={ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else if (status == pldm_license_install_status::PLDM_LIC_INVALID_HOSTSTATE)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.InvalidHostState";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set invalid host state status to license manager due to the ERROR={ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    else if (status == pldm_license_install_status::PLDM_LIC_INCORRECT_SEQUENCE)
    {
        pldm::utils::PropertyValue value =
            "com.ibm.License.LicenseManager.Status.IncorrectSequence";

        try
        {
            pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
        }
        catch (const std::exception& e)
        {
            error(
                "Failed to set incorrect sequence status to license manager due to the ERROR={ERR_EXCEP}",
                "ERR_EXCEP", e);
            return PLDM_ERROR;
        }
    }
    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
