#include "pch.h"
#include "ImeIndicator.h"

#include <imm.h>
#pragma comment(lib, "imm32.lib")
#include <oleacc.h>
#pragma comment(lib, "oleacc.lib")
#include <UIAutomation.h>
#include <wrl/client.h>

namespace {

	using Microsoft::WRL::ComPtr;

	// IME state of the foreground thread. nullopt : no caret
	struct sCaretState {
		RECT rcCaret{};	// screen coords
		bool bKorean{};
	};

	// MSAA caret object (WPF apps e.g. Visual Studio editor, Office ...)
	std::optional<RECT> GetCaretByMSAA(HWND hwnd) {
		ComPtr<IAccessible> acc;
		if (FAILED(AccessibleObjectFromWindow(hwnd, (DWORD)OBJID_CARET, IID_PPV_ARGS(&acc))) or !acc)
			return {};
		long x{}, y{}, w{}, h{};
		VARIANT self{ .vt = VT_I4 };
		self.lVal = CHILDID_SELF;
		if (FAILED(acc->accLocation(&x, &y, &w, &h, self)) or (x == 0 and y == 0 and w == 0 and h == 0))
			return {};
		return RECT{ x, y, x + w, y + h };
	}

	// UI Automation TextPattern2 caret range (Chromium, Electron, UWP ...)
	std::optional<RECT> GetCaretByUIA() {
		static ComPtr<IUIAutomation> s_uia = [] {
			ComPtr<IUIAutomation> uia;
			CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
			return uia;
		}();
		if (!s_uia)
			return {};
		ComPtr<IUIAutomationElement> focus;
		if (FAILED(s_uia->GetFocusedElement(&focus)) or !focus)
			return {};
		// caret range : TextPattern2::GetCaretRange, or TextPattern selection (Visual Studio editor)
		ComPtr<IUIAutomationTextRange> range;
		if (ComPtr<IUIAutomationTextPattern2> text2;
			SUCCEEDED(focus->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2))) and text2)
		{
			BOOL bActive{};
			text2->GetCaretRange(&bActive, &range);
		}
		if (!range) {
			ComPtr<IUIAutomationTextPattern> text;
			if (FAILED(focus->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) or !text)
				return {};
			ComPtr<IUIAutomationTextRangeArray> sels;
			int count{};
			if (FAILED(text->GetSelection(&sels)) or !sels or FAILED(sels->get_Length(&count)) or count <= 0)
				return {};
			if (FAILED(sels->GetElement(0, &range)) or !range)
				return {};
			// collapse to start (caret position)
			range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range.Get(), TextPatternRangeEndpoint_Start);
		}

		auto getRect = [](IUIAutomationTextRange* r) -> std::optional<RECT> {
			SAFEARRAY* rects{};
			if (FAILED(r->GetBoundingRectangles(&rects)) or !rects)
				return {};
			std::optional<RECT> result;
			double* data{};
			LONG ub{-1};
			SafeArrayGetUBound(rects, 1, &ub);
			if (ub >= 3 and SUCCEEDED(SafeArrayAccessData(rects, (void**)&data))) {
				// {left, top, width, height}, ...
				result = RECT{ (LONG)data[0], (LONG)data[1], (LONG)(data[0] + data[2]), (LONG)(data[1] + data[3]) };
				SafeArrayUnaccessData(rects);
			}
			SafeArrayDestroy(rects);
			return result;
		};

		if (auto rc = getRect(range.Get()))
			return rc;

		// degenerate range has no rectangle in some providers : use the next (or previous) character
		ComPtr<IUIAutomationTextRange> ch;
		if (FAILED(range->Clone(&ch)) or !ch)
			return {};
		int moved{};
		ch->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved);
		if (moved > 0) {
			if (auto rc = getRect(ch.Get())) {
				rc->right = rc->left;	// caret at left edge of next char
				return rc;
			}
		}
		range->Clone(&ch);
		ch->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved);
		if (moved < 0) {
			if (auto rc = getRect(ch.Get())) {
				rc->left = rc->right;	// caret at right edge of previous char
				return rc;
			}
		}
		return {};
	}

	std::optional<sCaretState> GetCaretState() {
		HWND hwndFG = GetForegroundWindow();
		if (!hwndFG)
			return {};
		DWORD tid = GetWindowThreadProcessId(hwndFG, nullptr);
		GUITHREADINFO gti{ .cbSize = sizeof(gti) };
		if (!GetGUIThreadInfo(tid, &gti))
			return {};
		HWND hwndFocus = gti.hwndFocus ? gti.hwndFocus : (gti.hwndCaret ? gti.hwndCaret : hwndFG);

		sCaretState state;
		if (gti.hwndCaret) {
			POINT lt{ gti.rcCaret.left, gti.rcCaret.top }, rb{ gti.rcCaret.right, gti.rcCaret.bottom };
			ClientToScreen(gti.hwndCaret, &lt);
			ClientToScreen(gti.hwndCaret, &rb);
			state.rcCaret = { lt.x, lt.y, rb.x, rb.y };
		}
		else {
			RECT rcFG{};
			GetWindowRect(hwndFG, &rcFG);
			auto isValid = [&](std::optional<RECT> const& rc) {
				return rc and PtInRect(&rcFG, POINT{ rc->left, rc->bottom - 1 });
			};
			if (auto rc = GetCaretByMSAA(hwndFocus); isValid(rc))
				state.rcCaret = *rc;
			else if (auto rc = GetCaretByUIA(); isValid(rc))
				state.rcCaret = *rc;
			else
				return {};
		}

		// Korean keyboard layout + native (Hangul) conversion mode
		auto langID = LOWORD((UINT_PTR)GetKeyboardLayout(tid));
		if (PRIMARYLANGID(langID) == LANG_KOREAN) {
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
