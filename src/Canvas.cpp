/* Copyright 2014 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

// utils
#include "BaseUtil.h"
#include <dwmapi.h>
#include <vssym32.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include "Dpi.h"
#include "FileUtil.h"
#include "FrameRateWnd.h"
#include "Timer.h"
#include "Touch.h"
#include "UITask.h"
#include "WinUtil.h"
// rendering engines
#include "BaseEngine.h"
#include "EngineManager.h"
#include "Doc.h"
// layout controllers
#include "SettingsStructs.h"
#include "Controller.h"
#include "DisplayModel.h"
#include "EbookController.h"
#include "GlobalPrefs.h"
#include "RenderCache.h"
#include "TextSelection.h"
#include "TextSearch.h"
// ui
#include "SumatraPDF.h"
#include "WindowInfo.h"
#include "TabInfo.h"
#include "resource.h"
#include "Canvas.h"
#include "Caption.h"
#include "Menu.h"
#include "Notifications.h"
#include "uia/Provider.h"
#include "Search.h"
#include "Selection.h"
#include "SumatraAbout.h"
#include "Tabs.h"
#include "Toolbar.h"
#include "Translations.h"

//+ls@150206;
#include "tchar.h"
#include <vector>
#include <map>
using namespace std;

// these can be global, as the mouse wheel can't affect more than one window at once
static int gDeltaPerLine = 0;
// set when WM_MOUSEWHEEL has been passed on (to prevent recursion)
static bool gWheelMsgRedirect = false;


// --------------------------------------------------------------------------
//+ls@150206;

static TCHAR pt_message[2048];
void lsdebugout(TCHAR *msg, ...)
{
//#ifdef _DEBUG
	//if (!check()) return;

	TCHAR *text = pt_message;

	va_list	argptr;
	va_start(argptr, msg);
	_vstprintf(text, msg, argptr);
	va_end(argptr);

	//if (pt_msg != NULL)
	//{
	//	pt_msg(text);
	//}
	//else
	{
		//printf( "%s", text );
		//TRACE("%s", text);
		OutputDebugString(text);
	}
//#endif
}

typedef struct {
	int pos;
	RECT rc;
	int pageno;
	int posmax;
}lsmarkinfo_t;

class LsCom{
public:
	static bool lsisinRect(RECT &rc, int x, int y)
	{
		return ((y > rc.top && y <rc.bottom)
			&& (x>rc.left && x < rc.right));
	}

};

class LsFastMark{
public: 
	LsFastMark(){
		lsmarknummax = 10;
		lsmarknum = 10;
		lsmarkheight = 30;

		lsfontwid = 0;
		lsfonthei = 0;

		lsbarheight = 8;
		lsbarwidth = 50;
		lspos = 0;
		lsdotpen = NULL;
	}
	~LsFastMark(){
		if (lsdotpen)
			DeleteObject(lsdotpen);
	}
	int lsfontwid;
	int lsfonthei;
	void lsgetFontSize(HDC &hdc)
	{
		TEXTMETRIC tm;
		GetTextMetrics(hdc, &tm);
		lsfontwid = tm.tmAveCharWidth;
		lsfonthei = tm.tmHeight + tm.tmExternalLeading;
	}

	int lsmarknummax;
	int lsmarknum;
	int lsmarkheight;

	map<int, lsmarkinfo_t> lsmarkmap;
	map<int, lsmarkinfo_t>::iterator lsfindMark(int x, int y)
	{
		map<int, lsmarkinfo_t>::iterator it;
		for (it = lsmarkmap.begin(); it != lsmarkmap.end(); it++)
		{
			if (LsCom::lsisinRect(it->second.rc, x, y))
				return it;
		}
		return lsmarkmap.end();
	}
	map<int, lsmarkinfo_t>::iterator lsfindMark(int pos)
	{
		map<int, lsmarkinfo_t>::iterator it;
		for (it = lsmarkmap.begin(); it != lsmarkmap.end(); it++)
		{
			if (it->first == pos)
				return it;
		}
		return lsmarkmap.end();
	}
	lsmarkinfo_t * findMarkinfo(int x, int y)
	{
		map<int, lsmarkinfo_t>::iterator it = lsfindMark(x, y);
		if (it != lsmarkmap.end())
			return &it->second;
		else
			return NULL;
	}
	lsmarkinfo_t * findMarkinfo(int pos)
	{
		map<int, lsmarkinfo_t>::iterator it = lsfindMark(pos);
		if (it != lsmarkmap.end())
			return &it->second;
		else
			return NULL;
	}
	RECT lsbarrc;
	int lsbarheight;
	int lsbarwidth;
	int lspos;
	RECT * getBarrc() { return &lsbarrc;  }

	HPEN lsdotpen;
	void lscreatDotPen()
	{
		lsdotpen = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
	}

	RECT genMarkRect(int pos)
	{
		RECT rc;
		rc = lsbarrc;
		rc.top = pos;
		rc.bottom = rc.top + lsmarkheight;// 5;
		return rc;
	}
	bool lsaddMark(int pos,int pageno, int posmax)
	{
		//if (lsmarkmap.size() >= lsmarknummax - 1)
		//{
		//	lsdebugout(TEXT(">ladddMark: too much marks, add failed.\r\n"));
		//	return false;
		//}
		//
		map<int, lsmarkinfo_t>::iterator it = lsfindMark(pos);
		if (it != lsmarkmap.end())
			return false;
		else
		{
			lsmarkinfo_t mk;
			mk.pos = pos;
			mk.pageno = pageno;// lsgetPageNo();
			mk.rc = genMarkRect(pos);
			mk.posmax = posmax;// lssi.nMax;	// 150309;
			//lsdebugout(TEXT(">lsaddmark: pageno=%d\r\n"), mk.pageno);
			lsmarkmap.insert(pair<int, lsmarkinfo_t>(pos, mk));

			int num = lsmarkmap.size();
			if (num > lsmarknum)
				lsmarknum = num;

			return true;
		}
	}
	int calStartPos(int num, int nummin, int h)
	{
		if (num >= nummin)
			return h;
		else
		{
			return h + (nummin - num) / 2 * h;
		}
	}
	void lsdrawMarks(HDC &hdc)
	{
		int num = lsmarkmap.size();
		if (num <= 0)
			return;
		// check font size;
		if (lsfontwid == 0)
			lsgetFontSize(hdc);
		// update;
		//updateScrollinfo();
		//int posmax = lssi.nMax;
		// recal freespace;
		int htotal = lsbarrc.bottom;
		lsmarkheight = htotal / ((lsmarknum + 2) * 2);// (lsmarknummax * 2);
		int hmark = lsmarkheight * 2;

		//
		map<int, lsmarkinfo_t>::iterator it = lsmarkmap.begin();
		int pos = calStartPos(lsmarkmap.size(), lsmarknum, hmark);
		RECT rc;
		int fontpos = (lsmarkheight - lsfonthei) / 2;
		for (; it != lsmarkmap.end(); it++, pos += hmark)
		{

			it->second.rc.top = pos;
			it->second.rc.bottom = it->second.rc.top + lsmarkheight;
			rc = it->second.rc;

			//SetDCBrushColor(hdc, RGB(255, 0, 0));
			SelectObject(hdc, GetStockObject(NULL_BRUSH));

			//HPEN hPen = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
			if (lsdotpen == NULL)	lscreatDotPen();
			HPEN hOldPen = (HPEN)SelectObject(hdc, lsdotpen);
			//FillRect(hdc, &it->second.rc, GetStockBrush(DKGRAY_BRUSH));
			//Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
			Ellipse(hdc, -rc.right, rc.top, rc.right, rc.bottom);
			SelectObject(hdc, hOldPen);

			//lsdebugout(TEXT(">lsdrawMarks: pageno=%d\r\n"), it->second.pageno);
			rc.top += fontpos;
			_stprintf(pt_message, TEXT("%d"), it->second.pageno);
			SetBkMode(hdc, TRANSPARENT);
			//SetTextColor(hdc, RGB(0, 0, 255)); 
			TextOut(hdc, rc.left, rc.top, pt_message, _tcslen(pt_message));
		}
	}
	void lsdelMark(map<int, lsmarkinfo_t>::iterator it)
	{
		lsmarkmap.erase(it);
		
	}
	void lsdelMark(int key)
	{
		lsmarkmap.erase(key);
	}

	void lsdrawbar(HDC &hdc)
	{
		//FillRect(hdc, &lsbarrc, GetStockBrush(GRAY_BRUSH));
	}
	void lsdrawCurpos(HDC &hdc, double posPrecent)
	{
		// draw current-location in ls-scrool-bar;
		RECT pos = lsbarrc;
		double f = posPrecent;// lsgetPosPrecent(win);// double)si.nPos / (double)si.nMax;
		pos.top = lsbarrc.bottom *f;
		pos.bottom = pos.top + lsbarheight;
		//original = SelectObject(hdc, GetStockObject(DC_PEN));
		//Rectangle(hdc, pos.left, pos.top, pos.right, pos.bottom);

		if (lsdotpen == NULL)	lscreatDotPen();
		HPEN hOldPen = (HPEN)SelectObject(hdc, lsdotpen);
		MoveToEx(hdc, pos.left, pos.top, NULL);
		LineTo(hdc, pos.right, pos.top);
		SelectObject(hdc, hOldPen);

		//FillRect(hdc, &pos, GetStockBrush(BLACK_BRUSH));
		//SelectObject(hdc, original);
		lspos = pos.top;
	}
	void drawMarks(HDC &hdc, PAINTSTRUCT &ps)
	{
		// update client rect;
		RECT rc = ps.rcPaint;
		lsbarrc = rc;
		lsbarrc.left = 0;
		lsbarrc.right = lsbarrc.left + lsbarwidth;
		
		//TextOut(hdc, rc.right / 2, rc.bottom / 2, TEXT("hello ls"), 8);
		// draw ls-scroll-bar;
		// lsdrawbar(hdc);	

		// draw ls-bookmarks;
		lsdrawMarks(hdc);

		//lsdrawCurpos(win, hdc);
	}
};
class LsFastDrag{
private:
	bool lsdragms;
	int lspritime;		// privous time;
	int lsmintimegap;	// in millisecond;

	bool lsdragdis;
	int lsprix;			// privous mouse location;
	int lspriy;
	
	RECT mdragrc;	//+ls@150315;

	bool misfastdrag = false;

public:
	LsFastDrag(){
		lsdragms = false;
		lspritime = 0;		// privous time;
		lsmintimegap = 10;	// in millisecond;

		lsdragdis = false;
		lsprix = 0;			// privous mouse location;
		lspriy = 0;
	}

	int lscalDistance(int x, int y)
	{
		if (lsdragdis)
		{
			lsdragdis = false;
			lsprix = x;
			lspriy = y;
			return 0;
		}
		// cal location diff;
		int dx = x - lsprix;
		int dy = y - lspriy;
		lsprix = x;
		lspriy = y;

		return dy;
	}
	int lsgetMillisecond()
	{
		//SYSTEMTIME st;
		////GetSystemTime(&st);
		//GetLocalTime(&st);
		//lsdebugout(TEXT(">lsonDragging: %02d:%02d:%02d,%d\r\n"),
		//	st.wHour, st.wMinute, st.wSecond,
		//	st.wMilliseconds);

		// way 2;
		FILETIME ft;
		GetSystemTimeAsFileTime(&ft);
		long long ll_now = (LONGLONG)ft.dwLowDateTime
			+ ((LONGLONG)(ft.dwHighDateTime) << 32LL); // to nonosecond;
		int ms = ll_now / 10000; // to millisecond;
		return ms;
	}
	int lscalMillisecond()
	{
		int ms = lsgetMillisecond();
		if (lsdragms)
		{
			lsdragms = false;
			lspritime = ms;
			return -1;			// initial value;
		}
		// cal time diff;
		int dt = ms - lspritime;
		if (dt < lsmintimegap)
			return 0;
		else
			lspritime = ms;
		return ms;
	}

	bool isInDragArea(int x, int y)
	{
		return LsCom::lsisinRect(mdragrc, x, y);
	}
	bool isFastDrag() {
		return misfastdrag;
	}
	bool lsonDragStart(int x, int y)
	{
		lsdragms = true;
		lsdragdis = true;

		misfastdrag = isInDragArea(x, y);
		return misfastdrag;
	}
	void lsonDragStop()
	{
		misfastdrag = false;
		lsdragms = false;
		lsdragdis = false;
	}
	bool lsonDragging(WindowInfo &win, int x, int y)
	{
		if (!isFastDrag())
			return false;

		int d = lscalMillisecond();
		int dy = 0;
		if (d == -1)		//set initial value;
		{
			dy = lscalDistance(x, y);
			return false;
		}
		else if (d == 0)	// time gap is too small;
			return false;
		else
			dy = lscalDistance(x, y);
		//
		double base = 2.7;
		int dis = 0;// *((double)lssi.nPage / 20);
		//if (dy >= 0)
		//dis = pow(base, dy);
		//else
		//dis = -pow(base, -dy);
		if (dy >= 0)
			dis = -pow(dy, base);
		else
			dis = pow(-dy, base);
		//lsdebugout(TEXT(">lsonDragging: move=%d\r\n"), dis);
		win.MoveDocBy(0, dis);

		return true;
	}
	void onDraw(PAINTSTRUCT &ps)
	{
		// update drag area;
		mdragrc = ps.rcPaint;
		mdragrc.right /= 2;
	}
};
class LsFun{
private:
	LsFastMark mfastmark;
	LsFastDrag mfastdrag;

	SCROLLINFO lssi;
	void updateScrollinfo()
	{
		lssi.cbSize = sizeof(lssi);
		lssi.fMask = SIF_ALL;
		GetScrollInfo(lswinfix->hwndCanvas, SB_VERT, &lssi);
	}
	int lsgetCurrentPos(WindowInfo &win)
	{
		updateScrollinfo();
		return lssi.nPos;
	}
	double lsgetPosPrecent(WindowInfo& win)
	{
		updateScrollinfo();
		double f = (double)lssi.nPos / (double)lssi.nMax;
		return f;
	}
	int lsgetPageNo()
	{
		DisplayModel *dm = lswinfix->ctrl->AsFixed();
		return dm->CurrentPageNo();
	}	
	void lsgotoPos(WindowInfo& win, int pos, int posmax)
	{
		int curpos = lsgetCurrentPos(win);
		//updateScrollinfo();
		int curposmax = lssi.nMax;
		if (posmax != curposmax)
			pos = double(curposmax) / (double)posmax * (double)pos;
		win.MoveDocBy(0, pos - curpos);
	}
public:
	void lstest(WindowInfo& win)
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(win.hwndCanvas, &ps);
		TextOut(hdc, 0, 0, TEXT("hello ls"), 8);
		EndPaint(win.hwndCanvas, &ps);
	}
	
	TCHAR mfilename[1024];
	WindowInfo * lswinfix;

	POINT lsscreentoClient(int x, int y)
	{
		POINT p;
		p.x = x; p.y = y;
		ScreenToClient(lswinfix->hwndCanvas, &p);
		return p;
	}
	void onDraw(WindowInfo &win, HDC &hdc, PAINTSTRUCT &ps)
	{
		mfastdrag.onDraw(ps);
		mfastmark.drawMarks(hdc, ps);
		mfastmark.lsdrawCurpos(hdc, lsgetPosPrecent(win));
	}
	void lsonMouseLeftButtonDbClick(WindowInfo& win, int x, int y, WPARAM key)
	{
		// add bookmark;
		int pos = lsgetCurrentPos(win);
		
		if (mfastmark.lsaddMark(pos,lsgetPageNo(),lssi.nMax))
		{
			//lsdebugout(TEXT(">DbClick: add mark ok\r\n"));
			updateMarkbar();
		}
	}
	bool lsonMouseLeftButtonDown(WindowInfo& win, int x, int y, WPARAM key)
	{
		//lsdebugout(TEXT(">lsonMouseLeftButtonDown: (x,y)=(%d,%d)\r\n"), x, y);
		map<int, lsmarkinfo_t>::iterator it = mfastmark.lsfindMark(x, y);
		if (it != mfastmark.lsmarkmap.end())
		{
			
			lsgotoPos(win, it->first, it->second.posmax);
			return true;
		}
		else
			return false;
	}
	bool lsonMouseLeftButtonUp(WindowInfo& win, int x, int y, WPARAM key)
	{
		//lsdebugout(TEXT(">lsonMouseLeftButtonUp: (x,y)=(%d,%d)\r\n"), x, y);
		return false;
	}
	bool lsonMouseRightButtonDown(WindowInfo& win, int x, int y, WPARAM key)
	{
		//lsdebugout(TEXT(">lsonMouseRightButtonDown: (x,y)=(%d,%d)\r\n"), x, y);
		lsmarkinfo_t * info = mfastmark.findMarkinfo(x, y);
		if (info != NULL)
		{
			mfastmark.lsdelMark(info->pos);
			updateMarkbar();
			//lsmarkmap.erase(it);
			//::InvalidateRect(win.hwndCanvas, &lsbarrc, true);
			//::UpdateWindow(win.hwndCanvas);
			return true;
		}
		else
			return false;
	}
	
	lsmarkinfo_t * mmarkinfo;
	bool lsonPanBegin(int x, int y)
	{
		POINT p = lsscreentoClient(x, y);
		mmarkinfo = mfastmark.findMarkinfo(p.x, p.y);
		//if (lsit != lsmarkmap.end())
		//	lsdebugout(TEXT(">lsonPanbegin: in.\r\n"));

		return false;
	}
	void updateMarkbar()
	{
		::InvalidateRect(lswinfix->hwndCanvas, mfastmark.getBarrc(), true);
		::UpdateWindow(lswinfix->hwndCanvas);
	}
	bool lsonPanEnd(int x, int y)
	{
		POINT p = lsscreentoClient(x, y);
		lsmarkinfo_t * info = mfastmark.findMarkinfo(p.x, p.y);
		if ((mmarkinfo != NULL) && (mmarkinfo != info))
		{
			mfastmark.lsdelMark(mmarkinfo->pos);
			updateMarkbar();
		}
		return false;
	}
	// --------------------------------------------------------------------------
	bool onDragStart(int x, int y)
	{
		return mfastdrag.lsonDragStart(x, y);
	}
	void onDragStop()
	{
		mfastdrag.lsonDragStop();
	}
	bool onDragging(WindowInfo &win,int x, int y)
	{
		return mfastdrag.lsonDragging(win, x, y);
	}

public:
	LsFun()
	{
		lswinfix = NULL;
		lssi = { 0 };
	}
	~LsFun(){}
};

class LsMarkDoc
{
private:
	int mcount;
	map<int, LsFun*> mdocmarks;

public:
	map<int, LsFun*>::iterator find(TCHAR * name)
	{
		map<int, LsFun*>::iterator it;
		for (it = mdocmarks.begin(); it != mdocmarks.end(); it++)
		{
			if (_tcscmp(name, it->second->mfilename) == 0)
			{
				break;
			}
		}
		return it;
	}
	LsFun* findDoc(TCHAR *name)
	{
		LsFun * fun = NULL;
		map<int, LsFun*>::iterator it = find(name);
		if (it != mdocmarks.end())
			fun = it->second;
		return fun;
	}
	LsFun * addDoc(TCHAR *name)
	{
		//if (mdocmarks.size() > 0)
		{
			map<int, LsFun*>::iterator it = find(name);
			if (it != mdocmarks.end())
			{
				//lsdebugout(TEXT(">addDoc: already exist.\r\n"));
				return it->second;
			}
		}
		LsFun * fun = new LsFun();
		_tcscpy(fun->mfilename, name);// , _tcslen(name));
		//lsdebugout(TEXT(">addDoc: mfilename=%s\r\n"), fun->mfilename);
		mdocmarks.insert(pair<int, LsFun*>(mcount++, fun));
		lsdebugout(TEXT(">lsmarddoc: adddoc ok\r\n"));
		return fun;
	}
	void delDoc(TCHAR *name)
	{
		map<int, LsFun*>::iterator it = find(name);
		if (it != mdocmarks.end())
		{
			if (it->second)
			{
				lsdebugout(TEXT(">lsmarkDoc.delDoc, name=%s\r\n"), it->second->mfilename);
				delete it->second;
				it->second = NULL;
			}
			mdocmarks.erase(it);
		}
	}
	LsMarkDoc(){
		mcount = 0;
	}
	~LsMarkDoc(){
		map<int, LsFun*>::iterator it;
		for (it = mdocmarks.begin(); it != mdocmarks.end(); it++)
		{
			if (it->second)
			{
				delete it->second;
				it->second = NULL;
			}
		}
		mdocmarks.clear();
	}
};

static LsMarkDoc g_lsmarkdoc;
static LsMarkDoc *glsmarkdoc = &g_lsmarkdoc;
static LsFun * glsdoc = NULL;

void lsselectDoc(TCHAR *name)
{
	//lsdebugout(TEXT(">lsselectDoc: name=%s\r\n"), name);
	LsFun * doc = glsmarkdoc->findDoc(name);
	if (doc != NULL)
	{
		lsdebugout(TEXT(">lsselectDoc: %s\r\n"), doc->mfilename);
		glsdoc = doc;
	}
}
void lsaddDoc(TCHAR *name)
{
	//lsdebugout(TEXT(">lsaddDoc: name=%s\r\n"), name);
	glsmarkdoc->addDoc(name);
	//lsselectDoc(name);
}
void lsupdateDoc(TCHAR *name)
{
	LsFun * doc = glsmarkdoc->addDoc(name);
	if (glsdoc == NULL)
		glsdoc = doc;
	else if (_tcscmp(glsdoc->mfilename, name) != 0)
	{
		glsdoc = doc;
	}
}
void lscloseDoc(TCHAR *name)
{
	lsdebugout(TEXT(">lscloseDoc: name=%s\r\n"), name);
	glsdoc = NULL;
	glsmarkdoc->delDoc(name);
}

//--------------------------------------------------------------------

void UpdateDeltaPerLine()
{
    ULONG ulScrollLines;
    BOOL ok = SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &ulScrollLines, 0);
    if (!ok)
        return;
    // ulScrollLines usually equals 3 or 0 (for no scrolling) or -1 (for page scrolling)
    // WHEEL_DELTA equals 120, so iDeltaPerLine will be 40
    if (ulScrollLines == (ULONG)-1)
        gDeltaPerLine = -1;
    else if (ulScrollLines != 0)
        gDeltaPerLine = WHEEL_DELTA / ulScrollLines;
    else
        gDeltaPerLine = 0;
}

///// methods needed for FixedPageUI canvases with document loaded /////

static void OnVScroll(WindowInfo& win, WPARAM wParam)
{
    AssertCrash(win.AsFixed());

    SCROLLINFO si = { 0 };
    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(win.hwndCanvas, SB_VERT, &si);

    int iVertPos = si.nPos;
    int lineHeight = 16;
    if (!IsContinuous(win.ctrl->GetDisplayMode()) && ZOOM_FIT_PAGE == win.ctrl->GetZoomVirtual())
        lineHeight = 1;

    switch (LOWORD(wParam)) {
    case SB_TOP:        si.nPos = si.nMin; break;
    case SB_BOTTOM:     si.nPos = si.nMax; break;
    case SB_LINEUP:     si.nPos -= lineHeight; break;
    case SB_LINEDOWN:   si.nPos += lineHeight; break;
    case SB_HPAGEUP:    si.nPos -= si.nPage / 2; break;
    case SB_HPAGEDOWN:  si.nPos += si.nPage / 2; break;
    case SB_PAGEUP:     si.nPos -= si.nPage; break;
    case SB_PAGEDOWN:   si.nPos += si.nPage; break;
    case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
    }

    // Set the position and then retrieve it.  Due to adjustments
    // by Windows it may not be the same as the value set.
    si.fMask = SIF_POS;
    SetScrollInfo(win.hwndCanvas, SB_VERT, &si, TRUE);
    GetScrollInfo(win.hwndCanvas, SB_VERT, &si);

    // If the position has changed, scroll the window and update it
    if (si.nPos != iVertPos)
        win.AsFixed()->ScrollYTo(si.nPos);
}

static void OnHScroll(WindowInfo& win, WPARAM wParam)
{
    AssertCrash(win.AsFixed());

    SCROLLINFO si = { 0 };
    si.cbSize = sizeof (si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(win.hwndCanvas, SB_HORZ, &si);

    int iVertPos = si.nPos;
    switch (LOWORD(wParam)) {
    case SB_LEFT:       si.nPos = si.nMin; break;
    case SB_RIGHT:      si.nPos = si.nMax; break;
    case SB_LINELEFT:   si.nPos -= 16; break;
    case SB_LINERIGHT:  si.nPos += 16; break;
    case SB_PAGELEFT:   si.nPos -= si.nPage; break;
    case SB_PAGERIGHT:  si.nPos += si.nPage; break;
    case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
    }

    // Set the position and then retrieve it.  Due to adjustments
    // by Windows it may not be the same as the value set.
    si.fMask = SIF_POS;
    SetScrollInfo(win.hwndCanvas, SB_HORZ, &si, TRUE);
    GetScrollInfo(win.hwndCanvas, SB_HORZ, &si);

    // If the position has changed, scroll the window and update it
    if (si.nPos != iVertPos)
        win.AsFixed()->ScrollXTo(si.nPos);
}

static void OnDraggingStart(WindowInfo& win, int x, int y, bool right=false)
{
    SetCapture(win.hwndCanvas);
    win.mouseAction = right ? MA_DRAGGING_RIGHT : MA_DRAGGING;
    win.dragPrevPos = PointI(x, y);
    if (GetCursor())
        SetCursor(gCursorDrag);
	
	if (glsdoc)
		glsdoc->onDragStart(x,y);//+ls@150206;
}

static void OnDraggingStop(WindowInfo& win, int x, int y, bool aborted)
{
	if (glsdoc)
		glsdoc->onDragStop();	//+ls@150306;

    if (GetCapture() != win.hwndCanvas)
        return;

    if (GetCursor())
        SetCursor(IDC_ARROW);
    ReleaseCapture();

    if (aborted)
        return;

    SizeI drag(x - win.dragPrevPos.x, y - win.dragPrevPos.y);
    win.MoveDocBy(drag.dx, -2 * drag.dy);
}

static void OnMouseMove(WindowInfo& win, int x, int y, WPARAM flags)
{
    AssertCrash(win.AsFixed());

    if (win.presentation) {
        if (PM_BLACK_SCREEN == win.presentation || PM_WHITE_SCREEN == win.presentation) {
            SetCursor((HCURSOR)nullptr);
            return;
        }
        // shortly display the cursor if the mouse has moved and the cursor is hidden
        if (PointI(x, y) != win.dragPrevPos && !GetCursor()) {
            if (win.mouseAction == MA_IDLE)
                SetCursor(IDC_ARROW);
            else
                SendMessage(win.hwndCanvas, WM_SETCURSOR, 0, 0);
            SetTimer(win.hwndCanvas, HIDE_CURSOR_TIMER_ID, HIDE_CURSOR_DELAY_IN_MS, nullptr);
        }
    }

    if (win.dragStartPending) {
        // have we already started a proper drag?
        if (abs(x - win.dragStart.x) <= GetSystemMetrics(SM_CXDRAG) &&
            abs(y - win.dragStart.y) <= GetSystemMetrics(SM_CYDRAG)) {
            return;
        }
        win.dragStartPending = false;
        delete win.linkOnLastButtonDown;
        win.linkOnLastButtonDown = nullptr;
    }

    switch (win.mouseAction) {
    case MA_SCROLLING:
        win.yScrollSpeed = (y - win.dragStart.y) / SMOOTHSCROLL_SLOW_DOWN_FACTOR;
        win.xScrollSpeed = (x - win.dragStart.x) / SMOOTHSCROLL_SLOW_DOWN_FACTOR;
        break;
    case MA_SELECTING_TEXT:
        if (GetCursor())
            SetCursor(IDC_IBEAM);
        /* fall through */
    case MA_SELECTING:
        win.selectionRect.dx = x - win.selectionRect.x;
        win.selectionRect.dy = y - win.selectionRect.y;
        OnSelectionEdgeAutoscroll(&win, x, y);
        win.RepaintAsync();
        break;
    case MA_DRAGGING:
    case MA_DRAGGING_RIGHT:
		bool isfastdrag = false;
		if (glsdoc)
		{
			isfastdrag = glsdoc->onDragging(win, x, y);
		}
		//lsdebugout(TEXT(">onMouseMove: MA_dragging, dx=%d,dy=%d\r\n"),
		//	win.dragPrevPos.x - x, win.dragPrevPos.y - y);
		if (!isfastdrag)
			win.MoveDocBy(win.dragPrevPos.x - x, win.dragPrevPos.y - y);
        break;
    }
    // needed also for detecting cursor movement in presentation mode
    win.dragPrevPos = PointI(x, y);

    NotificationWnd *wnd = win.notifications->GetForGroup(NG_CURSOR_POS_HELPER);
    if (wnd) {
        if (MA_SELECTING == win.mouseAction)
            win.selectionMeasure = win.AsFixed()->CvtFromScreen(win.selectionRect).Size();
        UpdateCursorPositionHelper(&win, PointI(x, y), wnd);
    }
}

