// VcamTest.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//

#include <streams.h>
#include <initguid.h>
#include "virtual_cam.h"

#define CreateComObject(clsid, iid, var) CoCreateInstance(clsid, NULL, \
CLSCTX_INPROC_SERVER, iid, (void **)&var);

STDAPI AMovieSetupRegisterServer(CLSID clsServer, LPCWSTR szDescription,
	LPCWSTR szFileName, LPCWSTR szThreadingModel = L"Both",
	LPCWSTR szServerType = L"InprocServer32");
STDAPI AMovieSetupUnregisterServer(CLSID clsServer);

const AMOVIESETUP_MEDIATYPE AMSMediaTypesV =
{
	&MEDIATYPE_Video,
	&MEDIASUBTYPE_NULL
};

const AMOVIESETUP_PIN AMSPinV =
{
	L"Output",
	FALSE,
	TRUE,
	FALSE,
	FALSE,
	&CLSID_NULL,
	NULL,
	1,
	&AMSMediaTypesV
};

const AMOVIESETUP_FILTER AMSFilterV =
{
	&CLSID_VirtualCam,
	L"VirtualCam",
	MERIT_DO_NOT_USE,
	1,
	&AMSPinV
};

CFactoryTemplate g_Templates[1] =
{
	{
		L"VirtualCam",
		&CLSID_VirtualCam,
		CVCam::CreateInstance,
		NULL,
		&AMSFilterV
	}
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

STDAPI RegisterFilters(BOOL bRegister, int reg_video_filters)
{
	HRESULT hr = NOERROR;

	WCHAR achFileName[MAX_PATH];
	char achTemp[MAX_PATH];
	ASSERT(g_hInst != 0);

	if (0 == GetModuleFileNameA(g_hInst, achTemp, sizeof(achTemp)))
		return AmHresultFromWin32(GetLastError());

	MultiByteToWideChar(CP_ACP, 0L, achTemp, lstrlenA(achTemp) + 1,
		achFileName, NUMELMS(achFileName));

	hr = CoInitialize(0);
	if (bRegister) {

		for (int i = 0; i < reg_video_filters; i++) {
			hr |= AMovieSetupRegisterServer(*(g_Templates[i].m_ClsID),
				g_Templates[i].m_Name, achFileName);
		}

	}

	if (SUCCEEDED(hr)) {

		IFilterMapper2* fm = 0;
		hr = CreateComObject(CLSID_FilterMapper2, IID_IFilterMapper2, fm);

		if (SUCCEEDED(hr)) {
			if (bRegister) {
				IMoniker* moniker_audio = 0;
				REGFILTER2 rf2;
				rf2.dwVersion = 1;
				rf2.dwMerit = MERIT_DO_NOT_USE;
				rf2.cPins = 1;
				rf2.rgPins = &AMSPinV;
				for (int i = 0; i < reg_video_filters; i++) {
					IMoniker* moniker_video = 0;
					hr = fm->RegisterFilter(*(g_Templates[i].m_ClsID),
						g_Templates[i].m_Name, &moniker_video,
						&CLSID_VideoInputDeviceCategory, NULL, &rf2);
				}

			}
			else {
				for (int i = 0; i < reg_video_filters; i++) {
					hr = fm->UnregisterFilter(&CLSID_VideoInputDeviceCategory, 0,
						*(g_Templates[i].m_ClsID));
				}
			}
		}

		if (fm)
			fm->Release();
	}

	if (SUCCEEDED(hr) && !bRegister) {
		for (int i = 0; i < reg_video_filters; i++) {
			hr = AMovieSetupUnregisterServer(*(g_Templates[i].m_ClsID));
		}
	}

	CoFreeUnusedLibraries();
	CoUninitialize();
	return hr;
}

STDAPI DllInstall(BOOL bInstall, _In_opt_ LPCWSTR pszCmdLine)
{
	if (!bInstall)
		return RegisterFilters(FALSE, 1);
	else
		return RegisterFilters(TRUE, 1);
}

STDAPI DllRegisterServer()
{
	return RegisterFilters(TRUE, 1);
}

STDAPI DllUnregisterServer()
{
	return RegisterFilters(FALSE, 1);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  dwReason, LPVOID lpReserved)
{
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}

