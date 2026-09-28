# WinKeyUtil

Keyboard utility living in the system tray (successor of KeyVolume, rewritten with Qt UI).

* Volume Control
	- Volume UP / DOWN : R-Ctrl + R-Shift + Up / Down
	- Optionally controls a Yamaha AV receiver (YamahaRemoteControl XML API, host configurable)

* Auto Shift
	- Upper case character without pressing Shift : hold 'a' longer -> 'A'

* Mouse Jump
	- Ctrl + Alt + Shift + 1..9 : move cursor to the corresponding monitor

* Wake on LAN
	- R-Ctrl + R-Shift + R-Alt + '\'

* IME Indicator
	- Shows the IME state next to the text caret of the foreground window : 'ㅎ' (Korean) / 'A' (English)
	- Option to show only in Korean mode (hide 'A')
	- Settings : background color, opacity (5 ~ 100 %), size (4 ~ 32 px), offset X / Y from caret (px), polling interval (10 ~ 10,000 ms)
	- Caret detection : Win32 caret -> MSAA (OBJID_CARET) -> UI Automation (TextPattern2 caret range / TextPattern selection)
	- Diagnostic trace is written to OutputDebugString with '[ImeIndicator]' prefix (view with DebugView)

Qt is used for UI only. Hook, receiver and WOL logic are plain C++ / Win32.

## License

MIT License. See [LICENSE.txt](LICENSE.txt).