static void OnMouseLeftButtonDown(WindowInfo& win, int x, int y, WPARAM key)
{
    //lf("Left button clicked on %d %d", x, y);
    if (MA_DRAGGING_RIGHT == win.mouseAction)
        return;

    if (MA_SCROLLING == win.mouseAction) {
        win.mouseAction = MA_IDLE;
        return;
    }

    CrashIfDebugOnly(win.mouseAction != MA_IDLE); // happened e.g. in crash 50539
    CrashIf(!win.AsFixed());

    SetFocus(win.hwndFrame);

	if (glsdoc)
		if (glsdoc->lsonMouseLeftButtonDown(win, x, y, key)) // +ls@150307;
		{
			return;
		}

    AssertCrash(!win.linkOnLastButtonDown);
    DisplayModel *dm = win.AsFixed();
    PageElement *pageEl = dm->GetElementAtPos(PointI(x, y));
    if (pageEl && pageEl->GetType() == Element_Link)
        win.linkOnLastButtonDown = pageEl;
    else
        delete pageEl;
    win.dragStartPending = true;
    win.dragStart = PointI(x, y);

    // - without modifiers, clicking on text starts a text selection
    //   and clicking somewhere else starts a drag
    // - pressing Shift forces dragging
    // - pressing Ctrl forces a rectangular selection
    // - pressing Ctrl+Shift forces text selection
    // - not having CopySelection permission forces dragging
    bool isShift = IsShiftPressed();
    bool isCtrl = IsCtrlPressed();
    bool canCopy = HasPermission(Perm_CopySelection);
    bool isOverText = dm->IsOverText(PointI(x,y));
    if (!canCopy || (isShift || !isOverText) && !isCtrl) {
        OnDraggingStart(win, x, y);
    } else {
        OnSelectionStart(&win, x, y, key);
    }
}

