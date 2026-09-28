#pragma once

#include <optional>
#include <string>
#include <string_view>

// Yamaha AV receiver control (YamahaRemoteControl XML API). Blocking calls - use from a worker thread.
class xReceiver {
public:
	enum class eZone { main = 0, zone2 };

	std::string m_host{"192.168.10.11"};
	eZone m_zone{eZone::main};

	// diff in 0.1dB (0 : just read). returns current volume
	std::optional<int> ChangeVolume(int diff) const;
	bool Power(bool bOn) const;

protected:
	std::string_view ZoneName() const { return m_zone == eZone::main ? "Main_Zone" : "Zone_2"; }
	std::optional<std::string> Post(std::string_view xml) const;
};

