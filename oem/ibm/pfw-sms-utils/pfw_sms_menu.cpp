#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <security/pam_appl.h>
#include "pfw_sms_menu.hpp"

/** @struct Data for the PAM Conversation function
 *
 * @note This is passed from our client code through PAM into the conversation via appdata_ptr
 * [in] password - this the password that our application already has before invoking PAM.
 *    For authentication, this is the current password.
 *    For password change, this is the proposed new password.
 * [out] changePasswordReasonCode - If one of the PAM modules (such as pam_pwquality) gives a
 *       response, we convert it to a reason code and store it here.
 *       The caller will need examine both this code and the return value from the pam_* function.
 */
struct pfw_sms_pam_appdata
{
  const char *password;
  enum changePasswordReasonCode reasonCode;
};

/** @brief Our PAM Conversation function
 *
 * This function handles the following elements of the PAM conversation:
 *  - For both authentication and password change, when PAM asks for the password, we supply it.
 *  - For the change password function, if a module (such as pam_pwquality.so) complains about the password quality,
 *    we translate that message into a reason code (appdata_ptr->reasonCode).
 * We use struct pfw_sms_pam_appdata (as this function's appdata_ptr) to pass data to and from our application and PAM:
 * @note For parameter descriptions, see docs for pam_start and pam_conv
 */