static void OnMouseLeftButtonUp(WindowInfo& win, int x, int y, WPARAM key)
{
    AssertCrash(win.AsFixed());
	//+ls
	if (glsdoc)
		glsdoc->lsonMouseLeftButtonUp(win, x, y, key);

    if (MA_IDLE == win.mouseAction || MA_DRAGGING_RIGHT == win.mouseAction)
        return;
    AssertCrash(MA_SELECTING == win.mouseAction || MA_SELECTING_TEXT == win.mouseAction || MA_DRAGGING == win.mouseAction);	

    bool didDragMouse = !win.dragStartPending ||
        abs(x - win.dragStart.x) > GetSystemMetrics(SM_CXDRAG) ||
        abs(y - win.dragStart.y) > GetSystemMetrics(SM_CYDRAG);
    if (MA_DRAGGING == win.mouseAction)
        OnDraggingStop(win, x, y, !didDragMouse);
    else {
        OnSelectionStop(&win, x, y, !didDragMouse);
        if (MA_SELECTING == win.mouseAction && win.showSelection)
            win.selectionMeasure = win.AsFixed()->CvtFromScreen(win.selectionRect).Size();
    }

    DisplayModel *dm = win.AsFixed();
    PointD ptPage = dm->CvtFromScreen(PointI(x, y));
    // TODO: win.linkHandler->GotoLink might spin the event loop
    PageElement *link = win.linkOnLastButtonDown;
    win.linkOnLastButtonDown = nullptr;
    win.mouseAction = MA_IDLE;

    if (didDragMouse)
        /* pass */;
    /* return from white/black screens in presentation mode */
    else if (PM_BLACK_SCREEN == win.presentation || PM_WHITE_SCREEN == win.presentation)
        win.ChangePresentationMode(PM_ENABLED);
    /* follow an active link */
    else if (link && link->GetRect().Contains(ptPage)) {
        PageDestination *dest = link->AsLink();
        win.linkHandler->GotoLink(dest);
        SetCursor(IDC_ARROW);
        // highlight the clicked link (as a reminder of the last action once the user returns)
        if (dest && (Dest_LaunchURL == dest->GetDestType() || Dest_LaunchFile == dest->GetDestType())) {
            DeleteOldSelectionInfo(&win, true);
            win.currentTab->selectionOnPage = SelectionOnPage::FromRectangle(dm, dm->CvtToScreen(link->GetPageNo(), link->GetRect()));
            win.showSelection = win.currentTab->selectionOnPage != nullptr;
            win.RepaintAsync();
        }
    }
    /* if we had a selection and this was just a click, hide the selection */
    else if (win.showSelection)
        ClearSearchResult(&win);
    /* if there's a permanent forward search mark, hide it */
    else if (win.fwdSearchMark.show && gGlobalPrefs->forwardSearch.highlightPermanent) {
        win.fwdSearchMark.show = false;
        win.RepaintAsync();
    }
    /* in presentation mode, change pages on left/right-clicks */
    else if (PM_ENABLED == win.presentation) {
        if ((key & MK_SHIFT))
            win.ctrl->GoToPrevPage();
        else
            win.ctrl->GoToNextPage();
    }

    delete link;
}

