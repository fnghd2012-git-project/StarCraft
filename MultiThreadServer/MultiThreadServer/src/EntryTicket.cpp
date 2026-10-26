#include "Network.h"
#include <bcrypt.h>
#include "Config.h"
#include "EntryTicket.h"

namespace
{
void CheckCrypto(NTSTATUS status)
{
    if (status < 0)
    {
        throw runtime_error("Windows cryptography failed");
    }
}
} // namespace

CEntryTicket::CEntryTicket(const fs::path& keyFile)
{
    const CIniFile ini(keyFile);
    const string hex = ini.Get("Cluster", "Key");
    if (hex.size() != 64)
    {
        throw runtime_error("Cluster key must contain 64 hex characters");
    }
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        unsigned byte = 0;
        const auto result = from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
        if (result.ec != errc{} || result.ptr != hex.data() + i + 2)
        {
            throw runtime_error("Invalid cluster key");
        }
        m_key += static_cast<char>(byte);
    }
    CheckCrypto(BCryptOpenAlgorithmProvider(
        &m_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
}

CEntryTicket::~CEntryTicket()
{
    if (m_algorithm)
    {
        BCryptCloseAlgorithmProvider(m_algorithm, 0);
    }
}

string CEntryTicket::Sign(const string& bytes) const
{
    string signature(32, '\0');
    CheckCrypto(BCryptHash(m_algorithm,
                           reinterpret_cast<PUCHAR>(const_cast<char*>(m_key.data())),
                           static_cast<ULONG>(m_key.size()),
                           reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                           static_cast<ULONG>(bytes.size()),
                           reinterpret_cast<PUCHAR>(signature.data()),
                           32));
    return signature;
}

string CEntryTicket::NewId()
{
    array<unsigned char, 16> bytes{};
    CheckCrypto(BCryptGenRandom(
        nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    constexpr char hex[] = "0123456789abcdef";
    string result;
    for (const auto byte : bytes)
    {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}

string CEntryTicket::SignRelay(const string& role,
                               const string& serverId,
                               const string& clientNonce,
                               const string& serverNonce) const
{
    return Sign("chat-relay:" + role + ":" + serverId + ":" + clientNonce + ":" + serverNonce);
}

bool CEntryTicket::VerifyRelay(const string& role,
                               const string& serverId,
                               const string& clientNonce,
                               const string& serverNonce,
                               const string& signature) const
{
    if (signature.size() != 32)
    {
        return false;
    }
    const string expected = SignRelay(role, serverId, clientNonce, serverNonce);
    unsigned difference = 0;
    for (size_t i = 0; i < signature.size(); ++i)
    {
        difference |= static_cast<unsigned char>(signature[i]) ^ static_cast<unsigned char>(expected[i]);
    }
    return difference == 0;
}

long long CEntryTicket::NowSeconds()
{
    return chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();
}

wire::WorldTicket CEntryTicket::Issue(const wire::UnitState& unit,
                                      const string& destination,
                                      const string& address,
                                      int port,
                                      long long reentryAt) const
{
    wire::EntryClaims claims;
    claims.set_player_id(unit.player_id());
    claims.set_name(unit.name());
    claims.set_game_server(unit.game_server());
    claims.set_unit_index(unit.unit_index());
    claims.set_expires_at(NowSeconds() + 30);
    claims.set_nonce(NewId());
    claims.set_destination(destination);
    claims.set_reentry_at(reentryAt);
    wire::WorldTicket ticket;
    if (!claims.SerializeToString(ticket.mutable_claims()))
    {
        throw runtime_error("Ticket serialization failed");
    }
    ticket.set_signature(Sign(ticket.claims()));
    ticket.set_address(address);
    ticket.set_port(port);
    return ticket;
}

wire::EntryClaims CEntryTicket::Verify(const wire::EnterWorldRequest& request) const
{
    if (request.claims().empty() || request.claims().size() > 512 || request.signature().size() != 32)
    {
        throw runtime_error("Invalid entry ticket");
    }
    // 서명을 먼저 비교한 뒤 본문을 해석한다. 실패 위치에 따라 일찍 반환하지 않는다.
    const string signature = Sign(request.claims());
    unsigned difference = 0;
    for (size_t i = 0; i < signature.size(); ++i)
    {
        difference |=
            static_cast<unsigned char>(signature[i]) ^ static_cast<unsigned char>(request.signature()[i]);
    }
    if (difference != 0)
    {
        throw runtime_error("Entry ticket signature mismatch");
    }
    wire::EntryClaims claims;
    const auto now = NowSeconds();
    if (!claims.ParseFromString(request.claims()) || claims.player_id().size() != 32 ||
        claims.nonce().size() != 32 || claims.name().empty() || claims.name().size() > 32 ||
        claims.expires_at() <= now || claims.expires_at() > now + 35)
    {
        throw runtime_error("Invalid or expired entry ticket");
    }
    return claims;
}
