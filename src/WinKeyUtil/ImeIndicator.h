#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>

// Shows IME state ("ㅎ" : Korean, "A" : English) next to the caret of the foreground window
class xImeIndicator : public QWidget {
	Q_OBJECT

public:
	explicit xImeIndicator(QWidget* parent = nullptr);

	void SetEnabled(bool bOn);
	void SetBackground(QColor const& color);
	void SetOpacity(double opacity);	// 0.0 ~ 1.0
	void SetOffset(QPoint const& offset);	// relative to caret (bottom-left), physical pixels
	void SetInterval(int ms);	// 10 ~ 10000
	void SetShowEnglish(bool bShow);
	void SetBoxSize(int px);	// 4 ~ 32, physical pixels

protected:
	QTimer m_timer;
	QColor m_background{0, 0, 128};
	QPoint m_offset{0, 2};
	bool m_bShowEnglish{true};
	int m_size{20};
	std::optional<bool> m_bKorean;	// nullopt : hidden

	void OnTimer();
	void paintEvent(QPaintEvent* event) override;
};
