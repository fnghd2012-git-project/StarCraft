#pragma once

#include "GameData.h"

string MakeServerInfo(const ServerConfig& config, const MapConfig& map, const GameData& data);
void RunTcpServer(const ServerConfig& config, const MapConfig& map, const GameData& data, int runSeconds);