static int pamConversationFunction(int num_msg, const struct pam_message **message_ptr, struct pam_response **response_ptr, void *appdata_ptr)
{
  if ((appdata_ptr == NULL) || (message_ptr == NULL) || (response_ptr == NULL))
  {
    return PAM_CONV_ERR;
  }
  if (num_msg <= 0 || num_msg >= PAM_MAX_NUM_MSG)
  {
    return PAM_CONV_ERR;
  }

  struct pfw_sms_pam_appdata* pfw_sms_appdata_ptr = (struct pfw_sms_pam_appdata*)appdata_ptr;
  *response_ptr = (pam_response *)calloc(num_msg, sizeof(struct pam_response));  // PAM frees
  if (*response_ptr == NULL)
  {
    return PAM_CONV_ERR;
  }
  const struct pam_message *msg_ptr = *message_ptr;
  for (int msg = 0; msg < num_msg; ++msg)
  {
    //printf("Conv with msg=%d\n", msg);
    //printf("Conv with msg_ptr[msg]=%p\n", (void*)&msg_ptr[msg]);
    char *local_password;
    switch (msg_ptr[msg].msg_style)
    {
    case PAM_PROMPT_ECHO_OFF:
      //printf("PAM prompt echo off: %s\n", msg_ptr[msg].msg);
      // Assume PAM is asking for the password.  Supply a malloc'd pasword that PAM will free
      //printf("Supplying the password: '%s'\n", pfw_sms_appdata_ptr->password);
      local_password = strdup(pfw_sms_appdata_ptr->password); // PAM will free
      if (local_password == NULL)
      {
          return PAM_BUF_ERR; // *authenticated = false
      }
      pfw_sms_appdata_ptr->reasonCode = PasswordChangeSuccessful; // working assumption
      response_ptr[msg]->resp = local_password;
      break;
    case PAM_PROMPT_ECHO_ON:
      // This is not expected
      //printf("PAM prompt echo on: %s\n", msg_ptr[msg].msg);
      response_ptr[msg]->resp = strdup("Unexpected");
      break;
    case PAM_ERROR_MSG: // fall through
    case PAM_TEXT_INFO:
      //printf("PAM info: %s\n", msg_ptr[msg].msg);
      // PAM info: You are required to change your password immediately (administrator enforced).
      if (0 == strncmp(msg_ptr[msg].msg, "BAD PASSWORD: ", 14))
      {
          // Handle messages from lib_pwquality
	  const char *detail = msg_ptr[msg].msg + 14;
	  pfw_sms_appdata_ptr->reasonCode = BadPasswordForUnknownReason;
          if (0 == strcmp(detail, "The password is a palindrome"))
          {
              pfw_sms_appdata_ptr->reasonCode = BadPasswordIsPalindrome;
          }
          if (0 == strcmp(detail, "The password contains the user name in some form"))
          {
              pfw_sms_appdata_ptr->reasonCode = BadPasswordContainsUsername;
          }
	  if ((0 == strcmp(detail, "The password is too short")) ||
              (0 == strncmp(detail, "The password is shorter than ", 29)))
	  {
              pfw_sms_appdata_ptr->reasonCode = BadPasswordTooShort;
	  }
          if ((0 == strcmp(detail, "The password does not contain enough character classes")) ||
              (0 == strncmp(detail, "The password contains less than ", 32)))
          {
              pfw_sms_appdata_ptr->reasonCode = BadPasswordNotEnoughCharacterClasses;
          }
          if ((0 == strcmp(detail, "The password contains too long of a monotonic character sequence")) ||
              (0 == strncmp(detail, "The password contains monotonic sequence longer than ", 53)))
          {
              pfw_sms_appdata_ptr->reasonCode = BadPasswordTooLongMonotonicSequence;
          }
          if (0 == strcmp(detail, "No password supplied"))
          {
              pfw_sms_appdata_ptr->reasonCode = BadPasswordNoPasswordSupplied;
          }
	  if (0 == strncmp(detail, "The password fails the dictionary check", 39))
	  {
	      pfw_sms_appdata_ptr->reasonCode = BadPasswordDictionaryCheck;
	  }
      } else if ((0 == strncmp(msg_ptr[msg].msg, "Username ", 9)) &&
		 (0 != strstr(msg_ptr[msg].msg, " / Password ")) &&
		 (0 != strstr(msg_ptr[msg].msg, " exceeds IPMI 16/20 limit")))
      {
	// Handle messages from https://github.com/openbmc/pam-ipmi/blob/master/src/pam_ipmicheck/pam_ipmicheck.c
        pfw_sms_appdata_ptr->reasonCode = PasswordTooLongforIPMIUser;
      }
      // Ignore other messges, such as "You are required to change your password immediately (administrator enforced)."
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
static void common_authentication_handler(const char *username, const char *password, bool *authenticated, bool *passwordChangeRequired, bool *operationAllowed)
{
    *authenticated = false;
    *passwordChangeRequired = false;
    *operationAllowed = false;

    struct pfw_sms_pam_appdata appdata = {password, PasswordChangeFailedUnknownReason};
    char* appData = (char*)&appdata;
    const struct pam_conv pamConversation = {pamConversationFunction,
                                             appData};
    pam_handle_t* pamHandle = NULL; // this gets set by pam_start
    int retval = pam_start("ibm-pfw-sms-menu", username, &pamConversation,
                           &pamHandle);
    if (retval != PAM_SUCCESS)
    {
        return; // *authenticated = false
    }

    retval = pam_authenticate(pamHandle,
                              PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
    if (retval != PAM_SUCCESS)
    {
        pam_end(pamHandle, retval); // ignore retval
        return;  // *authenticated = false
    }

    *authenticated = true;

    /* check that the account is healthy */
    retval = pam_acct_mgmt(pamHandle, PAM_DISALLOW_NULL_AUTHTOK);
    if ((retval != PAM_SUCCESS) && (retval != PAM_NEW_AUTHTOK_REQD))
    {
	pam_end(pamHandle, retval); // ignore retval
        return;  // *operationAllowed = false
    }
    *passwordChangeRequired = (retval == PAM_NEW_AUTHTOK_REQD);
    *operationAllowed = (! *passwordChangeRequired);

    retval = pam_end(pamHandle, retval); // ignore retval
}

void authenticate(const char *username, const char *password, bool *authenticated, bool *passwordChangeRequired, bool *operationAllowed)
{
  common_authentication_handler(username, password, authenticated, passwordChangeRequired, operationAllowed);
}

enum changePasswordReasonCode
changePassword(const char *username, const char *currentPassword, const char *newPassword)
{
    bool passwordChangeRequired = false;
    bool operationAllowed = false;
    bool authenticated = false;
    common_authentication_handler(username, currentPassword, &authenticated, &passwordChangeRequired, &operationAllowed);
    if (!authenticated)
    {
        return NotAuthenticated;
    }

    struct pfw_sms_pam_appdata appdata = {newPassword, PasswordChangeFailedUnknownReason};
    char* appData = (char*)&appdata;
    const struct pam_conv pamConversation = {pamConversationFunction,
                                             appData};
    pam_handle_t* pamHandle = NULL; // this gets set by pam_start
    int retval = pam_start("ibm-pfw-sms-menu", username, &pamConversation,
                           &pamHandle);
    if (retval != PAM_SUCCESS)
    {
        return PAMStartFailed;
    }

    retval = pam_chauthtok(pamHandle, PAM_SILENT);
    if (retval != PAM_SUCCESS)
    {
        pam_end(pamHandle, retval); // ignore retval
        return appdata.reasonCode;
    }

    retval = pam_end(pamHandle, retval); // ignore retval
    return PasswordChangeSuccessful;
}

