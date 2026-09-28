#pragma once

#include <atomic>
#include <functional>

// Low-level keyboard hook (ported from KeyVolume)
//  - R-Ctrl + R-Shift + Up/Down         : volume up/down
//  - R-Ctrl + R-Shift + R-Alt + '[' / ']' : key generator on/off
//  - R-Ctrl + R-Shift + R-Alt + '\'     : wake on lan
//  - Ctrl + Alt + Shift + 1..9          : move cursor to monitor #
//  - Auto Shift                         : hold a key to get its shifted char
namespace keyhook {

	struct sOptions {
		std::atomic<bool> bVolumeKey{true};
		std::atomic<bool> bAutoShift{true};
		std::atomic<bool> bMouseJump{true};
		std::atomic<int> minRepeatCount{2};
	};
	inline sOptions g_options;

	enum class eEvent { VolumeUp, VolumeDown, GeneratorOn, GeneratorOff, WakeOnLan, KeyTouched };

	// handler is called from the hook (main thread). It must return quickly - queue heavy work.
	bool Install(std::function<void(eEvent)> handler);
	void Uninstall();

	void SendKey(WORD vk, WORD scanCode);

}	// namespace keyhook
