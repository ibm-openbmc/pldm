#pragma once

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <iostream>
#include <map>
#include <memory>
#include <vector>
namespace pldm
{
namespace Response_api
{
using Response = std::vector<uint8_t>;
class Transport
{
  public:
    Transport(int socketFd) : sockFd(socketFd) {}
    int sendPLDMRespMsg(Response& response, uint32_t indx)
    {
        struct iovec iov[2]{};
        struct msghdr msg
        {};

        iov[0].iov_base = &requestMap[indx][0];
        iov[0].iov_len = sizeof(requestMap[indx][0]) +
                         sizeof(requestMap[indx][1]);
        iov[1].iov_base = response.data();
        iov[1].iov_len = response.size();
        msg.msg_iov = iov;
        msg.msg_iovlen = sizeof(iov) / sizeof(iov[0]);

        int rc = sendmsg(sockFd, &msg, 0);
        if (rc < 0)
        {
            std::cerr << "sendto system call failed, RC= " << rc << "\n";
        }

        removeHeader(indx);
        return rc;
    }
    void setRequestMsgRef(std::vector<uint8_t>& reqMsg)
    {
        mRequestMsg = reqMsg;
    }

    void removeHeader(uint32_t indx)
    {
        requestMap.erase(indx);
    }

    uint32_t getUniqueKey()
    {
        uint32_t key = 0;
        for (size_t indx = 0; indx <= requestMap.size(); indx++)
        {
            if (requestMap.find(indx) == requestMap.end())
            {
                key = indx;
                break;
            }
        }
        return key;
    }
    int getRequestHeaderIndex()
    {
        int indx = getUniqueKey();
        requestMap[indx] = mRequestMsg;
        return indx;
    }

  private:
    std::vector<uint8_t> mRequestMsg;
    std::map<int, std::vector<uint8_t>> requestMap;
    int sockFd;
    static int requestID;
};

struct Interfaces
{
    std::unique_ptr<Transport> transport{};
};

} // namespace Response_api

} // namespace pldm
