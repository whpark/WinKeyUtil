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

* Key Generator (keeps PC awake)
	- R-Ctrl + R-Shift + R-Alt + '[' / ']' : on / off. Sends VolDn key when idle for the given interval.

Qt is used for UI only. Hook, receiver and WOL logic are plain C++ / Win32.

## License

MIT License. See [LICENSE.txt](LICENSE.txt).
