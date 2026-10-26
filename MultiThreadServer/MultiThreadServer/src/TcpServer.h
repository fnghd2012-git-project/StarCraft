#pragma once

#include "GameData.h"

string MakeServerInfo(const stServerConfig& config, const stMapConfig& map, const stGameData& data);
void RunTcpServer(const stServerConfig& config,
                  const stMapConfig& map,
                  const stGameData& data,
                  int runSeconds);
