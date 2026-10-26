#include "PacketDispatch.h"

namespace
{
void QueueSendPacket(const shared_ptr<stGameChannel>& sp_channel, const stServerConfig& config, string packet)
{
    // 시스템 응답도 큐에 넣는다. 실제 소켓 송신은 네트워크 쓰레드(워커)가 담당한다.
    lock_guard lock(sp_channel->sendMutex);
    if (sp_channel->pendingSendBytes + packet.size() > static_cast<size_t>(config.maxPendingSendBytes))
    {
        throw runtime_error("Client send queue full");
    }
    sp_channel->pendingSendBytes += packet.size();
    sp_channel->sendQueue.push_back(move(packet));
    if (sp_channel->notifySend && !sp_channel->sendNotifyPending)
    {
        sp_channel->sendNotifyPending = true;
        sp_channel->notifySend();
    }
}
} // namespace

// 두 전송 방식에서 같은 버전/형식 검사와 게임 명령 큐를 사용한다.
void HandleClientPackets(string& recvBuffer,
                         bool& versionChecked,
                         const shared_ptr<stGameChannel>& sp_channel,
                         const stServerConfig& config,
                         const string& serverInfo,
                         CGameLogic& game)
{
    // 완성된 패킷만 소비하고, 마지막에 남은 불완전 패킷은 수신 버퍼에 유지한다.
    size_t consumed = 0;
    while (true)
    {
        const string_view remaining(recvBuffer.data() + consumed, recvBuffer.size() - consumed);
        const size_t size = Protocol::PacketSize(remaining);
        if (size == 0 || remaining.size() < size)
        {
            break;
        }
        // 4바이트 헤더에서 종류를 읽고 본문 범위를 잡는다. 본문 역직렬화는 아직 하지 않는다.
        const auto type = static_cast<Protocol::Type>(Protocol::Read16(remaining.data() + 2));
        const string_view payload = remaining.substr(Protocol::HeaderSize, size - Protocol::HeaderSize);
        if (type == Protocol::Type::SYS_ServerVersionCheck && !versionChecked)
        {
            // 같은 버전인 연결만 다음 요청을 허용한다. 계정 로그인은 이 검사 이후 별도로 한다.
            wire::ServerVersionCheckRequest request;
            if (!request.ParseFromArray(payload.data(), static_cast<int>(payload.size())) ||
                request.client_name().empty() || request.client_name().size() > 64)
            {
                throw runtime_error("Invalid ServerVersionCheckRequest");
            }
            if (request.protocol_version() != Protocol::Version)
            {
                throw runtime_error("Server protocol version mismatch");
            }
            versionChecked = true;
            // 맵 정보는 시작할 때 한 번 직렬화했다. 모든 접속에 같은 완성 패킷을 사용한다.
            QueueSendPacket(sp_channel, config, serverInfo);
        }
        else if (type == Protocol::Type::SYS_Ping && versionChecked)
        {
            wire::Ping request;
            if (!request.ParseFromArray(payload.data(), static_cast<int>(payload.size())) ||
                request.sequence() == 0 || request.text().empty() || request.text().size() > 128)
            {
                throw runtime_error("Invalid Ping");
            }
            // 역직렬화 성공과 게임에서 허용하는 값인지는 별개다. 위에서 둘 다 확인한다.
            wire::Pong response;
            response.set_sequence(request.sequence());
            response.set_text(request.text());
            response.set_padding(request.padding());
            QueueSendPacket(sp_channel, config, Protocol::EncodeMessage(Protocol::Type::SYS_Pong, response));
        }
        else if (versionChecked &&
                 (type == Protocol::Type::CM_JoinGame || type == Protocol::Type::CM_ResumeGame ||
                  type == Protocol::Type::CM_RequestTicket || type == Protocol::Type::CM_EnterWorld ||
                  type == Protocol::Type::CM_Move || type == Protocol::Type::CM_Attack ||
                  type == Protocol::Type::CM_LeaveWorld || type == Protocol::Type::CM_Chat ||
                  type == Protocol::Type::CM_Logout || type == Protocol::Type::CM_Rank ||
                  type == Protocol::Type::CM_Skill || type == Protocol::Type::SS_RelayConnect ||
                  type == Protocol::Type::SS_RelayAuth || type == Protocol::Type::SS_RelayChat))
        {
            // 게임 요청은 메인 스레드에 전달한다. CGameLogic::Update -> Handle에서 역직렬화한다.
            game.Submit(sp_channel, type, payload);
        }
        else
        {
            throw runtime_error("Unexpected packet type/state");
        }
        consumed += size;
    }
    // 처리한 패킷만 지운다. 덜 도착한 다음 패킷은 이후 수신 바이트와 이어 붙인다.
    recvBuffer.erase(0, consumed);
}
