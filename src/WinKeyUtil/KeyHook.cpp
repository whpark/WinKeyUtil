#include "pch.h"
#include "KeyHook.h"

#include <imm.h>
#pragma comment(lib, "imm32.lib")

namespace keyhook {

	namespace {
		HHOOK s_hHook{};
		std::function<void(eEvent)> s_handler;

		void Notify(eEvent e) {
			if (s_handler)
				s_handler(e);
		}

		bool IsDown(int vk) { return GetKeyState(vk) < 0; }

		// R-Ctrl + R-Shift (+ R-Alt), without Win/Apps
		bool IsRightCombo(bool bAlt) {
			return IsDown(VK_RSHIFT) and IsDown(VK_RCONTROL)
				and (bAlt ? IsDown(VK_RMENU) : !IsDown(VK_MENU))
				and !IsDown(VK_LWIN) and !IsDown(VK_RWIN) and !IsDown(VK_APPS);
		}

		void MouseJump(int iMonitor) {
			struct sParam { int iTarget; int iCur{}; } param{iMonitor};
			EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR, HDC, LPRECT pRect, LPARAM lParam) -> BOOL {
				auto& p = *std::bit_cast<sParam*>(lParam);
				if (p.iCur++ != p.iTarget)
					return TRUE;
				// Send WM_MOUSEMOVE first so the cursor gets drawn
				INPUT input{ .type = INPUT_MOUSE };
				input.mi.dx = input.mi.dy = 10;
				input.mi.dwFlags = MOUSEEVENTF_MOVE;
				SendInput(1, &input, sizeof(input));
				SetCursorPos((pRect->left + pRect->right) / 2, (pRect->top + pRect->bottom) / 2);
				return FALSE;
			}, std::bit_cast<LPARAM>(&param));
		}

		LRESULT AutoShift(WPARAM wParam, KBDLLHOOKSTRUCT const& kb) {
			static std::map<DWORD, int> keyTable;
			static std::map<DWORD, int> const keyMapEx = {
				{VK_OEM_3, '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
				{'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {VK_OEM_MINUS, '_'}, {VK_OEM_PLUS, '+'},
				{VK_OEM_4, '{'}, {VK_OEM_6, '}'}, {VK_OEM_5, '|'}, {VK_OEM_1, ':'}, {VK_OEM_7, '"'},
				{VK_OEM_COMMA, '<'}, {VK_OEM_PERIOD, '>'}, {VK_OEM_2, '?'},
			};

			if (kb.flags & LLKHF_LOWER_IL_INJECTED)
				return 0;
			auto const scanCode = kb.scanCode;
			if (!scanCode)
				return 0;

			bool bKeyDown{}, bModified{}, bKorean{};
			switch (wParam) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				bKeyDown = true;
				bModified = IsDown(VK_SHIFT) or IsDown(VK_CONTROL) or IsDown(VK_MENU) or IsDown(VK_LWIN) or IsDown(VK_RWIN);
				if (auto hWnd = GetForegroundWindow()) {
					auto hIME = ImmGetDefaultIMEWnd(hWnd);
					if (::SendMessage(hIME, WM_IME_CONTROL, 0x0005, 0) != 0
						and (::SendMessage(hIME, WM_IME_CONTROL, 0x0001, 0) & IME_CMODE_HANGUL))
						bKorean = true;
				}
				break;
			}

			if (!bKeyDown or bModified or bKorean) {
				keyTable[scanCode] = 0;
				return 0;
			}

			int converted{};
			auto const vk = kb.vkCode;
			if (vk >= 'A' and vk <= 'Z')
				converted = (int)vk;
			else if (auto iter = keyMapEx.find(vk); iter != keyMapEx.end())
				converted = iter->second;

			auto& count = keyTable[scanCode];
			if (converted) {
				int const minRepeat = g_options.minRepeatCount;
				if (count < minRepeat) {
					count++;
				}
				else if (count == minRepeat) {
					count += 2;	// no more repeat
					SendKey(VK_BACK, (WORD)MapVirtualKey(VK_BACK, MAPVK_VK_TO_VSC));
					SendKey(0, (WORD)converted);
				}
			}
			return count > 1 ? 1 : 0;
		}

		LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
			if (code < 0 or !lParam)
				return CallNextHookEx(nullptr, code, wParam, lParam);

			auto const& kb = *reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);

			if (wParam == WM_KEYDOWN) {
				Notify(eEvent::KeyTouched);

				if (g_options.bVolumeKey and (kb.vkCode == VK_UP or kb.vkCode == VK_DOWN) and IsRightCombo(false)) {
					Notify(kb.vkCode == VK_UP ? eEvent::VolumeUp : eEvent::VolumeDown);
					return -1;
				}
				if ((kb.vkCode == VK_OEM_4 or kb.vkCode == VK_OEM_6) and IsRightCombo(true)) {	// '[' ']'
					Notify(kb.vkCode == VK_OEM_4 ? eEvent::GeneratorOn : eEvent::GeneratorOff);
					return -1;
				}
				if (kb.vkCode == VK_OEM_5 and IsRightCombo(true)) {	// '\'
					Notify(eEvent::WakeOnLan);
					return -1;
				}
				if (g_options.bMouseJump
					and kb.vkCode >= '1' and kb.vkCode <= '9'
					and (int)(kb.vkCode - '1') < GetSystemMetrics(SM_CMONITORS)
					and IsDown(VK_CONTROL) and IsDown(VK_MENU) and IsDown(VK_SHIFT)
					and !IsDown(VK_APPS) and !IsDown(VK_LWIN) and !IsDown(VK_RWIN))
				{
					MouseJump(kb.vkCode - '1');
				}
			}

			if (g_options.bAutoShift) {
				if (auto l = AutoShift(wParam, kb))
					return l;
			}

			return CallNextHookEx(nullptr, code, wParam, lParam);
		}
	}

	bool Install(std::function<void(eEvent)> handler) {
		s_handler = std::move(handler);
		if (!s_hHook)
			s_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
		return s_hHook != nullptr;
	}

	void Uninstall() {
		if (s_hHook)
			UnhookWindowsHookEx(s_hHook);
		s_hHook = nullptr;
		s_handler = {};
	}

	void SendKey(WORD vk, WORD scanCode) {
		INPUT input{ .type = INPUT_KEYBOARD };
		input.ki.wVk = vk;
		input.ki.wScan = scanCode;
		input.ki.dwFlags = vk == 0 ? KEYEVENTF_UNICODE : 0;
		SendInput(1, &input, sizeof(input));
	}

}	// namespace keyhook
