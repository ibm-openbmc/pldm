// unit test stub
// sample invocation: try_pfw_sms_ibm admin 0penBmc0

#include "pfw_sms_menu.hpp"

#include <stdio.h>

int main(int argc, char* argv[])
{
    if (argc == 3)
    {
        // authenticate
        bool authenticated = false;
        bool passwordChangeRequired = false;
        bool operationAllowed = false;
        authenticate(argv[1], argv[2], &authenticated, &passwordChangeRequired,
                     &operationAllowed);
        printf(
            "authenticated=%d, passwordChangeRequired=%d, operationAllowed=%d\n",
            (int)authenticated, (int)passwordChangeRequired,
            (int)operationAllowed);
        return (int)!authenticated;
    }
    else if (argc == 4)
    {
        // change password
        enum changePasswordReasonCode rc;
        rc = changePassword(argv[1], argv[2], argv[3]);
        printf("change password rc=%d\n", rc);
        return rc;
    }
    else
    {
        printf("Required arguments: USERNAME PASSWORD [NEW_PASSWORD]\n");
    }
    return 1;
}
