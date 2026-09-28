#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>

// On-screen notification (outlined text, fades out)
class xNotifyBox : public QWidget {
	Q_OBJECT

public:
	explicit xNotifyBox(QWidget* parent = nullptr);

	void ShowText(QString const& text, std::chrono::milliseconds dur = std::chrono::milliseconds(1500));

protected:
	QString m_text;
	QTimer m_timer;
	QElapsedTimer m_elapsed;
	std::chrono::milliseconds m_dur{};

	void paintEvent(QPaintEvent* event) override;
	void OnTimer();
};