static void OnMouseLeftButtonDblClk(WindowInfo& win, int x, int y, WPARAM key)
{
    //lf("Left button clicked on %d %d", x, y);
    if (win.presentation && !(key & ~MK_LBUTTON)) {
        // in presentation mode, left clicks turn the page,
        // make two quick left clicks (AKA one double-click) turn two pages
        OnMouseLeftButtonDown(win, x, y, key);
        return;
    }

	if (glsdoc)
		glsdoc->lsonMouseLeftButtonDbClick(win, x, y, key); // +ls@150307;

    bool dontSelect = false;
    if (gGlobalPrefs->enableTeXEnhancements && !(key & ~MK_LBUTTON))
        dontSelect = OnInverseSearch(&win, x, y);
    if (dontSelect)
        return;

    DisplayModel *dm = win.AsFixed();
    if (dm->IsOverText(PointI(x, y))) {
        int pageNo = dm->GetPageNoByPoint(PointI(x, y));
        if (win.ctrl->ValidPageNo(pageNo)) {
            PointD pt = dm->CvtFromScreen(PointI(x, y), pageNo);
            dm->textSelection->SelectWordAt(pageNo, pt.x, pt.y);
            UpdateTextSelection(&win, false);
            win.RepaintAsync();
        }
        return;
    }

    PageElement *pageEl = dm->GetElementAtPos(PointI(x, y));
    if (pageEl && pageEl->GetType() == Element_Link) {
        // speed up navigation in a file where navigation links are in a fixed position
        OnMouseLeftButtonDown(win, x, y, key);
    }
    else if (pageEl && pageEl->GetType() == Element_Image) {
        // select an image that could be copied to the clipboard
        RectI rc = dm->CvtToScreen(pageEl->GetPageNo(), pageEl->GetRect());

        DeleteOldSelectionInfo(&win, true);
        win.currentTab->selectionOnPage = SelectionOnPage::FromRectangle(dm, rc);
        win.showSelection = win.currentTab->selectionOnPage != nullptr;
        win.RepaintAsync();
    }
    delete pageEl;
}

static void OnMouseMiddleButtonDown(WindowInfo& win, int x, int y, WPARAM key)
{
    // Handle message by recording placement then moving document as mouse moves.

    switch (win.mouseAction) {
    case MA_IDLE:
        win.mouseAction = MA_SCROLLING;

        // record current mouse position, the farther the mouse is moved
        // from this position, the faster we scroll the document
        win.dragStart = PointI(x, y);
        SetCursor(IDC_SIZEALL);
        break;

    case MA_SCROLLING:
        win.mouseAction = MA_IDLE;
        break;
    }
}

static void OnMouseRightButtonDown(WindowInfo& win, int x, int y, WPARAM key)
{
    //lf("Right button clicked on %d %d", x, y);
    if (MA_SCROLLING == win.mouseAction)
        win.mouseAction = MA_IDLE;
    else if (win.mouseAction != MA_IDLE)
        return;
    AssertCrash(win.AsFixed());

    SetFocus(win.hwndFrame);

	if (glsdoc)
		if (glsdoc->lsonMouseRightButtonDown(win, x, y, key))
		{
			return;
		}

    win.dragStartPending = true;
    win.dragStart = PointI(x, y);

    OnDraggingStart(win, x, y, true);
}

static void OnMouseRightButtonUp(WindowInfo& win, int x, int y, WPARAM key)
{
    AssertCrash(win.AsFixed());
    if (MA_DRAGGING_RIGHT != win.mouseAction)
        return;

    bool didDragMouse = !win.dragStartPending ||
        abs(x - win.dragStart.x) > GetSystemMetrics(SM_CXDRAG) ||
        abs(y - win.dragStart.y) > GetSystemMetrics(SM_CYDRAG);
    OnDraggingStop(win, x, y, !didDragMouse);

    win.mouseAction = MA_IDLE;

    if (didDragMouse)
        /* pass */;
    else if (PM_ENABLED == win.presentation) {
        if ((key & MK_CONTROL))
            OnContextMenu(&win, x, y);
        else if ((key & MK_SHIFT))
            win.ctrl->GoToNextPage();
        else
            win.ctrl->GoToPrevPage();
    }
    /* return from white/black screens in presentation mode */
    else if (PM_BLACK_SCREEN == win.presentation || PM_WHITE_SCREEN == win.presentation)
        win.ChangePresentationMode(PM_ENABLED);
    else
        OnContextMenu(&win, x, y);
}

static void OnMouseRightButtonDblClick(WindowInfo& win, int x, int y, WPARAM key)
{
    if (win.presentation && !(key & ~MK_RBUTTON)) {
        // in presentation mode, right clicks turn the page,
        // make two quick right clicks (AKA one double-click) turn two pages
        OnMouseRightButtonDown(win, x, y, key);
        return;
    }
}

