#include "pch.h"
#include "WOL.h"

#ifndef _WIN32_WINNT
#	define _WIN32_WINNT 0x0A00
#endif

bool SendWakeOnLan(std::string_view mac, std::string const& broadcast, std::string* err) {
	auto fail = [err](char const* msg) { if (err) *err = msg; return false; };

	std::string hex;
	for (char c : mac)
		if (std::isxdigit((unsigned char)c))
			hex += c;
	if (hex.size() != 12)
		return fail("Invalid MAC address format");
	uint8_t addr[6];
	for (int i = 0; i < 6; i++)
		addr[i] = (uint8_t)std::stoi(hex.substr(i * 2, 2), nullptr, 16);

	std::vector<uint8_t> packet(6, 0xff);
	for (int i = 0; i < 16; i++)
		packet.insert(packet.end(), std::begin(addr), std::end(addr));

	try {
		using asio::ip::udp;
		asio::io_context ioc;
		udp::socket socket(ioc, udp::v4());
		socket.set_option(asio::socket_base::broadcast(true));
		socket.send_to(asio::buffer(packet), udp::endpoint(asio::ip::make_address_v4(broadcast), 9));
	}
	catch (std::exception const& e) {
		return fail(e.what());
	}
	return true;
}
