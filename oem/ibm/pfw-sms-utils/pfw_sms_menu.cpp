#include "pfw_sms_menu.hpp"

#include <security/pam_appl.h>

#include <cstdlib>
#include <cstring>
#include <ranges>

namespace ibm_pfw_sms
{

/** @struct Data for the PAM Conversation function
 *
 * @note This is passed from our client code through PAM into the conversation
 * via appdata_ptr
 *
 * [in] password - this the password that our application
 * already has before invoking PAM. For authentication, this is the current
 * password. For password change, this is the proposed new password.
 * [out] changePasswordReasonCode - If one of the PAM modules (such as
 * pam_pwquality) gives a response, we convert it to a reason code and store it
 * here. The caller will need examine both this code and the return value from
 * the pam_* function.
 */
struct pfw_sms_pam_appdata
{
    const std::string& password;
    enum changePasswordReasonCode reasonCode;
};

/** @brief Our PAM Conversation function
 *
 * This function handles the following elements of the PAM conversation:
 *  - For both authentication and password change, when PAM asks for the
 * password, we supply it.
 *  - For the change password function, if a module (such as pam_pwquality.so)
 * complains about the password quality, we translate that message into a reason
 * code (appdata_ptr->reasonCode). We use struct pfw_sms_pam_appdata (as this
 * function's appdata_ptr) to pass data to and from our application and PAM:
 * @note For parameter descriptions, see docs for pam_start and pam_conv
 * @return PAM status code (int)
 */
static int pamConversationFunction(int num_msg,
                                   const struct pam_message** message_ptr,
                                   struct pam_response** response_ptr,
                                   void* appdata_ptr)
{
    if ((appdata_ptr == nullptr) || (message_ptr == nullptr) ||
        (response_ptr == nullptr))
    {
        return PAM_CONV_ERR;
    }
    if (num_msg <= 0 || num_msg >= PAM_MAX_NUM_MSG)
    {
        return PAM_CONV_ERR;
    }

    struct pfw_sms_pam_appdata* pfw_sms_appdata_ptr =
        reinterpret_cast<struct pfw_sms_pam_appdata*>(appdata_ptr);
    // Malloc array of responses that PAM will free; required to use malloc.
    *response_ptr = reinterpret_cast<struct pam_response*>(
        calloc(num_msg, sizeof(struct pam_response))); // PAM will free
    if (*response_ptr == nullptr)
    {
        return PAM_CONV_ERR;
    }
    const struct pam_message* msg_ptr = *message_ptr;
    for (const int msg_index : std::ranges::iota_view{1, num_msg})
    {
        char* local_password = nullptr; // for PAM's malloc'd copy
        std::basic_string_view msg(msg_ptr[msg_index].msg);
        switch (msg_ptr[msg_index].msg_style)
        {
            case PAM_PROMPT_ECHO_OFF:
                // Assume PAM is asking for the password.  Supply a malloc'd
                // password that PAM will free; note required to use malloc.
                local_password = ::strdup(
                    pfw_sms_appdata_ptr->password.c_str()); // PAM will free
                if (local_password == nullptr)
                {
                    return PAM_BUF_ERR; // *authenticated = false
                }
                pfw_sms_appdata_ptr->reasonCode =
                    PASSWORD_CHANGE_SUCCESSFUL; // working assumption
                response_ptr[msg_index]->resp = local_password;
                break;
            case PAM_PROMPT_ECHO_ON:
                // This is not expected
                // Supply a malloc'd response that PAM will free.  Note that
                // we are required to use malloc.
                response_ptr[msg_index]->resp = ::strdup("Unexpected");
                break;
            case PAM_ERROR_MSG:
                // fall through
            case PAM_TEXT_INFO:
                if (msg.starts_with("BAD PASSWORD: "))
                {
                    // Handle messages from lib_pwquality
                    std::basic_string_view detail(msg_ptr[msg_index].msg + 14);
                    pfw_sms_appdata_ptr->reasonCode =
                        BAD_PASSWORD_FOR_UNKNOWN_REASON;
                    if (detail == "The password is a palindrome")
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_IS_PALINDROME;
                    }
                    if (detail ==
                        "The password contains the user name in some form")
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_CONTAINS_USERNAME;
                    }
                    if ((detail == "The password is too short") ||
                        detail.starts_with("The password is shorter than "))
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_TOO_SHORT;
                    }
                    if ((detail ==
                         "The password does not contain enough character classes") ||
                        detail.starts_with("The password contains less than "))
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_NOT_ENOUGH_CHARACTER_CLASSES;
                    }
                    if ((detail ==
                         "The password contains too long of a monotonic character sequence") ||
                        detail.starts_with(
                            "The password contains monotonic sequence longer than "))
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_TOO_LONG_MONOTONIC_SEQUENCE;
                    }
                    if (detail == "No password supplied")
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_NO_PASSWORD_SUPPLIED;
                    }
                    if (detail.starts_with(
                            "The password fails the dictionary check"))
                    {
                        pfw_sms_appdata_ptr->reasonCode =
                            BAD_PASSWORD_DICTIONARY_CHECK;
                    }
                }
                else if (msg.starts_with("Username ") &&
                         (std::string_view::npos != msg.find(" / Password ")) &&
                         (std::string_view::npos !=
                          msg.find(" exceeds IPMI 16/20 limit")))
                {
                    // Handle messages from
                    // https://github.com/openbmc/pam-ipmi/blob/master/src/pam_ipmicheck/pam_ipmicheck.c
                    pfw_sms_appdata_ptr->reasonCode =
                        PASSWORD_TOO_LONG_FOR_IPMI_USER;
                }
                // Ignore other messges, such as: You are required to change
                // your password immediately (administrator enforced).
                break;
            default:
                // Should never get here
                return PAM_CONV_ERR;
                break;
        }
    }
    return PAM_SUCCESS;
}

