#include "pch.h"
#include "DlgAbout.h"
#include "Version.h"

xDlgAbout::xDlgAbout(QWidget* parent) : QDialog(parent) {
	setWindowTitle("About WinKeyUtil");
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	resize(480, 460);

	auto* layout = new QVBoxLayout(this);

	// header : icon + name
	auto* header = new QHBoxLayout;
	auto* icon = new QLabel(this);
	icon->setPixmap((parent ? parent->windowIcon() : windowIcon()).pixmap(48, 48));
	header->addWidget(icon);
	auto* title = new QLabel(QString("<b style='font-size:14pt'>WinKeyUtil</b><br>Version v%1<br>Copyright &copy; 2026 Biscuit-lab whpark").arg(WINKEYUTIL_VERSION_STR), this);
	header->addWidget(title, 1);
	layout->addLayout(header);

	auto* text = new QTextBrowser(this);
	text->setOpenExternalLinks(true);
	text->setHtml(QString(R"(
<p>Keyboard utility living in the system tray.</p>

<h3>License</h3>
<p>WinKeyUtil is licensed under the <b>MIT License</b>. See LICENSE.txt.</p>
<p>THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.</p>

<h3>Third party software</h3>

<p><b>Qt %1</b> &mdash; The Qt Company Ltd. and other contributors<br>
Used under the <b>GNU Lesser General Public License v3 (LGPLv3)</b>.<br>
Qt is dynamically linked (Qt6*.dll). You may replace the Qt libraries with your own modified version
of the same major version.<br>
Qt source code : <a href="https://download.qt.io/official_releases/qt/">https://download.qt.io/official_releases/qt/</a><br>
License text : licenses\LGPL-3.0-only.txt, licenses\GPL-3.0-only.txt (<a href="https://www.gnu.org/licenses/lgpl-3.0.html">LGPLv3</a>,
<a href="https://www.gnu.org/licenses/gpl-3.0.html">GPLv3</a>)</p>

<p><b>Asio</b> &mdash; Copyright &copy; Christopher M. Kohlhoff<br>
Used under the <b>Boost Software License 1.0</b>.<br>
<a href="https://www.boost.org/LICENSE_1_0.txt">https://www.boost.org/LICENSE_1_0.txt</a></p>

<p>See THIRD_PARTY_NOTICES.txt for full notices.</p>
)").arg(qVersion()));
	layout->addWidget(text, 1);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
	auto* btnAboutQt = buttons->addButton("About Qt", QDialogButtonBox::ActionRole);
	connect(btnAboutQt, &QPushButton::clicked, this, [this] { QMessageBox::aboutQt(this); });
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	layout->addWidget(buttons);
}
