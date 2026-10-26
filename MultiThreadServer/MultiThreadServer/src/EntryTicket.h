#pragma once

#include "Packet.h"

// 게임 서버와 World만 공유하는 키로 입장권을 서명한다. 계정 인증을 대체하지는 않는다.
class CEntryTicket final
{
public:
    explicit CEntryTicket(const fs::path& keyFile);
    ~CEntryTicket();
    CEntryTicket(const CEntryTicket&) = delete;
    CEntryTicket& operator=(const CEntryTicket&) = delete;
    wire::WorldTicket Issue(const wire::UnitState& unit,
                            const string& destination,
                            const string& address,
                            int port,
                            long long reentryAt = 0) const;
    wire::EntryClaims Verify(const wire::EnterWorldRequest& request) const;
    // 입장권과 다른 접두사로 서명해 용도가 다른 서명을 재사용할 수 없게 한다.
    string SignRelay(const string& role,
                     const string& serverId,
                     const string& clientNonce,
                     const string& serverNonce) const;
    bool VerifyRelay(const string& role,
                     const string& serverId,
                     const string& clientNonce,
                     const string& serverNonce,
                     const string& signature) const;
    static string NewId();
    static long long NowSeconds();

private:
    string Sign(const string& bytes) const;
    string m_key;
    void* m_algorithm = nullptr;
};