#ifdef DRAW_PAGE_SHADOWS
#define BORDER_SIZE   1
#define SHADOW_OFFSET 4
static void PaintPageFrameAndShadow(HDC hdc, RectI& bounds, RectI& pageRect, bool presentation)
{
    // Frame info
    RectI frame = bounds;
    frame.Inflate(BORDER_SIZE, BORDER_SIZE);

    // Shadow info
    RectI shadow = frame;
    shadow.Offset(SHADOW_OFFSET, SHADOW_OFFSET);
    if (frame.x < 0) {
        // the left of the page isn't visible, so start the shadow at the left
        int diff = std::min(-pageRect.x, SHADOW_OFFSET);
        shadow.x -= diff; shadow.dx += diff;
    }
    if (frame.y < 0) {
        // the top of the page isn't visible, so start the shadow at the top
        int diff = std::min(-pageRect.y, SHADOW_OFFSET);
        shadow.y -= diff; shadow.dy += diff;
    }

    // Draw shadow
    if (!presentation) {
        ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(COL_PAGE_SHADOW));
        FillRect(hdc, &shadow.ToRECT(), brush);
    }

    // Draw frame
    ScopedGdiObj<HPEN> pe(CreatePen(PS_SOLID, 1, presentation ? TRANSPARENT : COL_PAGE_FRAME));
    ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(gRenderCache.backgroundColor));
    SelectObject(hdc, pe);
    SelectObject(hdc, brush);
    Rectangle(hdc, frame.x, frame.y, frame.x + frame.dx, frame.y + frame.dy);
}
#else
static void PaintPageFrameAndShadow(HDC hdc, RectI& bounds, RectI& pageRect, bool presentation)
{
    ScopedGdiObj<HPEN> pe(CreatePen(PS_NULL, 0, 0));
    ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(gRenderCache.backgroundColor));
    SelectObject(hdc, pe);
    SelectObject(hdc, brush);
    Rectangle(hdc, bounds.x, bounds.y, bounds.x + bounds.dx + 1, bounds.y + bounds.dy + 1);
}
#endif

/* debug code to visualize links (can block while rendering) */
static void DebugShowLinks(DisplayModel& dm, HDC hdc)
{
    if (!gDebugShowLinks)
        return;

    RectI viewPortRect(PointI(), dm.GetViewPort().Size());
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x00, 0xff, 0xff));
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    for (int pageNo = dm.PageCount(); pageNo >= 1; --pageNo) {
        PageInfo *pageInfo = dm.GetPageInfo(pageNo);
        if (!pageInfo || !pageInfo->shown || 0.0 == pageInfo->visibleRatio)
            continue;

        Vec<PageElement *> *els = dm.GetEngine()->GetElements(pageNo);
        if (els) {
            for (size_t i = 0; i < els->Count(); i++) {
                if (els->At(i)->GetType() == Element_Image)
                    continue;
                RectI rect = dm.CvtToScreen(pageNo, els->At(i)->GetRect());
                RectI isect = viewPortRect.Intersect(rect);
                if (!isect.IsEmpty())
                    PaintRect(hdc, isect);
            }
            DeleteVecMembers(*els);
            delete els;
        }
    }

    DeletePen(SelectObject(hdc, oldPen));

    if (dm.GetZoomVirtual() == ZOOM_FIT_CONTENT) {
        // also display the content box when fitting content
        pen = CreatePen(PS_SOLID, 1, RGB(0xff, 0x00, 0xff));
        oldPen = SelectObject(hdc, pen);

        for (int pageNo = dm.PageCount(); pageNo >= 1; --pageNo) {
            PageInfo *pageInfo = dm.GetPageInfo(pageNo);
            if (!pageInfo->shown || 0.0 == pageInfo->visibleRatio)
                continue;

            RectI rect = dm.CvtToScreen(pageNo, dm.GetEngine()->PageContentBox(pageNo));
            PaintRect(hdc, rect);
        }

        DeletePen(SelectObject(hdc, oldPen));
    }
}

// cf. http://forums.fofou.org/sumatrapdf/topic?id=3183580
static void GetGradientColor(COLORREF a, COLORREF b, float perc, TRIVERTEX *tv)
{
    tv->Red = (COLOR16)((GetRValueSafe(a) + perc * (GetRValueSafe(b) - GetRValueSafe(a))) * 256);
    tv->Green = (COLOR16)((GetGValueSafe(a) + perc * (GetGValueSafe(b) - GetGValueSafe(a))) * 256);
    tv->Blue = (COLOR16)((GetBValueSafe(a) + perc * (GetBValueSafe(b) - GetBValueSafe(a))) * 256);
}

static void DrawDocument(WindowInfo& win, HDC hdc, RECT *rcArea)
{
    AssertCrash(win.AsFixed());
    if (!win.AsFixed()) return;
    DisplayModel* dm = win.AsFixed();

    bool paintOnBlackWithoutShadow = win.presentation ||
    // draw comic books and single images on a black background (without frame and shadow)
                                     dm->GetEngine()->IsImageCollection();
    if (paintOnBlackWithoutShadow) {
        ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(WIN_COL_BLACK));
        FillRect(hdc, rcArea, brush);
    }
    else if (0 == gGlobalPrefs->fixedPageUI.gradientColors->Count()) {
        ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(GetNoDocBgColor()));
        FillRect(hdc, rcArea, brush);
    }
    else {
        COLORREF colors[3];
        colors[0] = gGlobalPrefs->fixedPageUI.gradientColors->At(0);
        if (gGlobalPrefs->fixedPageUI.gradientColors->Count() == 1) {
            colors[1] = colors[2] = colors[0];
        }
        else if (gGlobalPrefs->fixedPageUI.gradientColors->Count() == 2) {
            colors[2] = gGlobalPrefs->fixedPageUI.gradientColors->At(1);
            colors[1] = RGB((GetRValueSafe(colors[0]) + GetRValueSafe(colors[2])) / 2,
                            (GetGValueSafe(colors[0]) + GetGValueSafe(colors[2])) / 2,
                            (GetBValueSafe(colors[0]) + GetBValueSafe(colors[2])) / 2);
        }
        else {
            colors[1] = gGlobalPrefs->fixedPageUI.gradientColors->At(1);
            colors[2] = gGlobalPrefs->fixedPageUI.gradientColors->At(2);
        }
        SizeI size = dm->GetCanvasSize();
        float percTop = 1.0f * dm->GetViewPort().y / size.dy;
        float percBot = 1.0f * dm->GetViewPort().BR().y / size.dy;
        if (!IsContinuous(dm->GetDisplayMode())) {
            percTop += dm->CurrentPageNo() - 1; percTop /= dm->PageCount();
            percBot += dm->CurrentPageNo() - 1; percBot /= dm->PageCount();
        }
        SizeI vp = dm->GetViewPort().Size();
        TRIVERTEX tv[4] = { { 0, 0 }, { vp.dx, vp.dy / 2 }, { 0, vp.dy / 2 }, { vp.dx, vp.dy } };
        GRADIENT_RECT gr[2] = { { 0, 1 }, { 2, 3 } };
        if (percTop < 0.5f)
            GetGradientColor(colors[0], colors[1], 2 * percTop, &tv[0]);
        else
            GetGradientColor(colors[1], colors[2], 2 * (percTop - 0.5f), &tv[0]);
        if (percBot < 0.5f)
            GetGradientColor(colors[0], colors[1], 2 * percBot, &tv[3]);
        else
            GetGradientColor(colors[1], colors[2], 2 * (percBot - 0.5f), &tv[3]);
        bool needCenter = percTop < 0.5f && percBot > 0.5f;
        if (needCenter) {
            GetGradientColor(colors[1], colors[1], 0, &tv[1]);
            GetGradientColor(colors[1], colors[1], 0, &tv[2]);
            tv[1].y = tv[2].y = (LONG)((0.5f - percTop) / (percBot - percTop) * vp.dy);
        }
        else
            gr[0].LowerRight = 3;
        // TODO: disable for less than about two screen heights?
        GradientFill(hdc, tv, dimof(tv), gr, needCenter ? 2 : 1, GRADIENT_FILL_RECT_V);
    }

    bool rendering = false;
    RectI screen(PointI(), dm->GetViewPort().Size());

    for (int pageNo = 1; pageNo <= dm->PageCount(); ++pageNo) {
        PageInfo *pageInfo = dm->GetPageInfo(pageNo);
        if (!pageInfo || 0.0f == pageInfo->visibleRatio)
            continue;
        AssertCrash(pageInfo->shown);
        if (!pageInfo->shown)
            continue;

        RectI bounds = pageInfo->pageOnScreen.Intersect(screen);
        // don't paint the frame background for images
        if (!dm->GetEngine()->IsImageCollection())
            PaintPageFrameAndShadow(hdc, bounds, pageInfo->pageOnScreen, win.presentation);

        bool renderOutOfDateCue = false;
        UINT renderDelay = 0;
        if (!dm->ShouldCacheRendering(pageNo)) {
            dm->GetEngine()->RenderPage(hdc, pageInfo->pageOnScreen, pageNo, dm->GetZoomReal(pageNo), dm->GetRotation());
        }
        else
            renderDelay = gRenderCache.Paint(hdc, bounds, dm, pageNo, pageInfo, &renderOutOfDateCue);

        if (renderDelay) {
            ScopedFont fontRightTxt(CreateSimpleFont(hdc, L"MS Shell Dlg", 14));
            HGDIOBJ hPrevFont = SelectObject(hdc, fontRightTxt);
            SetTextColor(hdc, gRenderCache.textColor);
            if (renderDelay != RENDER_DELAY_FAILED) {
                if (renderDelay < REPAINT_MESSAGE_DELAY_IN_MS)
                    win.RepaintAsync(REPAINT_MESSAGE_DELAY_IN_MS / 4);
                else
                    DrawCenteredText(hdc, bounds, _TR("Please wait - rendering..."), IsUIRightToLeft());
                rendering = true;
            } else {
                DrawCenteredText(hdc, bounds, _TR("Couldn't render the page"), IsUIRightToLeft());
            }
            SelectObject(hdc, hPrevFont);
            continue;
        }

        if (!renderOutOfDateCue)
            continue;

        HDC bmpDC = CreateCompatibleDC(hdc);
        if (bmpDC) {
            SelectObject(bmpDC, gBitmapReloadingCue);
            int size = DpiScaleY(win.hwndFrame, 16);
            int cx = std::min(bounds.dx, 2 * size);
            int cy = std::min(bounds.dy, 2 * size);
            int x = bounds.x + bounds.dx - std::min((cx + size) / 2, cx);
            int y = bounds.y + std::max((cy - size) / 2, 0);
            int dxDest = std::min(cx, size);
            int dyDest = std::min(cy, size);
            StretchBlt(hdc, x, y, dxDest, dyDest, bmpDC, 0, 0, 16, 16, SRCCOPY);
            DeleteDC(bmpDC);
        }
    }

    if (win.showSelection)
        PaintSelection(&win, hdc);

    if (win.fwdSearchMark.show)
        PaintForwardSearchMark(&win, hdc);

    if (!rendering)
        DebugShowLinks(*dm, hdc);
}


