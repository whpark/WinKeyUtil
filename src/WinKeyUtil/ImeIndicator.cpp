#include "pch.h"
#include "ImeIndicator.h"

#include <imm.h>
#pragma comment(lib, "imm32.lib")

namespace {

	// IME state of the foreground thread. nullopt : no caret
	struct sCaretState {
		RECT rcCaret{};	// screen coords
		bool bKorean{};
	};

	std::optional<sCaretState> GetCaretState() {
		HWND hwndFG = GetForegroundWindow();
		if (!hwndFG)
			return {};
		DWORD tid = GetWindowThreadProcessId(hwndFG, nullptr);
		GUITHREADINFO gti{ .cbSize = sizeof(gti) };
		if (!GetGUIThreadInfo(tid, &gti) or !gti.hwndCaret)
			return {};

		sCaretState state;
		POINT lt{ gti.rcCaret.left, gti.rcCaret.top }, rb{ gti.rcCaret.right, gti.rcCaret.bottom };
		ClientToScreen(gti.hwndCaret, &lt);
		ClientToScreen(gti.hwndCaret, &rb);
		state.rcCaret = { lt.x, lt.y, rb.x, rb.y };

		// Korean keyboard layout + native (Hangul) conversion mode
		auto langID = LOWORD((UINT_PTR)GetKeyboardLayout(tid));
		if (PRIMARYLANGID(langID) == LANG_KOREAN) {
			HWND hwndFocus = gti.hwndFocus ? gti.hwndFocus : gti.hwndCaret;
			if (HWND hwndIME = ImmGetDefaultIMEWnd(hwndFocus)) {
				constexpr WPARAM IMC_GETCONVERSIONMODE = 0x0001;
				DWORD_PTR mode{};
				if (SendMessageTimeoutW(hwndIME, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG, 50, &mode))
					state.bKorean = (mode & IME_CMODE_NATIVE) != 0;
			}
		}
		return state;
	}

}

xImeIndicator::xImeIndicator(QWidget* parent)
	: QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	resize(20, 20);
	m_timer.setInterval(100);
	connect(&m_timer, &QTimer::timeout, this, &xImeIndicator::OnTimer);
}

void xImeIndicator::SetEnabled(bool bOn) {
	if (bOn)
		m_timer.start();
	else {
		m_timer.stop();
		m_bKorean.reset();
		hide();
	}
}

void xImeIndicator::SetBackground(QColor const& color) {
	m_background = color;
	update();
}

void xImeIndicator::SetOpacity(double opacity) {
	setWindowOpacity(std::clamp(opacity, 0.0, 1.0));
}

void xImeIndicator::SetOffset(QPoint const& offset) {
	m_offset = offset;
}

void xImeIndicator::SetInterval(int ms) {
	m_timer.setInterval(std::clamp(ms, 10, 10'000));
}

void xImeIndicator::SetShowEnglish(bool bShow) {
	m_bShowEnglish = bShow;
}

void xImeIndicator::OnTimer() {
	auto state = GetCaretState();
	if (!state or (!state->bKorean and !m_bShowEnglish)) {
		if (isVisible())
			hide();
		m_bKorean.reset();
		return;
	}

	if (m_bKorean != state->bKorean) {
		m_bKorean = state->bKorean;
		update();
	}
	if (!isVisible())
		show();

	// position in physical pixels (avoid Qt DPI mapping across monitors)
	auto hwnd = (HWND)winId();
	int size = qRound(20 * devicePixelRatioF());
	SetWindowPos(hwnd, HWND_TOPMOST, state->rcCaret.left + m_offset.x(), state->rcCaret.bottom + m_offset.y(), size, size,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void xImeIndicator::paintEvent(QPaintEvent*) {
	if (!m_bKorean)
		return;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(m_background);
	painter.drawRoundedRect(rect(), 3, 3);

	QFont font("Malgun Gothic");
	font.setPixelSize(height() * 3 / 4);
	font.setBold(true);
	painter.setFont(font);
	painter.setPen(Qt::white);
	painter.drawText(rect(), Qt::AlignCenter, *m_bKorean ? QStringLiteral(u"ㅎ") : QStringLiteral(u"A"));
}
