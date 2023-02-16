#include "device_updater.hpp"

#include "libpldm/firmware_update.h"

#include "activation.hpp"
#include "update_manager.hpp"

#include <phosphor-logging/lg2.hpp>

#include <functional>

PHOSPHOR_LOG2_USING;

namespace pldm
{
namespace fw_update
{
void DeviceUpdater::startFwUpdateFlow()
{
    auto instanceId = updateManager->requester.getInstanceId(eid);
    // NumberOfComponents
    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    // PackageDataLength
    const auto& fwDevicePkgData =
        std::get<FirmwareDevicePackageData>(fwDeviceIDRecord);
    // ComponentImageSetVersionString
    const auto& compImageSetVersion =
        std::get<ComponentImageSetVersion>(fwDeviceIDRecord);
    variable_field compImgSetVerStrInfo{};
    compImgSetVerStrInfo.ptr =
        reinterpret_cast<const uint8_t*>(compImageSetVersion.data());
    compImgSetVerStrInfo.length =
        static_cast<uint8_t>(compImageSetVersion.size());

    Request request(sizeof(pldm_msg_hdr) +
                    sizeof(struct pldm_request_update_req) +
                    compImgSetVerStrInfo.length);
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());

    auto rc = encode_request_update_req(
        instanceId, maxTransferSize, applicableComponents.size(),
        PLDM_FWUP_MIN_OUTSTANDING_REQ, fwDevicePkgData.size(),
        PLDM_STR_TYPE_ASCII, compImgSetVerStrInfo.length, &compImgSetVerStrInfo,
        requestMsg,
        sizeof(struct pldm_request_update_req) + compImgSetVerStrInfo.length);
    if (rc)
    {
        updateManager->requester.markFree(eid, instanceId);
        error("encode_request_update_req failed, EID = {EID}, RC = {RC}", "EID",
              unsigned(eid), "RC", rc);
        // Handle error scenario
    }

