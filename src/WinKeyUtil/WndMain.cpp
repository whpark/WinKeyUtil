#include "pch.h"
#include "WndMain.h"
#include "WOL.h"

using namespace std::literals;

xWndMain::xWndMain(QWidget* parent) : QMainWindow(parent) {
	ui.setupUi(this);

	LoadSettings();

	// Keys
	connect(ui.chkVolumeKey, &QCheckBox::toggled, this, [this](bool b) { keyhook::g_options.bVolumeKey = b; SaveSettings(); });
	connect(ui.chkAutoShift, &QCheckBox::toggled, this, [this](bool b) { keyhook::g_options.bAutoShift = b; SaveSettings(); });
	connect(ui.chkMouseJump, &QCheckBox::toggled, this, [this](bool b) { keyhook::g_options.bMouseJump = b; SaveSettings(); });
	connect(ui.spinMinRepeat, &QSpinBox::valueChanged, this, [this](int v) { keyhook::g_options.minRepeatCount = v; SaveSettings(); });

	// Receiver
	connect(ui.chkReceiver, &QCheckBox::toggled, this, &xWndMain::SaveSettings);
	connect(ui.edtHost, &QLineEdit::editingFinished, this, [this] { m_receiver.m_host = ui.edtHost->text().trimmed().toStdString(); SaveSettings(); });
	connect(ui.cmbZone, &QComboBox::currentIndexChanged, this, [this](int i) {
		m_receiver.m_zone = i == 0 ? xReceiver::eZone::main : xReceiver::eZone::zone2;
		SaveSettings();
		ReceiverVolume(0);
	});
	connect(ui.btnVolumeUp, &QPushButton::clicked, this, [this] { ReceiverVolume(+10); });
	connect(ui.btnVolumeDown, &QPushButton::clicked, this, [this] { ReceiverVolume(-10); });
	connect(ui.btnPowerOn, &QPushButton::clicked, this, [this] { ReceiverPower(true); });
	connect(ui.btnPowerOff, &QPushButton::clicked, this, [this] { ReceiverPower(false); });

	// WOL
	connect(ui.btnWakeUp, &QPushButton::clicked, this, &xWndMain::WakeOnLan);
	connect(ui.edtMAC, &QLineEdit::editingFinished, this, &xWndMain::SaveSettings);
	connect(ui.edtBroadcast, &QLineEdit::editingFinished, this, &xWndMain::SaveSettings);

	// Key Generator
	connect(ui.chkGenerate, &QCheckBox::toggled, this, &xWndMain::SetGenerator);
	connect(ui.spinInterval, &QSpinBox::valueChanged, this, [this](int sec) {
		m_timerGenerator.setInterval(sec * 1000);
		SaveSettings();
	});
	connect(&m_timerGenerator, &QTimer::timeout, this, [] { keyhook::SendKey(VK_VOLUME_DOWN, 0); });

	connect(ui.btnQuit, &QPushButton::clicked, this, &xWndMain::Quit);

	// Tray
	auto* menu = new QMenu(this);
	menu->addAction("Show Window", this, &xWndMain::ShowAndActivate);
	menu->addSeparator();
	menu->addAction("Receiver Power On", this, [this] { ReceiverPower(true); });
	menu->addAction("Receiver Standby", this, [this] { ReceiverPower(false); });
	menu->addAction("Receiver Volume Up", this, [this] { ReceiverVolume(+10); });
	menu->addAction("Receiver Volume Down", this, [this] { ReceiverVolume(-10); });
	menu->addSeparator();
	auto* actAutoShift = menu->addAction("Auto Shift");
	actAutoShift->setCheckable(true);
	connect(menu, &QMenu::aboutToShow, this, [this, actAutoShift] { actAutoShift->setChecked(ui.chkAutoShift->isChecked()); });
	connect(actAutoShift, &QAction::triggered, ui.chkAutoShift, &QCheckBox::setChecked);
	menu->addSeparator();
	menu->addAction("Quit", this, &xWndMain::Quit);
	m_tray.setIcon(windowIcon());
	m_tray.setToolTip("WinKeyUtil");
	m_tray.setContextMenu(menu);
	connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
		if (reason == QSystemTrayIcon::DoubleClick)
			ShowAndActivate();
	});
	m_tray.show();

	if (!keyhook::Install([this](keyhook::eEvent e) { OnHookEvent(e); }))
		ui.statusBar->showMessage("Failed to install keyboard hook");

	if (ui.chkReceiver->isChecked())
		ReceiverVolume(0);
}

xWndMain::~xWndMain() {
	keyhook::Uninstall();
}

