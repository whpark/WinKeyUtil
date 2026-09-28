#pragma once

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>
#include <atomic>
#include "TaskQueue.h"

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

	// caret position and IME state of the foreground thread
	struct sCaretState {
		RECT rcCaret{};	// screen coords
		bool bKorean{};
	};

protected:
	QTimer m_timer;
	QColor m_background{0, 0, 128};
	QPoint m_offset{0, 2};
	bool m_bShowEnglish{true};
	int m_size{20};
	std::optional<bool> m_bKorean;	// nullopt : hidden
	std::atomic_bool m_bBusy{};	// query is running on m_worker
	xTaskQueue m_worker;	// caret / IME query (MSAA, UIA calls may block). last member : joined first

	void OnTimer();
	void OnCaretState(std::optional<sCaretState> const& state);
	void paintEvent(QPaintEvent* event) override;
};