static void OnPaintDocument(WindowInfo& win)
{
    Timer t;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(win.hwndCanvas, &ps);

    switch (win.presentation) {
    case PM_BLACK_SCREEN:
        FillRect(hdc, &ps.rcPaint, GetStockBrush(BLACK_BRUSH));
        break;
    case PM_WHITE_SCREEN:
        FillRect(hdc, &ps.rcPaint, GetStockBrush(WHITE_BRUSH));
        break;
    default:
        DrawDocument(win, win.buffer->GetDC(), &ps.rcPaint);
        win.buffer->Flush(hdc);
    }

	//+ls@150306;
	if (glsdoc)
		glsdoc->onDraw(win, hdc, ps);

    EndPaint(win.hwndCanvas, &ps);
    if (gShowFrameRate) {
        ShowFrameRateDur(win.frameRateWnd, t.GetTimeInMs());
    }
}

static LRESULT OnSetCursor(WindowInfo& win, HWND hwnd)
{
    PointI pt;

    if (win.mouseAction != MA_IDLE)
        win.DeleteInfotip();

    switch (win.mouseAction) {
    case MA_DRAGGING:
    case MA_DRAGGING_RIGHT:
        SetCursor(gCursorDrag);
        return TRUE;
    case MA_SCROLLING:
        SetCursor(IDC_SIZEALL);
        return TRUE;
    case MA_SELECTING_TEXT:
        SetCursor(IDC_IBEAM);
        return TRUE;
    case MA_SELECTING:
        break;
    case MA_IDLE:
        if (GetCursor() && GetCursorPosInHwnd(hwnd, pt) && win.AsFixed()) {
            if (win.notifications->GetForGroup(NG_CURSOR_POS_HELPER)) {
                SetCursor(IDC_CROSS);
                return TRUE;
            }
            DisplayModel *dm = win.AsFixed();
            PageElement *pageEl = dm->GetElementAtPos(pt);
            if (pageEl) {
                ScopedMem<WCHAR> text(pageEl->GetValue());
                RectI rc = dm->CvtToScreen(pageEl->GetPageNo(), pageEl->GetRect());
                win.CreateInfotip(text, rc, true);

                bool isLink = pageEl->GetType() == Element_Link;
                delete pageEl;

                if (isLink) {
                    SetCursor(IDC_HAND);
                    return TRUE;
                }
            }
            else
                win.DeleteInfotip();
            if (dm->IsOverText(pt))
                SetCursor(IDC_IBEAM);
            else
                SetCursor(IDC_ARROW);
            return TRUE;
        }
        win.DeleteInfotip();
        break;
    }
    return win.presentation ? TRUE : FALSE;
}

static LRESULT CanvasOnMouseWheel(WindowInfo& win, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Scroll the ToC sidebar, if it's visible and the cursor is in it
    if (win.tocVisible && IsCursorOverWindow(win.hwndTocTree) && !gWheelMsgRedirect) {
        // Note: hwndTocTree's window procedure doesn't always handle
        //       WM_MOUSEWHEEL and when it's bubbling up, we'd return
        //       here recursively - prevent that
        gWheelMsgRedirect = true;
        LRESULT res = SendMessage(win.hwndTocTree, message, wParam, lParam);
        gWheelMsgRedirect = false;
        return res;
    }

    short delta = GET_WHEEL_DELTA_WPARAM(wParam);

    // Note: not all mouse drivers correctly report the Ctrl key's state
    if ((LOWORD(wParam) & MK_CONTROL) || IsCtrlPressed() || (LOWORD(wParam) & MK_RBUTTON)) {
        PointI pt;
        GetCursorPosInHwnd(win.hwndCanvas, pt);

        float zoom = win.ctrl->GetNextZoomStep(delta < 0 ? ZOOM_MIN : ZOOM_MAX);
        win.ctrl->SetZoomVirtual(zoom, &pt);
        UpdateToolbarState(&win);

        // don't show the context menu when zooming with the right mouse-button down
        if ((LOWORD(wParam) & MK_RBUTTON))
            win.dragStartPending = false;

        return 0;
    }

    // make sure to scroll whole pages in non-continuous Fit Content mode
    if (!IsContinuous(win.ctrl->GetDisplayMode()) &&
        ZOOM_FIT_CONTENT == win.ctrl->GetZoomVirtual()) {
        if (delta > 0)
            win.ctrl->GoToPrevPage();
        else
            win.ctrl->GoToNextPage();
        return 0;
    }

    if (gDeltaPerLine == 0)
        return 0;

    bool horizontal = (LOWORD(wParam) & MK_ALT) || IsAltPressed();
    if (horizontal)
        gSuppressAltKey = true;

    if (gDeltaPerLine < 0 && win.AsFixed()) {
        // scroll by (fraction of a) page
        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask  = SIF_PAGE;
        GetScrollInfo(win.hwndCanvas, horizontal ? SB_HORZ : SB_VERT, &si);
        if (horizontal)
            win.AsFixed()->ScrollXBy(-MulDiv(si.nPage, delta, WHEEL_DELTA));
        else
            win.AsFixed()->ScrollYBy(-MulDiv(si.nPage, delta, WHEEL_DELTA), true);
        return 0;
    }

    win.wheelAccumDelta += delta;
    int currentScrollPos = GetScrollPos(win.hwndCanvas, SB_VERT);

    while (win.wheelAccumDelta >= gDeltaPerLine) {
        if (horizontal)
            SendMessage(win.hwndCanvas, WM_HSCROLL, SB_LINELEFT, 0);
        else
            SendMessage(win.hwndCanvas, WM_VSCROLL, SB_LINEUP, 0);
        win.wheelAccumDelta -= gDeltaPerLine;
    }
    while (win.wheelAccumDelta <= -gDeltaPerLine) {
        if (horizontal)
            SendMessage(win.hwndCanvas, WM_HSCROLL, SB_LINERIGHT, 0);
        else
            SendMessage(win.hwndCanvas, WM_VSCROLL, SB_LINEDOWN, 0);
        win.wheelAccumDelta += gDeltaPerLine;
    }

    if (!horizontal && !IsContinuous(win.ctrl->GetDisplayMode()) &&
        GetScrollPos(win.hwndCanvas, SB_VERT) == currentScrollPos) {
        if (delta > 0)
            win.ctrl->GoToPrevPage(true);
        else
            win.ctrl->GoToNextPage();
    }

    return 0;
}

static LRESULT CanvasOnMouseHWheel(WindowInfo& win, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Scroll the ToC sidebar, if it's visible and the cursor is in it
    if (win.tocVisible && IsCursorOverWindow(win.hwndTocTree) && !gWheelMsgRedirect) {
        // Note: hwndTocTree's window procedure doesn't always handle
        //       WM_MOUSEHWHEEL and when it's bubbling up, we'd return
        //       here recursively - prevent that
        gWheelMsgRedirect = true;
        LRESULT res = SendMessage(win.hwndTocTree, message, wParam, lParam);
        gWheelMsgRedirect = false;
        return res;
    }

    short delta = GET_WHEEL_DELTA_WPARAM(wParam);
    win.wheelAccumDelta += delta;

    while (win.wheelAccumDelta >= gDeltaPerLine) {
        SendMessage(win.hwndCanvas, WM_HSCROLL, SB_LINERIGHT, 0);
        win.wheelAccumDelta -= gDeltaPerLine;
    }
    while (win.wheelAccumDelta <= -gDeltaPerLine) {
        SendMessage(win.hwndCanvas, WM_HSCROLL, SB_LINELEFT, 0);
        win.wheelAccumDelta += gDeltaPerLine;
    }

    return TRUE;
}

