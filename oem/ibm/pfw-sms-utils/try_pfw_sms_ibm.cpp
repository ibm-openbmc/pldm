// unit test stub
// sample invocation: try_pfw_sms_ibm admin 0penBmc0

#include <string>

#include "pfw_sms_menu.hpp"

#include <stdio.h>

using namespace ibm_pfw_sms;

int main(int argc, char* argv[])
{
    if (argc == 3)
    {
        // authenticate
        bool authenticated = false;
        bool passwordChangeRequired = false;
        bool operationAllowed = false;
        authenticate(std::string(argv[1]), std::string(argv[2]), authenticated, passwordChangeRequired,
                     operationAllowed);
        printf(
            "authenticated=%d, passwordChangeRequired=%d, operationAllowed=%d\n",
            (int)authenticated, (int)passwordChangeRequired,
            (int)operationAllowed);
        return (int)!authenticated;
    }
    else if (argc == 4)
    {
        // change password
	bool operationAllowed = false;
        enum changePasswordReasonCode rc;
        rc = changePassword(std::string(argv[1]), std::string(argv[2]), std::string(argv[3]), operationAllowed);
        printf("allowed=%d, rc=%d\n", (int)operationAllowed, rc);
        return rc;
    }
    else
    {
        printf("Required arguments: USERNAME PASSWORD [NEW_PASSWORD]\n");
    }
    return 1;
}
