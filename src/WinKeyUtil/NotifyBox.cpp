#include "pch.h"
#include "NotifyBox.h"

xNotifyBox::xNotifyBox(QWidget* parent)
	: QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	resize(160, 60);
	m_timer.setInterval(20);
	connect(&m_timer, &QTimer::timeout, this, &xNotifyBox::OnTimer);
}

void xNotifyBox::ShowText(QString const& text, std::chrono::milliseconds dur) {
	m_text = text;
	m_dur = dur;
	m_elapsed.start();
	if (auto* screen = QGuiApplication::primaryScreen())
		move(screen->geometry().topLeft());
	setWindowOpacity(1.0);
	show();
	update();
	m_timer.start();
}

void xNotifyBox::OnTimer() {
	auto elapsed = m_elapsed.elapsed();
	if (elapsed >= m_dur.count()) {
		m_timer.stop();
		hide();
		return;
	}
	setWindowOpacity(1.0 - (double)elapsed / m_dur.count());
}

void xNotifyBox::paintEvent(QPaintEvent*) {
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	QFont font("Segoe UI");
	font.setPixelSize(32);

	QPainterPath path;
	QFontMetrics fm(font);
	auto rc = fm.boundingRect(m_text);
	path.addText((width() - rc.width()) / 2.0, (height() + fm.ascent() - fm.descent()) / 2.0, font, m_text);

	painter.strokePath(path, QPen(QColor(0, 0, 92), 3));
	painter.fillPath(path, Qt::white);
}
