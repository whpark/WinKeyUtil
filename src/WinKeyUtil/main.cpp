#include "pch.h"
#include "WndMain.h"
#include <QtWidgets/QApplication>

int main(int argc, char* argv[]) {
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);	// lives in the system tray

	xWndMain window;
#ifdef _DEBUG
	window.show();
#endif
	return app.exec();
}