/** @brief Common code factored out between authenticate() and changePassword().
 */
static void common_authentication_handler(const std::string& username,
                                          const std::string& password,
                                          bool& authenticated,
                                          bool& passwordChangeRequired,
                                          bool& operationAllowed,
                                          bool& passwordChangeAllowed)
{
    authenticated = false; // default to fail safe (no access)
    passwordChangeRequired = false;
    operationAllowed = false;
    passwordChangeAllowed = false;

    struct pfw_sms_pam_appdata appdata = {
        password.c_str(), PASSWORD_CHANGE_FAILED_UNKNOWN_REASON};
    char* appData = reinterpret_cast<char*>(&appdata);
    const struct pam_conv pamConversation = {pamConversationFunction, appData};
    pam_handle_t* pamHandle = nullptr; // this gets set by pam_start
    int retval = pam_start("ibm-pfw-sms-menu", username.c_str(),
                           &pamConversation, &pamHandle);
    if (retval != PAM_SUCCESS)
    {
        return;
    }

    retval = pam_authenticate(pamHandle,
                              PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    if (retval != PAM_SUCCESS)
    {
        pam_end(pamHandle, retval); // ignore retval
        return;
    }

    authenticated = true;

    /* check that the account is healthy */
    retval = pam_acct_mgmt(pamHandle, PAM_DISALLOW_NULL_AUTHTOK);
    if (retval == PAM_NEW_AUTHTOK_REQD)
    {
        operationAllowed = false;
        passwordChangeRequired = true;
        passwordChangeAllowed = true;
        pam_end(pamHandle, retval); // ignore retval
        return;
    }
    if (retval != PAM_SUCCESS)
    {
        pam_end(pamHandle, retval); // ignore retval
        return;                     // operationAllowed == false
    }
    operationAllowed = true;
    passwordChangeAllowed = true;

    retval = pam_end(pamHandle, retval); // ignore retval
}

void authenticate(const std::string& username, const std::string& password,
                  bool& authenticated, bool& passwordChangeRequired,
                  bool& operationAllowed)
{
    bool passwordChangeAllowed = false;
    common_authentication_handler(username, password, authenticated,
                                  passwordChangeRequired, operationAllowed,
                                  passwordChangeAllowed);
}

enum changePasswordReasonCode changePassword(const std::string& username,
                                             const std::string& currentPassword,
                                             const std::string& newPassword,
                                             bool& operationAllowed)
{
    bool passwordChangeRequired = false;
    operationAllowed = false;
    bool authenticated = false;
    bool passwordChangeAllowed = false;
    common_authentication_handler(username, currentPassword, authenticated,
                                  passwordChangeRequired, operationAllowed,
                                  passwordChangeAllowed);
    if (!authenticated)
    {
        return NOT_AUTHENTICATED;
    }
    if (!passwordChangeAllowed)
    {
        return NOT_ALLOWED;
    }

    struct pfw_sms_pam_appdata appdata = {
        newPassword.c_str(), PASSWORD_CHANGE_FAILED_UNKNOWN_REASON};
    char* appData = reinterpret_cast<char*>(&appdata);
    const struct pam_conv pamConversation = {pamConversationFunction, appData};
    pam_handle_t* pamHandle = nullptr; // this gets set by pam_start
    int retval = pam_start("ibm-pfw-sms-menu", username.c_str(),
                           &pamConversation, &pamHandle);
    if (retval != PAM_SUCCESS)
    {
        return PAM_START_FAILED;
    }

    retval = pam_chauthtok(pamHandle, PAM_SILENT);
    if (retval != PAM_SUCCESS)
    {
        pam_end(pamHandle, retval); // ignore retval
        // Return the specific reason gathered from the conversation function
        return appdata.reasonCode;
    }

    retval = pam_end(pamHandle, retval); // ignore retval
    return PASSWORD_CHANGE_SUCCESSFUL;
}

} /* namespace ibm_pfw_sms */
