#ifndef PFW_SMS_MENU_IBM_HPP
#define PFW_SMS_MENU_IBM_HPP

#include <string>

namespace ibm_pfw_sms
{

/** @brief Return code from changePassword
 *
 * The "should never happen" codes should be captured for debug/diagnosis.
 * Comments describe the conditions for which the other codes are used;
 * these suggest what to tell interactive users.
 */
enum changePasswordReasonCode
{
    PASSWORD_CHANGE_SUCCESSFUL = 0x00,
    NOT_AUTHENTICATED = 0x01,            // Username or password was not correct
    NOT_ALLOWED = 0x02,                  // User is not BMC admin
    PAM_START_FAILED = 0x03,             // Should never happen
    LINUX_PAM_CONVERSATION_ERROR = 0x04, // Should never happen
    PASSWORD_CHANGE_FAILED_UNKNOWN_REASON = 0x0100,
    // Codes from pam_pwquality.so:
    BAD_PASSWORD_FOR_UNKNOWN_REASON = 0x0101, // The reason why the new password
                                              // was not accepted is unknown
    BAD_PASSWORD_IS_PALINDROME = 0x0102, // The new password is a palindrome
    BAD_PASSWORD_CONTAINS_USERNAME =
        0x0103,                      // The new password contains the username
    BAD_PASSWORD_TOO_SHORT = 0x0104, // The new password is too short
    BAD_PASSWORD_NOT_ENOUGH_CHARACTER_CLASSES =
        0x0105,                      // The new password does not have
                                     // enough different kinds of
                                     // characters: lowercase, uppercase,
                                     // numbers, and other
    BAD_PASSWORD_TOO_LONG_MONOTONIC_SEQUENCE =
        0x0106,                      // The new password has a monotonic
                                     // sequence, like 1234
    BAD_PASSWORD_NO_PASSWORD_SUPPLIED = 0x0107, // The new password is empty
                                                // (zero length)
    BAD_PASSWORD_DICTIONARY_CHECK = 0x0108,     // The new password contains a
                                                // dictionary word
    // Gap in numbering to accomodate future errors from pam_pwquality
    // Codes from pam_ipmicheck.so:
    PASSWORD_TOO_LONG_FOR_IPMI_USER =
        0x0201, // IPMI users are limited to 20 character passwords
};

/** @brief Authenticate a password request from PFW SMS
 *
 * @param[in] username - the name of the BMC account
 * @param[in] password - the password associated with the username
 * @param[out] authenticated - true if password authentication was
 * successful; note this only reports password authentication, and the
 * parameters passwordChangeRequired and operationAllowed should be checked
 * as appropriate.
 * @param[out] passwordChangeRequired - true if the password needs to be
 * changed
 * @param[out] operationAllowed - true if the user should be allowed to
 * perform the PFW SMS operation
 *
 * @note Authentication is to BMC user account
 */
void authenticate(const std::string& username, const std::string& password,
                  bool& authenticated, bool& passwordChangeRequired,
                  bool& operationAllowed);

/** @brief Handle password change request from PFW SMS
 *
 * @param[in] username - the name of the BMC account
 * @param[in] currentPassword - the password associated with the username
 * @param[in] newPassword - the proposed new password
 * @param[out] operationAllowed - true if the user is an authenticated admin
 * user
 * @return changePasswordReasonCode
 */
enum changePasswordReasonCode changePassword(
    const std::string& username, const std::string& currentPassword,
    const std::string& newPassword, bool& operationAllowed);

} /* namespace ibm_pfw_sms */

#endif /* PFW_SMS_MENU_IBM_HPP */
