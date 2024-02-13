#ifndef PFW_SMS_MENU_IBM_H
#define PFW_SMS_MENU_IBM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdbool.h>
#include <security/pam_appl.h>

enum changePasswordReasonCode
{
  PasswordChangeSuccessful = 0,
  NotAuthenticated = 1, // current/old password was not correct
  PAMStartFailed = 2, // Should never happen
  LinuxPAMConversationError = 3, // Should never happen
  PasswordChangeFailedUnknownReason = 1000,
  // Codes from pam_pwquality.so:
  BadPasswordForUnknownReason,  // The reason why the new password was not accepted is unknown
  BadPasswordIsPalindrome,  // The new password is a palindrome
  BadPasswordContainsUsername,  // The new password contains the username
  BadPasswordTooShort,  // The new password is too short
  BadPasswordNotEnoughCharacterClasses, // The new password does not have enough different kinds of characters: lowercase, uppcase, numbers, and other
  BadPasswordTooLongMonotonicSequence, // the new password has a monotinic sequence, like 1234
  BadPasswordNoPasswordSupplied, // The new password is empty (zero length)
  BadPasswordDictionaryCheck, // The new password contains a dictionary word
  // Codes from pam_ipmicheck.so:  etc.
  PasswordTooLongforIPMIUser = 2000, // IPMI users are limited to 20 character passwords
};

/** @brief Authenticate a password request from PFW SMS
 *
 * @param[in] username - the name of the BMC account
 * @param[in] password - the password associated with the username
 * @param[out] authenticated - true if password authentication was successful; note this only reports password authentication, and the parameters passwordChangeRequired and operationAllowed should be checked as appropriate.
 * @param[out] passwordChangeRequired - true if the password needs to be changed
 * @param[out] operationAllowed - true if the user should be allowed to perform the PFW SMS operation
 *
 * @note Authentication is to BMC user account
*/
void authenticate(const char *username, const char *password, bool *authenticated, bool *passwordChangeRequired, bool *operationAllowed);

/** @brief Handle password change request from PFW SMS
 *
 * @param[in] username - the name of the BMC account
 * @param[in] currentPassword - the password associated with the username
 * @param[in] newPassword - the proposed new password
 * @return changePasswordReasonCode
 */
enum changePasswordReasonCode
changePassword(const char *username, const char *currentPassword, const char *newPassword);

#ifdef __cplusplus
}
#endif

#endif /* PFW_SMS_MENU_IBM_H */
