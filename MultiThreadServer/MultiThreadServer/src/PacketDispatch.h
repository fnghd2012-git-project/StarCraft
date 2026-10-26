#pragma once
#include "GameLogic.h"

// 수신 누적 버퍼에서 완성된 패킷만 처리하고 남은 조각은 유지한다.
void HandleClientPackets(string& recvBuffer,
                         bool& versionChecked,
                         const shared_ptr<stGameChannel>& sp_channel,
                         const stServerConfig& config,
                         const string& serverInfo,
                         CGameLogic& game);