void xWndMain::LoadSettings() {
	auto& opt = keyhook::g_options;
	QSignalBlocker b1(ui.chkVolumeKey), b2(ui.chkAutoShift), b3(ui.chkMouseJump), b4(ui.spinMinRepeat), b5(ui.cmbZone), b6(ui.spinInterval);

	opt.bVolumeKey = m_reg.value("misc/SendSpeakerVolumeKey", true).toBool();
	opt.bAutoShift = m_reg.value("misc/AutoShift", true).toBool();
	opt.bMouseJump = m_reg.value("misc/MouseJump", true).toBool();
	opt.minRepeatCount = std::clamp(m_reg.value("misc/MinRepeatCount", 2).toInt(), 2, 100);
	ui.chkVolumeKey->setChecked(opt.bVolumeKey);
	ui.chkAutoShift->setChecked(opt.bAutoShift);
	ui.chkMouseJump->setChecked(opt.bMouseJump);
	ui.spinMinRepeat->setValue(opt.minRepeatCount);

	ui.chkReceiver->setChecked(m_reg.value("receiver/Enabled", true).toBool());
	m_receiver.m_host = m_reg.value("receiver/Host", QString::fromStdString(m_receiver.m_host)).toString().toStdString();
	m_receiver.m_zone = m_reg.value("receiver/Zone", 0).toInt() == 0 ? xReceiver::eZone::main : xReceiver::eZone::zone2;
	ui.edtHost->setText(QString::fromStdString(m_receiver.m_host));
	ui.cmbZone->setCurrentIndex((int)m_receiver.m_zone);

	ui.edtMAC->setText(m_reg.value("wol/MAC").toString());
	ui.edtBroadcast->setText(m_reg.value("wol/Broadcast", "192.168.10.255").toString());

	ui.spinInterval->setValue(m_reg.value("generator/Interval", 230).toInt());
	m_timerGenerator.setInterval(ui.spinInterval->value() * 1000);
}

void xWndMain::SaveSettings() {
	m_reg.setValue("misc/SendSpeakerVolumeKey", ui.chkVolumeKey->isChecked());
	m_reg.setValue("misc/AutoShift", ui.chkAutoShift->isChecked());
	m_reg.setValue("misc/MouseJump", ui.chkMouseJump->isChecked());
	m_reg.setValue("misc/MinRepeatCount", ui.spinMinRepeat->value());
	m_reg.setValue("receiver/Enabled", ui.chkReceiver->isChecked());
	m_reg.setValue("receiver/Host", ui.edtHost->text().trimmed());
	m_reg.setValue("receiver/Zone", ui.cmbZone->currentIndex());
	m_reg.setValue("wol/MAC", ui.edtMAC->text().trimmed());
	m_reg.setValue("wol/Broadcast", ui.edtBroadcast->text().trimmed());
	m_reg.setValue("generator/Interval", ui.spinInterval->value());
}

void xWndMain::OnHookEvent(keyhook::eEvent e) {
	using keyhook::eEvent;
	switch (e) {
	case eEvent::KeyTouched:
		// restart idle countdown
		if (m_timerGenerator.isActive())
			m_timerGenerator.start();
		break;
	case eEvent::VolumeUp:
	case eEvent::VolumeDown:
		// network I/O must not run inside the hook
		QMetaObject::invokeMethod(this, [this, bUp = e == eEvent::VolumeUp] { SendVolume(bUp); }, Qt::QueuedConnection);
		break;
	case eEvent::GeneratorOn:
	case eEvent::GeneratorOff:
		QMetaObject::invokeMethod(ui.chkGenerate, [this, b = e == eEvent::GeneratorOn] { ui.chkGenerate->setChecked(b); SetGenerator(b); }, Qt::QueuedConnection);
		break;
	case eEvent::WakeOnLan:
		QMetaObject::invokeMethod(this, [this] { WakeOnLan(); }, Qt::QueuedConnection);
		break;
	}
}

void xWndMain::SendVolume(bool bUp) {
	if (ui.chkReceiver->isChecked()) {
		ReceiverVolume(bUp ? +10 : -10);
		return;
	}
	keyhook::SendKey(bUp ? VK_VOLUME_UP : VK_VOLUME_DOWN, 0);
}

void xWndMain::ReceiverVolume(int diff) {
	m_worker.Push([this, receiver = m_receiver, diff] {
		auto volume = receiver.ChangeVolume(diff);
		QMetaObject::invokeMethod(this, [this, volume] { OnReceiverVolume(volume); }, Qt::QueuedConnection);
	});
}

void xWndMain::ReceiverPower(bool bOn) {
	m_worker.Push([this, receiver = m_receiver, bOn] {
		if (!receiver.Power(bOn))
			QMetaObject::invokeMethod(this, [this] { ui.statusBar->showMessage("Receiver : no response", 3000); }, Qt::QueuedConnection);
	});
}

void xWndMain::OnReceiverVolume(std::optional<int> volume) {
	if (!volume) {
		ui.statusBar->showMessage("Receiver : no response", 3000);
		return;
	}
	auto str = QString::number(*volume / 10., 'f', 1);
	if (ui.lblVolume->text() != str) {
		ui.lblVolume->setText(str);
		m_box.ShowText(str);
	}
}

void xWndMain::WakeOnLan() {
	SaveSettings();
	std::string err;
	if (SendWakeOnLan(ui.edtMAC->text().trimmed().toStdString(), ui.edtBroadcast->text().trimmed().toStdString(), &err)) {
		m_box.ShowText("WOL");
		ui.statusBar->showMessage("WOL packet sent", 3000);
	}
	else {
		ui.statusBar->showMessage("WOL : " + QString::fromStdString(err), 5000);
		m_box.ShowText("WOL failed");
	}
}

void xWndMain::SetGenerator(bool bOn) {
	if (bOn == m_timerGenerator.isActive())
		return;
	if (bOn)
		m_timerGenerator.start();
	else
		m_timerGenerator.stop();
	m_box.ShowText(bOn ? "VolDn" : "off");
}

void xWndMain::ShowAndActivate() {
	showNormal();
	raise();
	activateWindow();
}

void xWndMain::Quit() {
	m_bQuit = true;
	close();
	qApp->quit();
}

void xWndMain::closeEvent(QCloseEvent* event) {
	if (!m_bQuit and m_tray.isVisible()) {
		// minimize to tray
		hide();
		event->ignore();
		return;
	}
	SaveSettings();
	QMainWindow::closeEvent(event);
}
