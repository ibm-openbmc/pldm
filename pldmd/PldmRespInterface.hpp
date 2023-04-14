#pragma once

#include <memory>
#include <vector>
namespace pldm
{
namespace Response_api
{

class Transport
{
  public:
    Transport(int socketFd) : sockFd(socketFd)
    {}
    int sendPLDMRespMsg(Response& response)
    {
        struct iovec iov[2]{};
        struct msghdr msg
        {};

        iov[0].iov_base = &requestMsg[0];
        iov[0].iov_len = sizeof(requestMsg[0]) + sizeof(requestMsg[1]);
        iov[1].iov_base = response.data();
        iov[1].iov_len = response.size();
        msg.msg_iov = iov;
        msg.msg_iovlen = sizeof(iov) / sizeof(iov[0]);

        return sendmsg(sockFd, &msg, 0);
    }
    void setRequestMsgRef(std::vector<uint8_t>& reqMsg)
    {
        requestMsg = reqMsg;
    }

  private:
    int sockFd;
    std::vector<uint8_t> requestMsg;
};

struct Interfaces
{
    std::unique_ptr<Transport> transport{};
};

} // namespace Response_api

} // namespace pldm