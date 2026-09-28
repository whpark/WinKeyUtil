#pragma once

#include <optional>
#include <string>
#include <string_view>

// Wake on LAN magic packet
bool SendWakeOnLan(std::string_view mac, std::string const& broadcast, std::string* err = nullptr);