static LRESULT OnGesture(WindowInfo& win, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (!Touch::SupportsGestures())
        return DefWindowProc(win.hwndFrame, message, wParam, lParam);

    HGESTUREINFO hgi = (HGESTUREINFO)lParam;
    GESTUREINFO gi = { 0 };
    gi.cbSize = sizeof(GESTUREINFO);

    BOOL ok = Touch::GetGestureInfo(hgi, &gi);
    if (!ok) {
        Touch::CloseGestureInfoHandle(hgi);
        return 0;
    }

    switch (gi.dwID) {
        case GID_ZOOM:
            if (gi.dwFlags != GF_BEGIN && win.AsFixed()) {
                float zoom = (float)LODWORD(gi.ullArguments) / (float)win.touchState.startArg;
                ZoomToSelection(&win, zoom, false, true);
            }
            win.touchState.startArg = LODWORD(gi.ullArguments);
            break;

        case GID_PAN:
            // Flicking left or right changes the page,
            // panning moves the document in the scroll window
			
            if (gi.dwFlags == GF_BEGIN) {
				//lsdebugout(TEXT(">onGesture: GF_begin@(x,y)=(%d,%d).\r\n"), gi.ptsLocation.x, gi.ptsLocation.y);
				if (glsdoc)
				{
					if (!glsdoc->lsonPanBegin(gi.ptsLocation.x, gi.ptsLocation.y))
					{
						POINT p = glsdoc->lsscreentoClient(gi.ptsLocation.x, gi.ptsLocation.y);
						glsdoc->onDragStart(p.x, p.y);
					}
				}
                win.touchState.panStarted = true;
                win.touchState.panPos = gi.ptsLocation;
                win.touchState.panScrollOrigX = GetScrollPos(win.hwndCanvas, SB_HORZ);
            }
			else if (gi.dwFlags == GF_END)
			{
				//lsdebugout(TEXT(">onGesture: GF_end@(x,y)=(%d,%d).\r\n"), gi.ptsLocation.x, gi.ptsLocation.y);
				if (glsdoc)
				{
					POINT p = glsdoc->lsscreentoClient(gi.ptsLocation.x, gi.ptsLocation.y);
					glsdoc->lsonPanEnd(p.x, p.y);// gi.ptsLocation.x, gi.ptsLocation.y);
					glsdoc->onDragStop();
				}
			}
            else if (win.touchState.panStarted) {
                int deltaX = win.touchState.panPos.x - gi.ptsLocation.x;
                int deltaY = win.touchState.panPos.y - gi.ptsLocation.y;
                win.touchState.panPos = gi.ptsLocation;

                if ((!win.AsFixed() || !IsContinuous(win.AsFixed()->GetDisplayMode())) &&
                    (gi.dwFlags & GF_INERTIA) && abs(deltaX) > abs(deltaY)) {
                    // Switch pages once we hit inertia in a horizontal direction (only in
                    // non-continuous modes, cf. https://github.com/sumatrapdfreader/sumatrapdf/issues/9 )
                    if (deltaX < 0)
                        win.ctrl->GoToPrevPage();
                    else if (deltaX > 0)
                        win.ctrl->GoToNextPage();
                    // When we switch pages, go back to the initial scroll position
                    // and prevent further pan movement caused by the inertia
                    if (win.AsFixed())
                        win.AsFixed()->ScrollXTo(win.touchState.panScrollOrigX);
                    win.touchState.panStarted = false;
                }
                else if (win.AsFixed()) {
                    // Pan/Scroll
					bool isfastdrag = false;
					if (glsdoc)
					{
						POINT p = glsdoc->lsscreentoClient(gi.ptsLocation.x, gi.ptsLocation.y);
						isfastdrag = glsdoc->onDragging(win, p.x, p.y);
					}
					if (!isfastdrag)
						win.MoveDocBy(deltaX, deltaY);
                }
            }
            break;

        case GID_ROTATE:
            // Rotate the PDF 90 degrees in one direction
            if (gi.dwFlags == GF_END && win.AsFixed()) {
                // This is in radians
                double rads = GID_ROTATE_ANGLE_FROM_ARGUMENT(LODWORD(gi.ullArguments));
                // The angle from the rotate is the opposite of the Sumatra rotate, thus the negative
                double degrees = -rads * 180 / M_PI;

                // Playing with the app, I found that I often didn't go quit a full 90 or 180
                // degrees. Allowing rotate without a full finger rotate seemed more natural.
                if (degrees < -120 || degrees > 120)
                    win.AsFixed()->RotateBy(180);
                else if (degrees < -45)
                    win.AsFixed()->RotateBy(-90);
                else if (degrees > 45)
                    win.AsFixed()->RotateBy(90);
            }
            break;

        case GID_TWOFINGERTAP:
            // Two-finger tap toggles fullscreen mode
            OnMenuViewFullscreen(&win);
            break;

        case GID_PRESSANDTAP:
            // Toggle between Fit Page, Fit Width and Fit Content (same as 'z')
            if (gi.dwFlags == GF_BEGIN)
                win.ToggleZoom();
            break;

        default:
            // A gesture was not recognized
            break;
    }

    Touch::CloseGestureInfoHandle(hgi);
    return 0;
}

