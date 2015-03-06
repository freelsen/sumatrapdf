#include "LsDraw.h"
#include "WindowInfo.h"
#include "windows.h"

LsDraw g_lsdraw;
LsDraw * glsdraw =&g_lsdraw;

LsDraw::LsDraw()
{
}
LsDraw::~LsDraw()
{
}

void LsDraw::test(WindowInfo& win)
{
	PAINTSTRUCT ps;
	HDC hdc = BeginPaint(win.hwndCanvas, &ps);
	TextOut(hdc, 0, 0, TEXT("hello ls"), 8);
	EndPaint(win.hwndCanvas, &ps);
}