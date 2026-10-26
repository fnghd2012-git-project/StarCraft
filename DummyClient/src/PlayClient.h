#pragma once
#include "ClientConnection.h"

template <class Reply>
Reply RequestMessage(CClientConnection& connection,
                     Protocol::Type type,
                     const google::protobuf::MessageLite& request,
                     Protocol::Type expected)
{
    Reply reply;
    if (!reply.ParseFromString(connection.Request(type, request, expected)))
    {
        throw runtime_error("Invalid reply protobuf");
    }
    return reply;
}

int RunPlayClient(int argc, char* argv[]);
void RunFlowCheck(const fs::path& configPath);
void RunAccountCheck(const fs::path& configPath);
void RunSkillCheck(const fs::path& configPath);
void RunCapacityCheck(const fs::path& configPath);
void RunRankingCheck(const fs::path& configPath);