static LRESULT WndProcCanvasFixedPageUI(WindowInfo& win, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	// +ls@150307;
	//if (!glsdoc)
	{
		//lsselectDoc((WCHAR *)(win.tabs.Last()->GetTabTitle()));
		//lsselectDoc((WCHAR *)(win.currentTab->GetTabTitle()));
		lsupdateDoc((WCHAR *)(win.currentTab->GetTabTitle()));
	}
	if (glsdoc)
	{
		if (glsdoc->lswinfix == NULL)
			glsdoc->lswinfix = &win;
	}
    switch (msg) {
    case WM_PAINT:
        OnPaintDocument(win);
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_LBUTTONDOWN:
        OnMouseLeftButtonDown(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_LBUTTONUP:
        OnMouseLeftButtonUp(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_LBUTTONDBLCLK:
        OnMouseLeftButtonDblClk(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_MBUTTONDOWN:
        SetTimer(hwnd, SMOOTHSCROLL_TIMER_ID, SMOOTHSCROLL_DELAY_IN_MS, nullptr);
        // TODO: Create window that shows location of initial click for reference
        OnMouseMiddleButtonDown(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_RBUTTONDOWN:
        OnMouseRightButtonDown(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_RBUTTONUP:
        OnMouseRightButtonUp(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_RBUTTONDBLCLK:
        OnMouseRightButtonDblClick(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_VSCROLL:
        OnVScroll(win, wParam);
        return 0;

    case WM_HSCROLL:
        OnHScroll(win, wParam);
        return 0;

    case WM_MOUSEWHEEL:
        return CanvasOnMouseWheel(win, msg, wParam, lParam);

    case WM_MOUSEHWHEEL:
        return CanvasOnMouseHWheel(win, msg, wParam, lParam);

    case WM_SETCURSOR:
        if (OnSetCursor(win, hwnd))
            return TRUE;
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_CONTEXTMENU:
        OnContextMenu(&win, 0, 0);
        return 0;

    case WM_GESTURE:
        return OnGesture(win, msg, wParam, lParam);

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

///// methods needed for ChmUI canvases (should be subclassed by HtmlHwnd) /////

static LRESULT WndProcCanvasChmUI(WindowInfo& win, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_SETCURSOR:
        // TODO: make (re)loading a document always clear the infotip
        win.DeleteInfotip();
        return DefWindowProc(hwnd, msg, wParam, lParam);

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

///// methods needed for EbookUI canvases /////

static LRESULT CanvasOnMouseWheelEbook(WindowInfo& win, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Scroll the ToC sidebar, if it's visible and the cursor is in it
    if (win.tocVisible && IsCursorOverWindow(win.hwndTocTree) && !gWheelMsgRedirect) {
        // Note: hwndTocTree's window procedure doesn't always handle
        //       WM_MOUSEWHEEL and when it's bubbling up, we'd return
        //       here recursively - prevent that
        gWheelMsgRedirect = true;
        LRESULT res = SendMessage(win.hwndTocTree, message, wParam, lParam);
        gWheelMsgRedirect = false;
        return res;
    }

    short delta = GET_WHEEL_DELTA_WPARAM(wParam);
    if (delta > 0)
        win.ctrl->GoToPrevPage();
    else
        win.ctrl->GoToNextPage();
    return 0;
}

static LRESULT WndProcCanvasEbookUI(WindowInfo& win, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    bool wasHandled;
    LRESULT res = win.AsEbook()->HandleMessage(msg, wParam, lParam, wasHandled);
    if (wasHandled)
        return res;

    switch (msg) {
    case WM_SETCURSOR:
        // TODO: make (re)loading a document always clear the infotip
        win.DeleteInfotip();
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_MOUSEWHEEL:
        return CanvasOnMouseWheelEbook(win, msg, wParam, lParam);

    case WM_GESTURE:
        return OnGesture(win, msg, wParam, lParam);

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

///// methods needed for the About/Start screen /////

static void OnPaintAbout(WindowInfo& win)
{
    Timer t;
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(win.hwndCanvas, &ps);

    if (HasPermission(Perm_SavePreferences | Perm_DiskAccess) && gGlobalPrefs->rememberOpenedFiles && gGlobalPrefs->showStartPage) {
        DrawStartPage(win, win.buffer->GetDC(), gFileHistory, gRenderCache.textColor, gRenderCache.backgroundColor);
    } else {
        DrawAboutPage(win, win.buffer->GetDC());
    }
    win.buffer->Flush(hdc);

    EndPaint(win.hwndCanvas, &ps);
    if (gShowFrameRate) {
        ShowFrameRateDur(win.frameRateWnd, t.GetTimeInMs());
    }
}

static void OnMouseLeftButtonDownAbout(WindowInfo& win, int x, int y, WPARAM key)
{
    //lf("Left button clicked on %d %d", x, y);

    // remember a link under so that on mouse up we only activate
    // link if mouse up is on the same link as mouse down
    win.url = GetStaticLink(win.staticLinks, x, y);
}

static void OnMouseLeftButtonUpAbout(WindowInfo& win, int x, int y, WPARAM key)
{
    SetFocus(win.hwndFrame);

    const WCHAR *url = GetStaticLink(win.staticLinks, x, y);
    if (url && url == win.url) {
        if (str::Eq(url, SLINK_OPEN_FILE))
            SendMessage(win.hwndFrame, WM_COMMAND, IDM_OPEN, 0);
        else if (str::Eq(url, SLINK_LIST_HIDE)) {
            gGlobalPrefs->showStartPage = false;
            win.RedrawAll(true);
        } else if (str::Eq(url, SLINK_LIST_SHOW)) {
            gGlobalPrefs->showStartPage = true;
            win.RedrawAll(true);
        } else if (!str::StartsWithI(url, L"http:") &&
                   !str::StartsWithI(url, L"https:") &&
                   !str::StartsWithI(url, L"mailto:"))
        {
            LoadArgs args(url, &win);
            LoadDocument(args);
        } else
            LaunchBrowser(url);
    }
    win.url = nullptr;
}

static void OnMouseRightButtonDownAbout(WindowInfo& win, int x, int y, WPARAM key)
{
    //lf("Right button clicked on %d %d", x, y);
    SetFocus(win.hwndFrame);
    win.dragStart = PointI(x, y);
}

static void OnMouseRightButtonUpAbout(WindowInfo& win, int x, int y, WPARAM key)
{
    bool didDragMouse =
        abs(x - win.dragStart.x) > GetSystemMetrics(SM_CXDRAG) ||
        abs(y - win.dragStart.y) > GetSystemMetrics(SM_CYDRAG);
    if (!didDragMouse)
        OnAboutContextMenu(&win, x, y);
}

static LRESULT OnSetCursorAbout(WindowInfo& win, HWND hwnd)
{
    PointI pt;
    if (GetCursorPosInHwnd(hwnd, pt)) {
        StaticLinkInfo linkInfo;
        if (GetStaticLink(win.staticLinks, pt.x, pt.y, &linkInfo)) {
            win.CreateInfotip(linkInfo.infotip, linkInfo.rect);
            SetCursor(IDC_HAND);
        }
        else {
            win.DeleteInfotip();
            SetCursor(IDC_ARROW);
        }
        return TRUE;
    }

    win.DeleteInfotip();
    return FALSE;
}

static LRESULT WndProcCanvasAbout(WindowInfo& win, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        OnMouseLeftButtonDownAbout(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_LBUTTONUP:
        OnMouseLeftButtonUpAbout(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_LBUTTONDBLCLK:
        OnMouseLeftButtonDownAbout(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_RBUTTONDOWN:
        OnMouseRightButtonDownAbout(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_RBUTTONUP:
        OnMouseRightButtonUpAbout(win, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_SETCURSOR:
        if (OnSetCursorAbout(win, hwnd))
            return TRUE;
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_CONTEXTMENU:
        OnAboutContextMenu(&win, 0, 0);
        return 0;

    case WM_PAINT:
        OnPaintAbout(win);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

///// methods needed for FixedPageUI canvases with loading error /////

static void OnPaintError(WindowInfo& win)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(win.hwndCanvas, &ps);

    ScopedFont fontRightTxt(CreateSimpleFont(hdc, L"MS Shell Dlg", 14));
    HGDIOBJ hPrevFont = SelectObject(hdc, fontRightTxt);
    ScopedGdiObj<HBRUSH> brush(CreateSolidBrush(GetNoDocBgColor()));
    FillRect(hdc, &ps.rcPaint, brush);
    // TODO: should this be "Error opening %s"?
    ScopedMem<WCHAR> msg(str::Format(_TR("Error loading %s"), win.currentTab->filePath));
    DrawCenteredText(hdc, ClientRect(win.hwndCanvas), msg, IsUIRightToLeft());
    SelectObject(hdc, hPrevFont);

    EndPaint(win.hwndCanvas, &ps);
}

static LRESULT WndProcCanvasLoadError(WindowInfo& win, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_PAINT:
        OnPaintError(win);
        return 0;

    case WM_SETCURSOR:
        // TODO: make (re)loading a document always clear the infotip
        win.DeleteInfotip();
        return DefWindowProc(hwnd, msg, wParam, lParam);

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

///// methods needed for all types of canvas /////

void WindowInfo::RepaintAsync(UINT delay)
{
    // even though RepaintAsync is mostly called from the UI thread,
    // we depend on the repaint message to happen asynchronously
    uitask::Post([=]{
        if (!WindowInfoStillValid(this))
            return;
        if (!delay)
            WndProcCanvas(this->hwndCanvas, WM_TIMER, REPAINT_TIMER_ID, 0);
        else if (!this->delayedRepaintTimer)
            this->delayedRepaintTimer = SetTimer(this->hwndCanvas, REPAINT_TIMER_ID, delay, nullptr);
    });
}

static void OnTimer(WindowInfo& win, HWND hwnd, WPARAM timerId)
{
    PointI pt;

    switch (timerId) {
    case REPAINT_TIMER_ID:
        win.delayedRepaintTimer = 0;
        KillTimer(hwnd, REPAINT_TIMER_ID);
        win.RedrawAll();
        break;

    case SMOOTHSCROLL_TIMER_ID:
        if (MA_SCROLLING == win.mouseAction)
            win.MoveDocBy(win.xScrollSpeed, win.yScrollSpeed);
        else if (MA_SELECTING == win.mouseAction || MA_SELECTING_TEXT == win.mouseAction) {
            GetCursorPosInHwnd(win.hwndCanvas, pt);
            if (NeedsSelectionEdgeAutoscroll(&win, pt.x, pt.y))
                OnMouseMove(win, pt.x, pt.y, MK_CONTROL);
        }
        else {
            KillTimer(hwnd, SMOOTHSCROLL_TIMER_ID);
            win.yScrollSpeed = 0;
            win.xScrollSpeed = 0;
        }
        break;

    case HIDE_CURSOR_TIMER_ID:
        KillTimer(hwnd, HIDE_CURSOR_TIMER_ID);
        if (win.presentation)
            SetCursor((HCURSOR)nullptr);
        break;

    case HIDE_FWDSRCHMARK_TIMER_ID:
        win.fwdSearchMark.hideStep++;
        if (1 == win.fwdSearchMark.hideStep) {
            SetTimer(hwnd, HIDE_FWDSRCHMARK_TIMER_ID, HIDE_FWDSRCHMARK_DECAYINTERVAL_IN_MS, nullptr);
        }
        else if (win.fwdSearchMark.hideStep >= HIDE_FWDSRCHMARK_STEPS) {
            KillTimer(hwnd, HIDE_FWDSRCHMARK_TIMER_ID);
            win.fwdSearchMark.show = false;
            win.RepaintAsync();
        }
        else
            win.RepaintAsync();
        break;

    case AUTO_RELOAD_TIMER_ID:
        KillTimer(hwnd, AUTO_RELOAD_TIMER_ID);
        if (win.currentTab && win.currentTab->reloadOnFocus)
            ReloadDocument(&win, true);
        break;

    case EBOOK_LAYOUT_TIMER_ID:
        KillTimer(hwnd, EBOOK_LAYOUT_TIMER_ID);
        for (TabInfo *tab : win.tabs) {
            if (tab->AsEbook())
                tab->AsEbook()->TriggerLayout();
        }
        break;
    }
}

static void OnDropFiles(HDROP hDrop, bool dragFinish)
{
    WCHAR filePath[MAX_PATH];
    const int count = DragQueryFile(hDrop, DRAGQUERY_NUMFILES, 0, 0);

    for (int i = 0; i < count; i++) {
        DragQueryFile(hDrop, i, filePath, dimof(filePath));
        if (str::EndsWithI(filePath, L".lnk")) {
            ScopedMem<WCHAR> resolved(ResolveLnk(filePath));
            if (resolved)
                str::BufSet(filePath, dimof(filePath), resolved);
        }
        // The first dropped document may override the current window
        LoadArgs args(filePath);
        LoadDocument(args);
    }
    if (dragFinish)
        DragFinish(hDrop);
}

LRESULT CALLBACK WndProcCanvas(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // messages that don't require win
    switch (msg) {
    case WM_DROPFILES:
        CrashIf(lParam != 0 && lParam != 1);
        OnDropFiles((HDROP)wParam, !lParam);
        return 0;

    case WM_ERASEBKGND:
        // do nothing, helps to avoid flicker
        return TRUE;
    }

    WindowInfo *win = FindWindowInfoByHwnd(hwnd);
    if (!win)
        return DefWindowProc(hwnd, msg, wParam, lParam);

    // messages that require win
    switch (msg) {
    case WM_TIMER:
        OnTimer(*win, hwnd, wParam);
        return 0;

    case WM_SIZE:
        if (!IsIconic(win->hwndFrame))
            win->UpdateCanvasSize();
        return 0;

    case WM_GETOBJECT:
        // TODO: should we check for UiaRootObjectId, as in http://msdn.microsoft.com/en-us/library/windows/desktop/ff625912.aspx ???
        // On the other hand http://code.msdn.microsoft.com/windowsdesktop/UI-Automation-Clean-94993ac6/sourcecode?fileId=42883&pathId=2071281652
        // says that UiaReturnRawElementProvider() should be called regardless of lParam
        // Don't expose UIA automation in plugin mode yet. UIA is still too experimental
        if (!gPluginMode) {
            // disable UIAutomation in release builds until concurrency issues and
            // memory leaks have been figured out and fixed
#ifdef DEBUG
            if (win->CreateUIAProvider()) {
                // TODO: should win->uia_provider->Release() as in http://msdn.microsoft.com/en-us/library/windows/desktop/gg712214.aspx
                // and http://www.code-magazine.com/articleprint.aspx?quickid=0810112&printmode=true ?
                // Maybe instead of having a single provider per win, we should always create a new one
                // like in this sample: http://code.msdn.microsoft.com/windowsdesktop/UI-Automation-Clean-94993ac6/sourcecode?fileId=42883&pathId=2071281652
                // currently win->uia_provider refCount is really out of wack in WindowInfo::~WindowInfo
                // from logging it seems that UiaReturnRawElementProvider() increases refCount by 1
                // and since WM_GETOBJECT is called many times, it accumulates
                return uia::ReturnRawElementProvider(hwnd, wParam, lParam, win->uia_provider);
            }
#endif
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    default:
        // TODO: achieve this split through subclassing or different window classes
        if (win->AsFixed())
            return WndProcCanvasFixedPageUI(*win, hwnd, msg, wParam, lParam);

        if (win->AsChm())
            return WndProcCanvasChmUI(*win, hwnd, msg, wParam, lParam);

        if (win->AsEbook())
            return WndProcCanvasEbookUI(*win, hwnd, msg, wParam, lParam);

        if (win->IsAboutWindow())
            return WndProcCanvasAbout(*win, hwnd, msg, wParam, lParam);

        return WndProcCanvasLoadError(*win, hwnd, msg, wParam, lParam);
    }
}
