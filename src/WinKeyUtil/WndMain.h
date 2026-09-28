#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QSystemTrayIcon>
#include <QtCore/QSettings>
#include <QtCore/QTimer>
#include "ui_WndMain.h"

#include "KeyHook.h"
#include "Receiver.h"
#include "NotifyBox.h"
#include "ImeIndicator.h"
#include "TaskQueue.h"

class xWndMain : public QMainWindow {
	Q_OBJECT

public:
	xWndMain(QWidget* parent = nullptr);
	~xWndMain();

protected:
	Ui::xWndMainClass ui;
	QSettings m_reg{"WinKeyUtil", "WinKeyUtil"};
	QSystemTrayIcon m_tray;
	xReceiver m_receiver;
	xNotifyBox m_box;
	xImeIndicator m_ime;
	QColor m_colorIme{0, 0, 128};
	QTimer m_timerGenerator;
	bool m_bQuit{};
	xTaskQueue m_worker;    // receiver network I/O. last member : joined first

	void LoadSettings();
	void SaveSettings();
	void OnHookEvent(keyhook::eEvent e);
	void SendVolume(bool bUp);
	void ReceiverVolume(int diff);
	void ReceiverPower(bool bOn);
	void OnReceiverVolume(std::optional<int> volume);
	void WakeOnLan();
	void SetGenerator(bool bOn);
	void ApplyImeIndicator();
	void ShowAndActivate();
	void Quit();

	bool IsWindowsLocked();

	void closeEvent(QCloseEvent* event) override;
};

