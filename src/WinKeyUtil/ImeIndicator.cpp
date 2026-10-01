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

	// Diagnostics : trace of one poll. written to OutputDebugString (DebugView / VS Output) only when changed
	// used only on the worker thread (xImeIndicator::m_worker)
#ifdef _DEBUG
	std::wstring g_trace;
#endif

	template < typename ... Args >
	void Trace(std::wformat_string<Args...> fmt, Args&& ... args) {
	#ifdef _DEBUG
		g_trace += std::format(fmt, std::forward<Args>(args)...);
		g_trace += L"\n";
	#endif
	}
	void FlushTrace() {
	#ifdef _DEBUG
		static std::wstring s_last;
		if (g_trace != s_last) {
			OutputDebugStringW((L"[ImeIndicator] ----\n" + g_trace).c_str());
			s_last = g_trace;
		}
		g_trace.clear();
	#endif
	}
	std::wstring ToStr(RECT const& rc) {
		return std::format(L"({},{})-({},{})", rc.left, rc.top, rc.right, rc.bottom);
	}
	std::wstring ToStr(std::optional<RECT> const& rc) {
		return rc ? ToStr(*rc) : std::wstring(L"none");
	}
	std::wstring WndStr(HWND hwnd) {
		if (!hwnd)
			return L"null";
		wchar_t buf[256]{};
		GetClassNameW(hwnd, buf, (int)std::size(buf));
		return std::format(L"{:#x} '{}'", (UINT_PTR)hwnd, buf);
	}

	using sCaretState = xImeIndicator::sCaretState;

	// MSAA caret object (WPF apps e.g. Visual Studio editor, Office ...)
	std::optional<RECT> GetCaretByMSAA(HWND hwnd) {
		ComPtr<IAccessible> acc;
		if (HRESULT hr = AccessibleObjectFromWindow(hwnd, (DWORD)OBJID_CARET, IID_PPV_ARGS(&acc)); FAILED(hr) or !acc) {
			Trace(L"  MSAA : AccessibleObjectFromWindow hr={:#x}", (unsigned)hr);
			return {};
		}
		long x{}, y{}, w{}, h{};
		VARIANT self{ .vt = VT_I4 };
		self.lVal = CHILDID_SELF;
		HRESULT hr = acc->accLocation(&x, &y, &w, &h, self);
		Trace(L"  MSAA : accLocation hr={:#x} x={} y={} w={} h={}", (unsigned)hr, x, y, w, h);
		if (FAILED(hr) or (x == 0 and y == 0 and w == 0 and h == 0))
			return {};
		return RECT{ x, y, x + w, y + h };
	}

	// Win32 : disabled window, or read-only Edit / RichEdit control
	bool IsReadOnlyByStyle(HWND hwnd) {
		if (!hwnd)
			return false;
		if (!IsWindowEnabled(hwnd))
			return true;
		wchar_t cls[64]{};
		GetClassNameW(hwnd, cls, (int)std::size(cls));
		if (_wcsicmp(cls, L"Edit") == 0 or _wcsnicmp(cls, L"RichEdit", 8) == 0 or _wcsnicmp(cls, L"RICHEDIT", 8) == 0)
			return (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_READONLY) != 0;
		return false;
	}

	// UI Automation focused element
	ComPtr<IUIAutomationElement> GetFocusByUIA() {
		// per worker thread : released before CoUninitialize
		thread_local ComPtr<IUIAutomation> s_uia = [] {
			ComPtr<IUIAutomation> uia;
			CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
			return uia;
		}();
		if (!s_uia) {
			Trace(L"  UIA : no IUIAutomation");
			return {};
		}
		ComPtr<IUIAutomationElement> focus;
		if (HRESULT hr = s_uia->GetFocusedElement(&focus); FAILED(hr) or !focus) {
			Trace(L"  UIA : GetFocusedElement hr={:#x}", (unsigned)hr);
			return {};
		}
		return focus;
	}

	// UI Automation : focused element is disabled / read-only. unknown -> false (editable)
	bool IsReadOnlyByUIA(IUIAutomationElement* focus) {
		if (!focus)
			return false;
		if (BOOL bEnabled{TRUE}; SUCCEEDED(focus->get_CurrentIsEnabled(&bEnabled)) and !bEnabled) {
			Trace(L"  UIA : disabled");
			return true;
		}
		// ValuePattern (edit box, web document ...)
		if (ComPtr<IUIAutomationValuePattern> value;
			SUCCEEDED(focus->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) and value)
		{
			BOOL bReadOnly{};
			if (SUCCEEDED(value->get_CurrentIsReadOnly(&bReadOnly))) {
				Trace(L"  UIA : ValuePattern IsReadOnly={}", bReadOnly);
				return bReadOnly;
			}
		}
		// text attribute at the caret (rich text / code editor)
		ComPtr<IUIAutomationTextRange> range;
		if (ComPtr<IUIAutomationTextPattern2> text2;
			SUCCEEDED(focus->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2))) and text2)
		{
			BOOL bActive{};
			text2->GetCaretRange(&bActive, &range);
		}
		if (!range) {
			ComPtr<IUIAutomationTextPattern> text;
			ComPtr<IUIAutomationTextRangeArray> sels;
			int count{};
			if (SUCCEEDED(focus->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text))) and text
				and SUCCEEDED(text->GetSelection(&sels)) and sels and SUCCEEDED(sels->get_Length(&count)) and count > 0)
				sels->GetElement(0, &range);
		}
		if (range) {
			VARIANT v{};
			VariantInit(&v);
			if (SUCCEEDED(range->GetAttributeValue(UIA_IsReadOnlyAttributeId, &v))) {
				bool bReadOnly = v.vt == VT_BOOL and v.boolVal != VARIANT_FALSE;	// mixed / not supported (VT_UNKNOWN) : editable
				Trace(L"  UIA : IsReadOnlyAttribute vt={} -> {}", v.vt, bReadOnly);
				VariantClear(&v);
				return bReadOnly;
			}
		}
		return false;
	}

	// UI Automation caret (Chromium, Electron, UWP, WPF ...)
	std::optional<RECT> GetCaretByUIA(IUIAutomationElement* focus) {
		if (!focus)
			return {};
		{
			BSTR cls{}, name{}, fw{};
			CONTROLTYPEID type{};
			focus->get_CurrentClassName(&cls);
			focus->get_CurrentName(&name);
			focus->get_CurrentFrameworkId(&fw);
			focus->get_CurrentControlType(&type);
			std::wstring strName = name ? name : L"";
			if (strName.size() > 40)
				strName = strName.substr(0, 40) + L"...";
			Trace(L"  UIA : focus class='{}' type={} framework='{}' name='{}'", cls ? cls : L"", type, fw ? fw : L"", strName);
			SysFreeString(cls);
			SysFreeString(name);
			SysFreeString(fw);
		}

		auto getRect = [](IUIAutomationTextRange* r) -> std::optional<RECT> {
			SAFEARRAY* rects{};
			if (HRESULT hr = r->GetBoundingRectangles(&rects); FAILED(hr) or !rects) {
				Trace(L"    GetBoundingRectangles hr={:#x}", (unsigned)hr);
				return {};
			}
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
			Trace(L"    GetBoundingRectangles count={} rect={}", (ub + 1) / 4, ToStr(result));
			return result;
		};

		// rect of a degenerate (caret) range
		auto caretRect = [&](IUIAutomationTextRange* range) -> std::optional<RECT> {
			if (!range)
				return {};
			if (auto rc = getRect(range))
				return rc;

			// degenerate range has no rectangle in some providers : use the next (or previous) character
			ComPtr<IUIAutomationTextRange> ch;
			int moved{};
			if (SUCCEEDED(range->Clone(&ch)) and ch
				and SUCCEEDED(ch->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved)) and moved > 0)
			{
				Trace(L"   next char");
				if (auto rc = getRect(ch.Get())) {
					rc->right = rc->left;	// caret at left edge of next char
					return rc;
				}
			}
			if (SUCCEEDED(range->Clone(&ch)) and ch
				and SUCCEEDED(ch->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved)) and moved < 0)
			{
				Trace(L"   prev char");
				if (auto rc = getRect(ch.Get())) {
					rc->left = rc->right;	// caret at right edge of previous char
					return rc;
				}
			}
			return {};
		};

		// 1. TextPattern2::GetCaretRange (exact caret position)
		ComPtr<IUIAutomationTextPattern2> text2;
		HRESULT hr = focus->GetCurrentPatternAs(UIA_TextPattern2Id, IID_PPV_ARGS(&text2));
		Trace(L"  UIA : TextPattern2 hr={:#x} {}", (unsigned)hr, text2 ? L"supported" : L"not supported");
		if (SUCCEEDED(hr) and text2) {
			BOOL bActive{};
			ComPtr<IUIAutomationTextRange> range;
			hr = text2->GetCaretRange(&bActive, &range);
			Trace(L"   GetCaretRange hr={:#x} active={} range={}", (unsigned)hr, bActive, range ? L"ok" : L"null");
			if (SUCCEEDED(hr))
				if (auto rc = caretRect(range.Get()))
					return rc;
		}

		// 2. TextPattern selection start (Visual Studio editor ...)
		ComPtr<IUIAutomationTextPattern> text;
		hr = focus->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&text));
		Trace(L"  UIA : TextPattern hr={:#x} {}", (unsigned)hr, text ? L"supported" : L"not supported");
		if (FAILED(hr) or !text)
			return {};
		ComPtr<IUIAutomationTextRangeArray> sels;
		ComPtr<IUIAutomationTextRange> range;
		int count{};
		hr = text->GetSelection(&sels);
		if (sels)
			sels->get_Length(&count);
		Trace(L"   GetSelection hr={:#x} count={}", (unsigned)hr, count);
		if (FAILED(hr) or count <= 0 or FAILED(sels->GetElement(0, &range)) or !range)
			return {};
		range->MoveEndpointByRange(TextPatternRangeEndpoint_End, range.Get(), TextPatternRangeEndpoint_Start);	// collapse to start
		return caretRect(range.Get());
	}

	std::optional<sCaretState> GetCaretStateImpl() {
		HWND hwndFG = GetForegroundWindow();
		if (!hwndFG) {
			Trace(L"no foreground window");
			return {};
		}
		DWORD pid{};
		DWORD tid = GetWindowThreadProcessId(hwndFG, &pid);
		GUITHREADINFO gti{ .cbSize = sizeof(gti) };
		if (!GetGUIThreadInfo(tid, &gti)) {
			Trace(L"GetGUIThreadInfo failed. err={}", GetLastError());
			return {};
		}
		HWND hwndFocus = gti.hwndFocus ? gti.hwndFocus : (gti.hwndCaret ? gti.hwndCaret : hwndFG);
		RECT rcFG{};
		GetWindowRect(hwndFG, &rcFG);
		Trace(L"FG {} pid={} tid={} rect={}", WndStr(hwndFG), pid, tid, ToStr(rcFG));
		Trace(L"  focus {}, caret {}, rcCaret(client)={}", WndStr(gti.hwndFocus), WndStr(gti.hwndCaret), ToStr(gti.rcCaret));

		sCaretState state;
		if (gti.hwndCaret) {
			POINT lt{ gti.rcCaret.left, gti.rcCaret.top }, rb{ gti.rcCaret.right, gti.rcCaret.bottom };
			ClientToScreen(gti.hwndCaret, &lt);
			ClientToScreen(gti.hwndCaret, &rb);
			state.rcCaret = { lt.x, lt.y, rb.x, rb.y };
			Trace(L"  -> Win32 caret {}", ToStr(state.rcCaret));
			if (IsReadOnlyByStyle(hwndFocus)) {
				Trace(L"  -> read-only (style)");
				return {};
			}
		}
		else {
			if (IsReadOnlyByStyle(hwndFocus)) {
				Trace(L"  -> read-only (style)");
				return {};
			}
			auto isValid = [&](std::optional<RECT> const& rc) {
				return rc and PtInRect(&rcFG, POINT{ rc->left, rc->bottom - 1 });
			};
			auto rc = GetCaretByMSAA(hwndFocus);
			auto focus = GetFocusByUIA();
			if (isValid(rc)) {
				state.rcCaret = *rc;
				Trace(L"  -> MSAA caret {}", ToStr(state.rcCaret));
			}
			else {
				Trace(L"  MSAA rejected : {}", ToStr(rc));
				rc = GetCaretByUIA(focus.Get());
				if (!isValid(rc)) {
					Trace(L"  -> no caret (UIA {})", ToStr(rc));
					return {};
				}
				state.rcCaret = *rc;
				Trace(L"  -> UIA caret {}", ToStr(state.rcCaret));
			}
			if (IsReadOnlyByUIA(focus.Get())) {
				Trace(L"  -> read-only (UIA)");
				return {};
			}
		}

		// Korean keyboard layout + native (Hangul) conversion mode
		auto langID = LOWORD((UINT_PTR)GetKeyboardLayout(tid));
		if (PRIMARYLANGID(langID) == LANG_KOREAN) {
			HWND hwndIME = ImmGetDefaultIMEWnd(hwndFocus);
			DWORD_PTR mode{};
			LRESULT ok{};
			if (hwndIME) {
				constexpr WPARAM IMC_GETCONVERSIONMODE = 0x0001;
				ok = SendMessageTimeoutW(hwndIME, WM_IME_CONTROL, IMC_GETCONVERSIONMODE, 0, SMTO_ABORTIFHUNG, 50, &mode);
				if (ok)
					state.bKorean = (mode & IME_CMODE_NATIVE) != 0;
			}
			Trace(L"  IME lang={:#06x} imeWnd={} ok={} mode={:#x} -> {}", langID, WndStr(hwndIME), ok != 0, (unsigned)mode,
				state.bKorean ? L"Korean" : L"English");
		}
		else
			Trace(L"  IME lang={:#06x} (not Korean)", langID);
		return state;
	}

	std::optional<sCaretState> GetCaretState() {
		auto state = GetCaretStateImpl();
		FlushTrace();
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

void xImeIndicator::SetBoxSize(int px) {
	m_size = std::clamp(px, 4, 32);
}

void xImeIndicator::OnTimer() {
	// skip if previous query is still running (target app not responding ...)
	if (m_bBusy.exchange(true))
		return;
	m_worker.Push([this] {
		// COM (MTA) for MSAA / UIA on this worker thread
		thread_local struct sComInit {
			HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			~sComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
		} s_com;
		auto state = GetCaretState();
		// dropped by Qt if 'this' is destroyed
		QMetaObject::invokeMethod(this, [this, state] {
			m_bBusy = false;
			OnCaretState(state);
		}, Qt::QueuedConnection);
	});
}

void xImeIndicator::OnCaretState(std::optional<sCaretState> const& state) {
	if (!m_timer.isActive())	// disabled while querying
		return;
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
	SetWindowPos(hwnd, HWND_TOPMOST, state->rcCaret.left + m_offset.x(), state->rcCaret.bottom + m_offset.y(), m_size, m_size,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void xImeIndicator::paintEvent(QPaintEvent*) {
	if (!m_bKorean)
		return;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(m_background);
	auto radius = std::min(3.0, height() / 4.0);
	painter.drawRoundedRect(rect(), radius, radius);
	if (height() < 8)	// too small for text : color box only
		return;

	QFont font("Malgun Gothic");
	font.setPixelSize(height() * 3 / 4);
	font.setBold(true);
	painter.setFont(font);
	painter.setPen(Qt::white);
	painter.drawText(rect(), Qt::AlignCenter, *m_bKorean ? QStringLiteral(u"ㅎ") : QStringLiteral(u"A"));
}
