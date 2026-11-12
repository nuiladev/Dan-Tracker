#pragma once
#include <string>

bool startObsServer(int port, std::string (*getState)());
void stopObsServer();
