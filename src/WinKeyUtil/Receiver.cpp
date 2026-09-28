#include "pch.h"
#include "Receiver.h"

#include <format>

#ifndef _WIN32_WINNT
#	define _WIN32_WINNT 0x0A00
#endif
#include <asio.hpp>

std::optional<std::string> xReceiver::Post(std::string_view xml) const {
	using asio::ip::tcp;
	try {
		asio::io_context ioc;
		tcp::socket socket(ioc);
		asio::connect(socket, tcp::resolver(ioc).resolve(m_host, "80"));

		auto req = std::format(
			"POST /YamahaRemoteControl/ctrl HTTP/1.1\r\n"
			"Host: {0}\r\n"
			"Content-Type: text/xml\r\n"
			"Referer: http://{0}/index.html?zone={1}\r\n"
			"Content-Length: {2}\r\n"
			"Connection: close\r\n"
			"\r\n"
			"{3}",
			m_host, m_zone == eZone::main ? 1 : 2, xml.size(), xml);
		asio::write(socket, asio::buffer(req));

		// read until the receiver closes the connection
		std::string response;
		asio::error_code ec;
		asio::read(socket, asio::dynamic_buffer(response), ec);
		if (ec and ec != asio::error::eof)
			return {};

		auto pos = response.find("\r\n\r\n");
		if (pos == response.npos)
			return {};
		return response.substr(pos + 4);
	}
	catch (std::exception const&) {
		return {};
	}
}

std::optional<int> xReceiver::ChangeVolume(int diff) const {
	auto zone = ZoneName();
	auto res = Post(std::format(R"(<YAMAHA_AV cmd="GET"><{0}><Volume><Lvl>GetParam</Lvl></Volume></{0}></YAMAHA_AV>)", zone));
	if (!res)
		return {};
	// <YAMAHA_AV rsp="GET" RC="0"><Zone_2><Volume><Lvl><Val>-500</Val><Exp>1</Exp><Unit>dB</Unit></Lvl></Volume></Zone_2></YAMAHA_AV>
	constexpr std::string_view tag = "<Val>";
	auto pos = res->find(tag);
	if (pos == res->npos)
		return {};
	int volume = std::atoi(res->c_str() + pos + tag.size());
	if (diff) {
		constexpr int maxVol{-10 * 10}, minVol{-80 * 10};
		volume = std::clamp(volume + diff, minVol, maxVol);
		if (!Post(std::format(R"(<YAMAHA_AV cmd="PUT"><{0}><Volume><Lvl><Val>{1}</Val><Exp>1</Exp><Unit>dB</Unit></Lvl></Volume></{0}></YAMAHA_AV>)", zone, volume)))
			return {};
	}
	return volume;
}

bool xReceiver::Power(bool bOn) const {
	return Post(std::format(R"(<YAMAHA_AV cmd="PUT"><{0}><Power_Control><Power>{1}</Power></Power_Control></{0}></YAMAHA_AV>)",
		ZoneName(), bOn ? "On" : "Standby")).has_value();
}