    rc = updateManager->handler.registerRequest(
        eid, instanceId, PLDM_FWUP, PLDM_REQUEST_UPDATE, std::move(request),
        std::move(std::bind_front(&DeviceUpdater::requestUpdate, this)));
    if (rc)
    {
        error("Failed to send RequestUpdate request, EID = {EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        // Handle error scenario
    }
}

void DeviceUpdater::requestUpdate(mctp_eid_t eid, const pldm_msg* response,
                                  size_t respMsgLen)
{
    if (response == nullptr || !respMsgLen)
    {
        // Handle error scenario
        error("No response received for RequestUpdate, EID = {EID}", "EID",
              unsigned(eid));
        return;
    }

    uint8_t completionCode = 0;
    uint16_t fdMetaDataLen = 0;
    uint8_t fdWillSendPkgData = 0;

    auto rc = decode_request_update_resp(response, respMsgLen, &completionCode,
                                         &fdMetaDataLen, &fdWillSendPkgData);
    if (rc)
    {
        error("Decoding RequestUpdate response failed, EID = {EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        return;
    }
    if (completionCode)
    {
        error(
            "RequestUpdate response failed with error completion code, EID = {EID}, CC = {CC}",
            "EID", unsigned(eid), "CC", unsigned(completionCode));
        return;
    }

    // Optional fields DeviceMetaData and GetPackageData not handled
    pldmRequest = std::make_unique<sdeventplus::source::Defer>(
        updateManager->event,
        std::bind(&DeviceUpdater::sendPassCompTableRequest, this,
                  componentIndex));
}

void DeviceUpdater::sendPassCompTableRequest(size_t offset)
{
    pldmRequest.reset();

    auto instanceId = updateManager->requester.getInstanceId(eid);
    // TransferFlag
    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    uint8_t transferFlag = 0;
    if (applicableComponents.size() == 1)
    {
        transferFlag = PLDM_START_AND_END;
    }
    else if (offset == 0)
    {
        transferFlag = PLDM_START;
    }
    else if (offset == applicableComponents.size() - 1)
    {
        transferFlag = PLDM_END;
    }
    else
    {
        transferFlag = PLDM_MIDDLE;
    }
    const auto& comp = compImageInfos[applicableComponents[offset]];
    // ComponentClassification
    CompClassification compClassification = std::get<static_cast<size_t>(
        ComponentImageInfoPos::CompClassificationPos)>(comp);
    // ComponentIdentifier
    CompIdentifier compIdentifier =
        std::get<static_cast<size_t>(ComponentImageInfoPos::CompIdentifierPos)>(
            comp);
    // ComponentClassificationIndex
    CompClassificationIndex compClassificationIndex{};
    auto compKey = std::make_pair(compClassification, compIdentifier);
    if (compInfo.contains(compKey))
    {
        auto search = compInfo.find(compKey);
        compClassificationIndex = search->second;
    }
    else
    {
        // Handle error scenario
    }
    // ComponentComparisonStamp
    CompComparisonStamp compComparisonStamp = std::get<static_cast<size_t>(
        ComponentImageInfoPos::CompComparisonStampPos)>(comp);
    // ComponentVersionString
    const auto& compVersion =
        std::get<static_cast<size_t>(ComponentImageInfoPos::CompVersionPos)>(
            comp);
    variable_field compVerStrInfo{};
    compVerStrInfo.ptr = reinterpret_cast<const uint8_t*>(compVersion.data());
    compVerStrInfo.length = static_cast<uint8_t>(compVersion.size());

    Request request(sizeof(pldm_msg_hdr) +
                    sizeof(struct pldm_pass_component_table_req) +
                    compVerStrInfo.length);
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());
    auto rc = encode_pass_component_table_req(
        instanceId, transferFlag, compClassification, compIdentifier,
        compClassificationIndex, compComparisonStamp, PLDM_STR_TYPE_ASCII,
        compVerStrInfo.length, &compVerStrInfo, requestMsg,
        sizeof(pldm_pass_component_table_req) + compVerStrInfo.length);
    if (rc)
    {
        updateManager->requester.markFree(eid, instanceId);
        error("encode_pass_component_table_req failed, EID = {EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        // Handle error scenario
    }

    rc = updateManager->handler.registerRequest(
        eid, instanceId, PLDM_FWUP, PLDM_PASS_COMPONENT_TABLE,
        std::move(request),
        std::move(std::bind_front(&DeviceUpdater::passCompTable, this)));
    if (rc)
    {
        error(
            "Failed to send PassComponentTable request, EID = {EID}, RC = {RC}",
            "EID", unsigned(eid), "RC", rc);
        // Handle error scenario
    }
}

void DeviceUpdater::passCompTable(mctp_eid_t eid, const pldm_msg* response,
                                  size_t respMsgLen)
{
    if (response == nullptr || !respMsgLen)
    {
        // Handle error scenario
        error("No response received for PassComponentTable, EID = {EID}", "EID",
              unsigned(eid));
        return;
    }

    uint8_t completionCode = 0;
    uint8_t compResponse = 0;
    uint8_t compResponseCode = 0;

    auto rc =
        decode_pass_component_table_resp(response, respMsgLen, &completionCode,
                                         &compResponse, &compResponseCode);
    if (rc)
    {
        // Handle error scenario
        error(
            "Decoding PassComponentTable response failed, EID={EID}, RC = {RC}",
            "EID", unsigned(eid), "RC", rc);
        return;
    }
    if (completionCode)
    {
        // Handle error scenario
        error(
            "PassComponentTable response failed with error completion code, EID = {EID}, CC = {CC}",
            "EID", unsigned(eid), "CC", unsigned(completionCode));
        return;
    }
    // Handle ComponentResponseCode

    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    if (componentIndex == applicableComponents.size() - 1)
    {
        componentIndex = 0;
        pldmRequest = std::make_unique<sdeventplus::source::Defer>(
            updateManager->event,
            std::bind(&DeviceUpdater::sendUpdateComponentRequest, this,
                      componentIndex));
    }
    else
    {
        componentIndex++;
        pldmRequest = std::make_unique<sdeventplus::source::Defer>(
            updateManager->event,
            std::bind(&DeviceUpdater::sendPassCompTableRequest, this,
                      componentIndex));
    }
}

void DeviceUpdater::sendUpdateComponentRequest(size_t offset)
{
    pldmRequest.reset();

    auto instanceId = updateManager->requester.getInstanceId(eid);
    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    const auto& comp = compImageInfos[applicableComponents[offset]];
    // ComponentClassification
    CompClassification compClassification = std::get<static_cast<size_t>(
        ComponentImageInfoPos::CompClassificationPos)>(comp);
    // ComponentIdentifier
    CompIdentifier compIdentifier =
        std::get<static_cast<size_t>(ComponentImageInfoPos::CompIdentifierPos)>(
            comp);
    // ComponentClassificationIndex
    CompClassificationIndex compClassificationIndex{};
    auto compKey = std::make_pair(compClassification, compIdentifier);
    if (compInfo.contains(compKey))
    {
        auto search = compInfo.find(compKey);
        compClassificationIndex = search->second;
    }
    else
    {
        // Handle error scenario
    }

    // UpdateOptionFlags
    bitfield32_t updateOptionFlags;
    updateOptionFlags.bits.bit0 = std::get<3>(comp)[0];
    // ComponentVersion
    const auto& compVersion = std::get<7>(comp);
    variable_field compVerStrInfo{};
    compVerStrInfo.ptr = reinterpret_cast<const uint8_t*>(compVersion.data());
    compVerStrInfo.length = static_cast<uint8_t>(compVersion.size());

    Request request(sizeof(pldm_msg_hdr) +
                    sizeof(struct pldm_update_component_req) +
                    compVerStrInfo.length);
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());

    auto rc = encode_update_component_req(
        instanceId, compClassification, compIdentifier, compClassificationIndex,
        std::get<static_cast<size_t>(
            ComponentImageInfoPos::CompComparisonStampPos)>(comp),
        std::get<static_cast<size_t>(ComponentImageInfoPos::CompSizePos)>(comp),
        updateOptionFlags, PLDM_STR_TYPE_ASCII, compVerStrInfo.length,
        &compVerStrInfo, requestMsg,
        sizeof(pldm_update_component_req) + compVerStrInfo.length);
    if (rc)
    {
        updateManager->requester.markFree(eid, instanceId);
        error("encode_update_component_req failed, EID={EID}, RC = {RC}", "EID",
              unsigned(eid), "RC", rc);
        // Handle error scenario
    }

    rc = updateManager->handler.registerRequest(
        eid, instanceId, PLDM_FWUP, PLDM_UPDATE_COMPONENT, std::move(request),
        std::move(std::bind_front(&DeviceUpdater::updateComponent, this)));
    if (rc)
    {
        error("Failed to send UpdateComponent request, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        // Handle error scenario
    }
}

void DeviceUpdater::updateComponent(mctp_eid_t eid, const pldm_msg* response,
                                    size_t respMsgLen)
{
    if (response == nullptr || !respMsgLen)
    {
        // Handle error scenario
        error("No response received for updateComponent, EID={EID}", "EID",
              unsigned(eid));
        return;
    }

    uint8_t completionCode = 0;
    uint8_t compCompatibilityResp = 0;
    uint8_t compCompatibilityRespCode = 0;
    bitfield32_t updateOptionFlagsEnabled{};
    uint16_t timeBeforeReqFWData = 0;

    auto rc = decode_update_component_resp(
        response, respMsgLen, &completionCode, &compCompatibilityResp,
        &compCompatibilityRespCode, &updateOptionFlagsEnabled,
        &timeBeforeReqFWData);
    if (rc)
    {
        error("Decoding UpdateComponent response failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        return;
    }
    if (completionCode)
    {
        error(
            "UpdateComponent response failed with error completion code, EID = {EID}, CC = {CC}",
            "EID", unsigned(eid), "CC", unsigned(completionCode));
        return;
    }
}

Response DeviceUpdater::requestFwData(const pldm_msg* request,
                                      size_t payloadLength)
{
    uint8_t completionCode = PLDM_SUCCESS;
    uint32_t offset = 0;
    uint32_t length = 0;
    Response response(sizeof(pldm_msg_hdr) + sizeof(completionCode), 0);
    auto responseMsg = reinterpret_cast<pldm_msg*>(response.data());
    auto rc = decode_request_firmware_data_req(request, payloadLength, &offset,
                                               &length);
    if (rc)
    {
        error(
            "Decoding RequestFirmwareData request failed, EID={EID}, RC = {RC}",
            "EID", unsigned(eid), "RC", rc);
        rc = encode_request_firmware_data_resp(
            request->hdr.instance_id, PLDM_ERROR_INVALID_DATA, responseMsg,
            sizeof(completionCode));
        if (rc)
        {
            error(
                "Encoding RequestFirmwareData response failed, EID = {EID}, RC = {RC}",
                "EID", unsigned(eid), "RC", rc);
        }
        return response;
    }

    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    const auto& comp = compImageInfos[applicableComponents[componentIndex]];
    auto compOffset = std::get<5>(comp);
    auto compSize = std::get<6>(comp);
    error("offset = {OFFSET}, length = {LEN}", "OFFSET", unsigned(offset),
          "LEN", unsigned(length));
    if (length < PLDM_FWUP_BASELINE_TRANSFER_SIZE || length > maxTransferSize)
    {
        rc = encode_request_firmware_data_resp(
            request->hdr.instance_id, PLDM_FWUP_INVALID_TRANSFER_LENGTH,
            responseMsg, sizeof(completionCode));
        if (rc)
        {
            error(
                "Encoding RequestFirmwareData response failed, EID={EID}, RC = {RC}",
                "EID", unsigned(eid), "RC", rc);
        }
        return response;
    }

    if (offset + length > compSize + PLDM_FWUP_BASELINE_TRANSFER_SIZE)
    {
        rc = encode_request_firmware_data_resp(
            request->hdr.instance_id, PLDM_FWUP_DATA_OUT_OF_RANGE, responseMsg,
            sizeof(completionCode));
        if (rc)
        {
            error(
                "Encoding RequestFirmwareData response failed, EID={EID}, RC = {RC}",
                "EID", unsigned(eid), "RC", rc);
        }
        return response;
    }

    size_t padBytes = 0;
    if (offset + length > compSize)
    {
        padBytes = offset + length - compSize;
    }

    response.resize(sizeof(pldm_msg_hdr) + sizeof(completionCode) + length);
    responseMsg = reinterpret_cast<pldm_msg*>(response.data());
    package.seekg(compOffset + offset);
    package.read(reinterpret_cast<char*>(response.data() +
                                         sizeof(pldm_msg_hdr) +
                                         sizeof(completionCode)),
                 length - padBytes);
    rc = encode_request_firmware_data_resp(request->hdr.instance_id,
                                           completionCode, responseMsg,
                                           sizeof(completionCode));
    if (rc)
    {
        error(
            "Encoding RequestFirmwareData response failed, EID={EID}, RC = {RC}",
            "EID", unsigned(eid), "RC", rc);
        return response;
    }

    return response;
}

Response DeviceUpdater::transferComplete(const pldm_msg* request,
                                         size_t payloadLength)
{
    uint8_t completionCode = PLDM_SUCCESS;
    Response response(sizeof(pldm_msg_hdr) + sizeof(completionCode), 0);
    auto responseMsg = reinterpret_cast<pldm_msg*>(response.data());

    uint8_t transferResult = 0;
    auto rc =
        decode_transfer_complete_req(request, payloadLength, &transferResult);
    if (rc)
    {
        error("Decoding TransferComplete request failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        rc = encode_transfer_complete_resp(request->hdr.instance_id,
                                           PLDM_ERROR_INVALID_DATA, responseMsg,
                                           sizeof(completionCode));
        if (rc)
        {
            error(
                "Encoding TransferComplete response failed, EID={EID}, RC = {RC}",
                "EID", unsigned(eid), "RC", rc);
        }
        return response;
    }

    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    const auto& comp = compImageInfos[applicableComponents[componentIndex]];
    const auto& compVersion = std::get<7>(comp);

    if (transferResult == PLDM_FWUP_TRANSFER_SUCCESS)
    {
        info(
            "Component Transfer complete, EID = {EID}, COMPONENT_VERSION = {COMP_VERS}",
            "EID", unsigned(eid), "COMP_VERS", compVersion);
    }
    else
    {
        error(
            "Transfer of the component failed, EID={EID}, COMPONENT_VERSION = {COMP_VERS}, TRANSFER_RESULT = {TRANS_RES}",
            "EID", unsigned(eid), "COMP_VERS", compVersion, "TRANS_RES",
            unsigned(transferResult));
    }

    rc = encode_transfer_complete_resp(request->hdr.instance_id, completionCode,
                                       responseMsg, sizeof(completionCode));
    if (rc)
    {
        error("Encoding TransferComplete response failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        return response;
    }

    return response;
}

Response DeviceUpdater::verifyComplete(const pldm_msg* request,
                                       size_t payloadLength)
{
    uint8_t completionCode = PLDM_SUCCESS;
    Response response(sizeof(pldm_msg_hdr) + sizeof(completionCode), 0);
    auto responseMsg = reinterpret_cast<pldm_msg*>(response.data());

    uint8_t verifyResult = 0;
    auto rc = decode_verify_complete_req(request, payloadLength, &verifyResult);
    if (rc)
    {
        error("Decoding VerifyComplete request failed, EID = {EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        rc = encode_verify_complete_resp(request->hdr.instance_id,
                                         PLDM_ERROR_INVALID_DATA, responseMsg,
                                         sizeof(completionCode));
        if (rc)
        {
            error(
                "Encoding VerifyComplete response failed, EID={EID}, RC = {RC}",
                "EID", unsigned(eid), "RC", rc);
        }
        return response;
    }

    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    const auto& comp = compImageInfos[applicableComponents[componentIndex]];
    const auto& compVersion = std::get<7>(comp);

    if (verifyResult == PLDM_FWUP_VERIFY_SUCCESS)
    {
        info(
            "Component verification complete, EID={EID}, COMPONENT_VERSION={COMP_VERS}",
            "EID", unsigned(eid), "COMP_VERS", compVersion);
    }
    else
    {
        error(
            "Component verification failed, EID={EID}, COMPONENT_VERSION={COMP_VERS}, VERIFY_RESULT={VERIFY_RES}",
            "EID", unsigned(eid), "COMP_VERS", compVersion, "VERIFY_RES",
            unsigned(verifyResult));
    }

    rc = encode_verify_complete_resp(request->hdr.instance_id, completionCode,
                                     responseMsg, sizeof(completionCode));
    if (rc)
    {
        error("Encoding VerifyComplete response failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        return response;
    }

    return response;
}

Response DeviceUpdater::applyComplete(const pldm_msg* request,
                                      size_t payloadLength)
{
    uint8_t completionCode = PLDM_SUCCESS;
    Response response(sizeof(pldm_msg_hdr) + sizeof(completionCode), 0);
    auto responseMsg = reinterpret_cast<pldm_msg*>(response.data());

    uint8_t applyResult = 0;
    bitfield16_t compActivationModification{};
    auto rc = decode_apply_complete_req(request, payloadLength, &applyResult,
                                        &compActivationModification);
    if (rc)
    {
        error("Decoding ApplyComplete request failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        rc = encode_apply_complete_resp(request->hdr.instance_id,
                                        PLDM_ERROR_INVALID_DATA, responseMsg,
                                        sizeof(completionCode));
        if (rc)
        {
            error(
                "Encoding ApplyComplete response failed, EID={EID}, RC = {RC}",
                "EID", unsigned(eid), "RC", rc);
        }
        return response;
    }

    const auto& applicableComponents =
        std::get<ApplicableComponents>(fwDeviceIDRecord);
    const auto& comp = compImageInfos[applicableComponents[componentIndex]];
    const auto& compVersion = std::get<7>(comp);

    if (applyResult == PLDM_FWUP_APPLY_SUCCESS ||
        applyResult == PLDM_FWUP_APPLY_SUCCESS_WITH_ACTIVATION_METHOD)
    {
        info(
            "Component apply complete, EID = {EID}, COMPONENT_VERSION = {COMP_VERS}",
            "EID", unsigned(eid), "COMP_VERS", compVersion);
        updateManager->updateActivationProgress();
    }
    else
    {
        error(
            "Component apply failed, EID = {EID}, COMPONENT_VERSION = {COMP_VERS}, APPLY_RESULT = {APPLY_RES}",
            "EID", unsigned(eid), "COMP_VERS", compVersion, "APPLY_RES",
            unsigned(applyResult));
    }

    rc = encode_apply_complete_resp(request->hdr.instance_id, completionCode,
                                    responseMsg, sizeof(completionCode));
    if (rc)
    {
        error("Encoding ApplyComplete response failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        return response;
    }

    if (componentIndex == applicableComponents.size() - 1)
    {
        componentIndex = 0;
        pldmRequest = std::make_unique<sdeventplus::source::Defer>(
            updateManager->event,
            std::bind(&DeviceUpdater::sendActivateFirmwareRequest, this));
    }
    else
    {
        componentIndex++;
        pldmRequest = std::make_unique<sdeventplus::source::Defer>(
            updateManager->event,
            std::bind(&DeviceUpdater::sendUpdateComponentRequest, this,
                      componentIndex));
    }

    return response;
}

void DeviceUpdater::sendActivateFirmwareRequest()
{
    pldmRequest.reset();
    auto instanceId = updateManager->requester.getInstanceId(eid);
    Request request(sizeof(pldm_msg_hdr) +
                    sizeof(struct pldm_activate_firmware_req));
    auto requestMsg = reinterpret_cast<pldm_msg*>(request.data());

    auto rc = encode_activate_firmware_req(
        instanceId, PLDM_NOT_ACTIVATE_SELF_CONTAINED_COMPONENTS, requestMsg,
        sizeof(pldm_activate_firmware_req));
    if (rc)
    {
        updateManager->requester.markFree(eid, instanceId);
        error("encode_activate_firmware_req failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
    }

    rc = updateManager->handler.registerRequest(
        eid, instanceId, PLDM_FWUP, PLDM_ACTIVATE_FIRMWARE, std::move(request),
        std::move(std::bind_front(&DeviceUpdater::activateFirmware, this)));
    if (rc)
    {
        error("Failed to send ActivateFirmware request, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
    }
}

void DeviceUpdater::activateFirmware(mctp_eid_t eid, const pldm_msg* response,
                                     size_t respMsgLen)
{
    if (response == nullptr || !respMsgLen)
    {
        // Handle error scenario
        error("No response received for ActivateFirmware, EID={EID}", "EID",
              unsigned(eid));
        return;
    }

    uint8_t completionCode = 0;
    uint16_t estimatedTimeForActivation = 0;

    auto rc = decode_activate_firmware_resp(
        response, respMsgLen, &completionCode, &estimatedTimeForActivation);
    if (rc)
    {
        // Handle error scenario
        error("Decoding ActivateFirmware response failed, EID={EID}, RC = {RC}",
              "EID", unsigned(eid), "RC", rc);
        return;
    }
    if (completionCode)
    {
        // Handle error scenario
        error(
            "ActivateFirmware response failed with error completion code, EID = {EID}, CC = {CC}",
            "EID", unsigned(eid), "CC", unsigned(completionCode));
        return;
    }

    updateManager->updateDeviceCompletion(eid, true);
}

} // namespace fw_update

} // namespace pldm
