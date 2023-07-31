#include "inband_code_update.hpp"

#include "libpldm/entity.h"

#include "libpldmresponder/pdr.hpp"
#include "oem_ibm_handler.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <arpa/inet.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Dump/NewDump/server.hpp>

#include <exception>
#include <fstream>

PHOSPHOR_LOG2_USING;

namespace pldm
{
using namespace utils;

namespace responder
{
using namespace oem_ibm_platform;

/** @brief Directory where the lid files without a header are stored */
auto lidDirPath = fs::path(LID_STAGING_DIR) / "lid";

/** @brief Directory where the image files are stored as they are built */
auto imageDirPath = fs::path(LID_STAGING_DIR) / "image";

/** @brief Directory where the code update tarball files are stored */
auto updateDirPath = fs::path(LID_STAGING_DIR) / "update";

/** @brief The file name of the code update tarball */
constexpr auto tarImageName = "image.tar";

/** @brief The path to the code update tarball file */
auto tarImagePath = fs::path(imageDirPath) / tarImageName;

/** @brief The filename of the file where bootside data will be saved */
constexpr auto bootSideFileName = "bootSide";

constexpr auto bootSideAttrName = "fw_boot_side_current";
constexpr auto bootNextSideAttrName = "fw_boot_side";

/** @brief The filepath of file where bootside data will be saved */
auto bootSideDirPath = fs::path("/var/lib/pldm/") / bootSideFileName;

std::string CodeUpdate::fetchCurrentBootSide()
{
    return currBootSide;
}

std::string CodeUpdate::fetchNextBootSide()
{
    return nextBootSide;
}

int CodeUpdate::setCurrentBootSide(const std::string& currSide)
{
    currBootSide = currSide;
    return PLDM_SUCCESS;
}

int CodeUpdate::setNextBootSide(const std::string& nextSide)
{
    info("setNextBootSide, nextSide={NXT_SIDE}", "NXT_SIDE", nextSide);
    pldm_boot_side_data pldmBootSideData = readBootSideFile();
    currBootSide = (pldmBootSideData.current_boot_side == "Perm" ? Pside
                                                                 : Tside);
    nextBootSide = nextSide;
    pldmBootSideData.next_boot_side = (nextSide == Pside ? "Perm" : "Temp");
    std::string objPath{};
    if (nextBootSide == currBootSide)
    {
        info("Current bootside is same as next boot side");
        info("setting priority of running version 0");
        objPath = runningVersion;
    }
    else
    {
        info("Current bootside is not same as next boot side");
        info("setting priority of non running version 0");
        objPath = nonRunningVersion;
    }
    if (objPath.empty())
    {
        error("no nonRunningVersion present");
        return PLDM_PLATFORM_INVALID_STATE_VALUE;
    }

    try
    {
        auto priorityPropValue = dBusIntf->getDbusPropertyVariant(
            objPath.c_str(), "Priority", redundancyIntf);
        const auto& priorityValue = std::get<uint8_t>(priorityPropValue);
        if (priorityValue == 0)
        {
            // Requested next boot side is already set
            return PLDM_SUCCESS;
        }
    }
    catch (const std::exception& e)
    {
        // Alternate side may not be present due to a failed code update
        return PLDM_PLATFORM_INVALID_STATE_VALUE;
    }

    pldm::utils::DBusMapping dbusMapping{objPath, redundancyIntf, "Priority",
                                         "uint8_t"};
    uint8_t val = 0;
    pldm::utils::PropertyValue value = static_cast<uint8_t>(val);
    try
    {
        dBusIntf->setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        error(
            "failed to set the next boot side to {OBJ_PATH} ERROR={ERR_EXCEP}",
            "OBJ_PATH", objPath.c_str(), "ERR_EXCEP", e.what());
        return PLDM_ERROR;
    }
    writeBootSideFile(pldmBootSideData);
    return PLDM_SUCCESS;
}

int CodeUpdate::setRequestedApplyTime()
{
    int rc = PLDM_SUCCESS;
    pldm::utils::PropertyValue value =
        "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.OnReset";
    DBusMapping dbusMapping;
    dbusMapping.objectPath = "/xyz/openbmc_project/software/apply_time";
    dbusMapping.interface = "xyz.openbmc_project.Software.ApplyTime";
    dbusMapping.propertyName = "RequestedApplyTime";
    dbusMapping.propertyType = "string";
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        error("Failed To set RequestedApplyTime property ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        rc = PLDM_ERROR;
    }
    return rc;
}

int CodeUpdate::setRequestedActivation()
{
    int rc = PLDM_SUCCESS;
    pldm::utils::PropertyValue value =
        "xyz.openbmc_project.Software.Activation.RequestedActivations.Active";
    DBusMapping dbusMapping;
    dbusMapping.objectPath = newImageId;
    dbusMapping.interface = "xyz.openbmc_project.Software.Activation";
    dbusMapping.propertyName = "RequestedActivation";
    dbusMapping.propertyType = "string";
    try
    {
        pldm::utils::DBusHandler().setDbusProperty(dbusMapping, value);
    }
    catch (const std::exception& e)
    {
        error("Failed To set RequestedActivation property ERROR={ERR_EXCEP}",
              "ERR_EXCEP", e.what());
        rc = PLDM_ERROR;
    }
    return rc;
}

void CodeUpdate::setVersions()
{
    BiosAttributeList biosAttrList;
    static constexpr auto mapperService = "xyz.openbmc_project.ObjectMapper";
    static constexpr auto functionalObjPath =
        "/xyz/openbmc_project/software/functional";
    static constexpr auto activeObjPath =
        "/xyz/openbmc_project/software/active";
    static constexpr auto propIntf = "org.freedesktop.DBus.Properties";
    static constexpr auto pathIntf = "xyz.openbmc_project.Common.FilePath";

    auto& bus = dBusIntf->getBus();
    try
    {
        auto method = bus.new_method_call(mapperService, functionalObjPath,
                                          propIntf, "Get");
        method.append("xyz.openbmc_project.Association", "endpoints");
        std::variant<std::vector<std::string>> paths;

        auto reply = bus.call(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
        reply.read(paths);

        runningVersion = std::get<std::vector<std::string>>(paths)[0];
        auto runningPathPropValue = dBusIntf->getDbusPropertyVariant(
            runningVersion.c_str(), "Path", pathIntf);
        const auto& runningPath = std::get<std::string>(runningPathPropValue);

        auto method1 = bus.new_method_call(mapperService, activeObjPath,
                                           propIntf, "Get");
        method1.append("xyz.openbmc_project.Association", "endpoints");

        auto reply1 = bus.call(
            method1,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
        reply1.read(paths);
        for (const auto& path : std::get<std::vector<std::string>>(paths))
        {
            if (path != runningVersion)
            {
                nonRunningVersion = path;
                break;
            }
        }

        if (!fs::exists(bootSideDirPath))
        {
            pldm_boot_side_data pldmBootSideData;
            std::string nextBootSideBiosValue =
                getBiosAttrValue("fw_boot_side");
            pldmBootSideData.current_boot_side = nextBootSideBiosValue;
            pldmBootSideData.next_boot_side = nextBootSideBiosValue;
            pldmBootSideData.running_version_object = runningPath;

            writeBootSideFile(pldmBootSideData);
            biosAttrList.push_back(std::make_pair(
                bootSideAttrName, pldmBootSideData.current_boot_side));
            biosAttrList.push_back(std::make_pair(
                bootNextSideAttrName, pldmBootSideData.next_boot_side));
            setBiosAttr(biosAttrList);
        }
        else
        {
            pldm_boot_side_data pldmBootSideData = readBootSideFile();
            std::string runningVers;
            runningVers = pldmBootSideData.running_version_object;
            if (pldmBootSideData.running_version_object != runningPath)
            {
                info(
                    "BMC have booted with the new image runningPath={RUNN_PATH}",
                    "RUNN_PATH", runningPath.c_str());
                info("Previous Image was: {RUNN_VERS}", "RUNN_VERS",
                     runningVers);
                auto current_boot_side =
                    (pldmBootSideData.current_boot_side == "Temp" ? "Perm"
                                                                  : "Temp");
                pldmBootSideData.current_boot_side = current_boot_side;
                pldmBootSideData.next_boot_side = current_boot_side;
                pldmBootSideData.running_version_object = runningPath;
                writeBootSideFile(pldmBootSideData);
                biosAttrList.push_back(
                    std::make_pair(bootSideAttrName, current_boot_side));
                biosAttrList.push_back(std::make_pair(
                    bootNextSideAttrName, pldmBootSideData.next_boot_side));
                setBiosAttr(biosAttrList);
            }
            else
            {
                info(
                    "BMC have booted with the previous image runningPath={RUNN_PATH}",
                    "RUNN_PATH", runningVers);
                pldm_boot_side_data pldmBootSideData = readBootSideFile();
                pldmBootSideData.next_boot_side =
                    pldmBootSideData.current_boot_side;
                writeBootSideFile(pldmBootSideData);
                biosAttrList.push_back(std::make_pair(
                    bootSideAttrName, pldmBootSideData.current_boot_side));
                biosAttrList.push_back(std::make_pair(
                    bootNextSideAttrName, pldmBootSideData.next_boot_side));
                setBiosAttr(biosAttrList);
            }
            currBootSide =
                (pldmBootSideData.current_boot_side == "Temp" ? Tside : Pside);
            nextBootSide = (pldmBootSideData.next_boot_side == "Temp" ? Tside
                                                                      : Pside);
        }
    }
    catch (const std::exception& e)
    {
        error(
            "failed to make a d-bus call to Object Mapper Association, ERROR={ERR_EXCEP}",
            "ERR_EXCEP", e.what());
        if (retrySetVersion < maxVersionRetry)
        {
            retrySetVersion++;
            usleep(500 * 1000);
            setVersions();
        }
        else
        {
            throw std::runtime_error("Failed to fetch Update Software Object");
        }
        return;
    }

    using namespace sdbusplus::bus::match::rules;
    captureNextBootSideChange.push_back(
        std::make_unique<sdbusplus::bus::match::match>(
            pldm::utils::DBusHandler::getBus(),
            propertiesChanged(runningVersion, redundancyIntf),
            [this](sdbusplus::message::message& msg) {
        DbusChangedProps props;
        std::string iface;
        msg.read(iface, props);
        processPriorityChangeNotification(props);
            }));
    fwUpdateMatcher.push_back(std::make_unique<sdbusplus::bus::match::match>(
        pldm::utils::DBusHandler::getBus(),
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',path='/xyz/openbmc_project/software'",
        [this](sdbusplus::message::message& msg) {
        DBusInterfaceAdded interfaces;
        sdbusplus::message::object_path path;
        msg.read(path, interfaces);

        for (auto& interface : interfaces)
        {
            if (interface.first == "xyz.openbmc_project.Software.Activation")
            {
                auto imageInterface = "xyz.openbmc_project.Software.Activation";
                auto imageObjPath = path.str.c_str();

                // If the version interface is added with the Activation
                // value as Invalid, it is considered as assemble code
                // update image failure.
                // Failure is occured while assembling
                // lid files or while creating a tar ball.

                try
                {
                    auto propVal =
                        pldm::utils::DBusHandler().getDbusPropertyVariant(
                            imageObjPath, "Activation", imageInterface);
                    const auto& activation = std::get<std::string>(propVal);

                    if (activation ==
                        "xyz.openbmc_project.Software.Activation.Activations.Invalid")
                    {
                        if (isCodeUpdateInProgress())
                        {
                            error(
                                "InbandCodeUpdate Failed: Received Invalid Signal, Sending Error on End update sensor event to PHYP");
                            setCodeUpdateProgress(false);
                            auto sensorId = getFirmwareUpdateSensor();
                            sendStateSensorEvent(
                                sensorId, PLDM_STATE_SENSOR_STATE, 0,
                                uint8_t(CodeUpdateState::FAIL),
                                uint8_t(CodeUpdateState::START));
                            break;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    error(
                        "Code Update: Error getting Activation property, PATH={PATH} INTERFACE={INTF} PROPERTY = Activation ERROR={ERR_EXCEP}",
                        "PATH", imageObjPath, "INTF", imageInterface,
                        "ERR_EXCEP", e.what());
                }
                try
                {
                    nonRunningVersion = path.str;

                    if (isCodeUpdateInProgress())
                    {
                        info("Inband Code update is InProgress");
                        // Inband update
                        newImageId = path.str;
                        if (!imageActivationMatch)
                        {
                            imageActivationMatch =
                                std::make_unique<sdbusplus::bus::match::match>(
                                    pldm::utils::DBusHandler::getBus(),
                                    propertiesChanged(newImageId,
                                                      "xyz.openbmc_project."
                                                      "Software.Activation"),
                                    [this](sdbusplus::message::message& msg) {
                                DbusChangedProps props;
                                std::string iface;
                                msg.read(iface, props);
                                const auto itr = props.find("Activation");
                                if (itr != props.end())
                                {
                                    PropertyValue value = itr->second;
                                    auto propVal = std::get<std::string>(value);
                                    if (propVal ==
                                        "xyz.openbmc_project.Software."
                                        "Activation.Activations.Active")
                                    {
                                        info(
                                            "Received Active signal, Sending success on End update sensor event to PHYP");
                                        CodeUpdateState state =
                                            CodeUpdateState::END;
                                        setCodeUpdateProgress(false);
                                        auto sensorId =
                                            getFirmwareUpdateSensor();
                                        sendStateSensorEvent(
                                            sensorId, PLDM_STATE_SENSOR_STATE,
                                            0, uint8_t(state),
                                            uint8_t(CodeUpdateState::START));
                                        newImageId.clear();
                                        imageActivationMatch.reset();
                                    }
                                    else if (propVal == "xyz.openbmc_project."
                                                        "Software.Activation."
                                                        "Activations.Failed" ||
                                             propVal == "xyz.openbmc_"
                                                        "project.Software."
                                                        "Activation."
                                                        "Activations."
                                                        "Invalid")
                                    {
                                        info(
                                            "Image activation Failed or image Invalid, sending Failure on End update to PHYP");
                                        CodeUpdateState state =
                                            CodeUpdateState::FAIL;
                                        setCodeUpdateProgress(false);
                                        auto sensorId =
                                            getFirmwareUpdateSensor();
                                        sendStateSensorEvent(
                                            sensorId, PLDM_STATE_SENSOR_STATE,
                                            0, uint8_t(state),
                                            uint8_t(CodeUpdateState::START));
                                        newImageId.clear();
                                        imageActivationMatch.reset();
                                    }
                                }
                                    });
                        }
                        auto rc = setRequestedActivation();
                        if (rc != PLDM_SUCCESS)
                        {
                            CodeUpdateState state = CodeUpdateState::FAIL;
                            setCodeUpdateProgress(false);
                            auto sensorId = getFirmwareUpdateSensor();
                            sendStateSensorEvent(
                                sensorId, PLDM_STATE_SENSOR_STATE, 0,
                                uint8_t(state),
                                uint8_t(CodeUpdateState::START));
                            error("could not set RequestedActivation");
                        }
                        break;
                    }
                    else
                    {
                        // Out of band update
                        processRenameEvent();
                    }
                }
                catch (const sdbusplus::exception::exception& e)
                {
                    error(
                        "Error in getting Activation status, ERROR={ERR_EXCEP}, INTERFACE={INTF}, OBJECT PATH={OBJ_PATH}",
                        "ERR_EXCEP", e.what(), "INTF", imageInterface,
                        "OBJ_PATH", imageObjPath);
                }
            }
        }
        }));
}

void CodeUpdate::processRenameEvent()
{
    info("Processing Rename Event");

    BiosAttributeList biosAttrList;
    pldm_boot_side_data pldmBootSideData = readBootSideFile();
    pldmBootSideData.current_boot_side = "Perm";
    pldmBootSideData.next_boot_side = "Perm";

    currBootSide = Pside;
    nextBootSide = Pside;

    auto sensorId = getBootSideRenameStateSensor();
    sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0,
                         PLDM_BOOT_SIDE_HAS_BEEN_RENAMED,
                         PLDM_BOOT_SIDE_NOT_RENAMED);
    writeBootSideFile(pldmBootSideData);
    biosAttrList.push_back(
        std::make_pair(bootSideAttrName, pldmBootSideData.current_boot_side));
    biosAttrList.push_back(
        std::make_pair(bootNextSideAttrName, pldmBootSideData.next_boot_side));
    setBiosAttr(biosAttrList);
}

void CodeUpdate::writeBootSideFile(const pldm_boot_side_data& pldmBootSideData)
{
    fs::create_directories(bootSideDirPath.parent_path());
    std::ofstream writeFile(bootSideDirPath.string(),
                            std::ios::out | std::ios::binary);
    if (writeFile)
    {
        writeFile << pldmBootSideData.current_boot_side << std::endl;
        writeFile << pldmBootSideData.next_boot_side << std::endl;
        writeFile << pldmBootSideData.running_version_object << std::endl;

        writeFile.close();
    }
}

pldm_boot_side_data CodeUpdate::readBootSideFile()
{
    pldm_boot_side_data pldmBootSideDataRead{};

    std::ifstream readFile(bootSideDirPath.string(),
                           std::ios::in | std::ios::binary);

    if (readFile)
    {
        readFile >> pldmBootSideDataRead.current_boot_side;
        readFile >> pldmBootSideDataRead.next_boot_side;
        readFile >> pldmBootSideDataRead.running_version_object;

        readFile.close();
    }

    return pldmBootSideDataRead;
}

void CodeUpdate::processPriorityChangeNotification(
    const DbusChangedProps& chProperties)
{
    static constexpr auto propName = "Priority";
    const auto it = chProperties.find(propName);
    if (it == chProperties.end())
    {
        return;
    }
    uint8_t newVal = std::get<uint8_t>(it->second);

    pldm_boot_side_data pldmBootSideData = readBootSideFile();
    pldmBootSideData.next_boot_side =
        (newVal == 0)
            ? pldmBootSideData.current_boot_side
            : ((pldmBootSideData.current_boot_side == "Temp") ? "Perm"
                                                              : "Temp");
    writeBootSideFile(pldmBootSideData);
    nextBootSide = (pldmBootSideData.next_boot_side == "Temp" ? Tside : Pside);
    std::string currNextBootSide = getBiosAttrValue(bootNextSideAttrName);
    if (currNextBootSide == nextBootSide)
    {
        return;
    }
    BiosAttributeList biosAttrList;
    biosAttrList.push_back(
        std::make_pair(bootNextSideAttrName, pldmBootSideData.next_boot_side));
    setBiosAttr(biosAttrList);
}

void CodeUpdate::setOemPlatformHandler(
    pldm::responder::oem_platform::Handler* handler)
{
    oemPlatformHandler = handler;
}

void CodeUpdate::clearDirPath(const std::string& dirPath)
{
    if (std::filesystem::is_directory(dirPath))
    {
        for (const auto& iter : std::filesystem::directory_iterator(dirPath))
        {
            std::filesystem::remove_all(iter);
        }
    }
}

void CodeUpdate::sendStateSensorEvent(
    uint16_t sensorId, enum sensor_event_class_states sensorEventClass,
    uint8_t sensorOffset, uint8_t eventState, uint8_t prevEventState)
{
    pldm::responder::oem_ibm_platform::Handler* oemIbmPlatformHandler =
        dynamic_cast<pldm::responder::oem_ibm_platform::Handler*>(
            oemPlatformHandler);
    oemIbmPlatformHandler->sendStateSensorEvent(
        sensorId, sensorEventClass, sensorOffset, eventState, prevEventState);
}

void CodeUpdate::deleteImage()
{
    static constexpr auto UPDATER_SERVICE =
        "xyz.openbmc_project.Software.BMC.Updater";
    static constexpr auto SW_OBJ_PATH = "/xyz/openbmc_project/software";
    static constexpr auto DELETE_INTF =
        "xyz.openbmc_project.Collection.DeleteAll";

    auto& bus = dBusIntf->getBus();
    try
    {
        auto method = bus.new_method_call(UPDATER_SERVICE, SW_OBJ_PATH,
                                          DELETE_INTF, "DeleteAll");
        bus.call_noreply(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
    }
    catch (const std::exception& e)
    {
        error("Failed to delete image, ERROR={ERR_EXCEP}", "ERR_EXCEP",
              e.what());
        return;
    }
}

uint8_t fetchBootSide(uint16_t entityInstance, CodeUpdate* codeUpdate)
{
    uint8_t sensorOpState = tSideNum;
    if (entityInstance == 0)
    {
        auto currSide = codeUpdate->fetchCurrentBootSide();
        if (currSide == Pside)
        {
            sensorOpState = pSideNum;
        }
    }
    else if (entityInstance == 1)
    {
        auto nextSide = codeUpdate->fetchNextBootSide();
        if (nextSide == Pside)
        {
            sensorOpState = pSideNum;
        }
    }
    else
    {
        sensorOpState = PLDM_SENSOR_UNKNOWN;
    }

    return sensorOpState;
}

int setBootSide(uint16_t entityInstance, uint8_t currState,
                const std::vector<set_effecter_state_field>& stateField,
                CodeUpdate* codeUpdate)
{
    int rc = PLDM_SUCCESS;
    auto side = (stateField[currState].effecter_state == pSideNum) ? "P" : "T";

    if (entityInstance == 0)
    {
        rc = codeUpdate->setCurrentBootSide(side);
    }
    else if (entityInstance == 1)
    {
        rc = codeUpdate->setNextBootSide(side);
    }
    else
    {
        rc = PLDM_PLATFORM_INVALID_STATE_VALUE;
    }
    return rc;
}

template <typename... T>
int executeCmd(const T&... t)
{
    std::stringstream cmd;
    ((cmd << t << " "), ...) << std::endl;
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe)
    {
        throw std::runtime_error("popen() failed!");
    }
    int rc = pclose(pipe);
    if (WEXITSTATUS(rc))
    {
        std::cerr << "Error executing: ";
        ((std::cerr << " " << t), ...);
        std::cerr << "\n";
        return -1;
    }

    return 0;
}

int processCodeUpdateLid(const std::string& filePath)
{
    struct LidHeader
    {
        uint16_t magicNumber;
        uint16_t headerVersion;
        uint32_t lidNumber;
        uint32_t lidDate;
        uint16_t lidTime;
        uint16_t lidClass;
        uint32_t lidCrc;
        uint32_t lidSize;
        uint32_t headerSize;
    };
    LidHeader header;

    std::ifstream ifs(filePath, std::ios::in | std::ios::binary);
    if (!ifs)
    {
        error("ifstream open error: {FILE_PATH}", "FILE_PATH",
              filePath.c_str());
        return PLDM_ERROR;
    }
    ifs.seekg(0);
    ifs.read(reinterpret_cast<char*>(&header), sizeof(header));

    // File size should be the value of lid size minus the header size
    auto fileSize = fs::file_size(filePath);
    fileSize -= htonl(header.headerSize);
    if (fileSize < htonl(header.lidSize))
    {
        // File is not completely written yet
        ifs.close();
        return PLDM_SUCCESS;
    }

    constexpr auto magicNumber = 0x0222;
    if (htons(header.magicNumber) != magicNumber)
    {
        error("Invalid magic number: {FILE_PATH}", "FILE_PATH",
              filePath.c_str());
        ifs.close();
        return PLDM_ERROR;
    }

    if (!fs::exists(imageDirPath))
    {
        fs::create_directories(imageDirPath);
    }
    if (!fs::exists(lidDirPath))
    {
        fs::create_directories(lidDirPath);

        // Set the lid directory permissions to 777
        std::error_code ec;
        fs::permissions(lidDirPath, fs::perms::all, fs::perm_options::replace,
                        ec);
        if (ec)
        {
            error("Failed to set the lid directory permissions:{ERR}", "ERR",
                  ec.message());
            return PLDM_ERROR;
        }
    }

    constexpr auto bmcClass = 0x2000;
    if (htons(header.lidClass) == bmcClass)
    {
        // Skip the header and concatenate the BMC LIDs into a tar file
        std::ofstream ofs(tarImagePath,
                          std::ios::out | std::ios::binary | std::ios::app);
        ifs.seekg(htonl(header.headerSize));
        ofs << ifs.rdbuf();
        ofs.flush();
        ofs.close();
    }
    else
    {
        std::stringstream lidFileName;
        lidFileName << std::hex << htonl(header.lidNumber) << ".lid";
        auto lidNoHeaderPath = fs::path(lidDirPath) / lidFileName.str();
        std::ofstream ofs(lidNoHeaderPath,
                          std::ios::out | std::ios::binary | std::ios::trunc);
        ifs.seekg(htonl(header.headerSize));
        ofs << ifs.rdbuf();
        ofs.flush();
        ofs.close();

        // Set the lid file permissions to 440
        std::error_code ec;
        fs::permissions(lidNoHeaderPath,
                        fs::perms::owner_read | fs::perms::group_read,
                        fs::perm_options::replace, ec);
        if (ec)
        {
            error("Failed to set the lid file permissions: {ERR}", "ERR",
                  ec.message());
            return PLDM_ERROR;
        }
    }

    ifs.close();
    fs::remove(filePath);
    return PLDM_SUCCESS;
}

int CodeUpdate::assembleCodeUpdateImage()
{
    static constexpr auto UPDATER_SERVICE =
        "xyz.openbmc_project.Software.BMC.Updater";
    static constexpr auto SOFTWARE_PATH = "/xyz/openbmc_project/software";
    static constexpr auto LID_INTERFACE = "xyz.openbmc_project.Software.LID";

    auto& bus = dBusIntf->getBus();

    try
    {
        info("InbandCodeUpdate: AssembleCodeUpdateImage");
        auto method = bus.new_method_call(UPDATER_SERVICE, SOFTWARE_PATH,
                                          LID_INTERFACE,
                                          "AssembleCodeUpdateImage");
        bus.call_noreply(
            method,
            std::chrono::duration_cast<microsec>(sec(DBUS_TIMEOUT)).count());
    }
    catch (const std::exception& e)
    {
        error(
            "InbandCodeUpdate: Failed to Assemble code update image ERROR={ERR_EXCEP}",
            "ERR_EXCEP", e.what());
        setCodeUpdateProgress(false);
        auto sensorId = getFirmwareUpdateSensor();
        sendStateSensorEvent(sensorId, PLDM_STATE_SENSOR_STATE, 0,
                             uint8_t(CodeUpdateState::FAIL),
                             uint8_t(CodeUpdateState::START));
        return -1;
    }
    return PLDM_SUCCESS;
}

} // namespace responder
} // namespace pldm
