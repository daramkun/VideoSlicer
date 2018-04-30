#define _CRT_SECURE_NO_WARNINGS

#ifdef _DEBUG
#	define _CRTDBG_MAP_ALLOC
#	include <cstdlib>
#	include <crtdbg.h>
#endif

#include <string>

#include <Windows.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <atlbase.h>

#include <VersionHelpers.h>

#include "Resources/resource.h"

#include "Video/VideoDecoder.h"
#include "Image/ImageEncoder.h"
#include "ThreadPool.h"

#pragma comment ( lib, "comctl32.lib" )

enum SAVEFILEFORMAT
{
	SFF_PNG = 201,
	SFF_JPEG_100 = 202,
	SFF_JPEG_80 = 203,
	SFF_JPEG_60 = 204,
};

std::wstring g_openedVideoFile;
std::wstring g_saveTo;

bool g_isStarted;
double g_progress;

HANDLE g_thread;
DWORD g_threadId;

SAVEFILEFORMAT g_saveFileFormat = SFF_JPEG_100;

std::wstring ConvertTimeStamp ( LONGLONG nanosec, LPCWSTR ext ) noexcept
{
	UINT millisec = ( UINT ) ( nanosec / 10000 );

	UINT hour = millisec / 1000 / 60 / 60;
	millisec -= hour * 1000 * 60 * 60;
	UINT minute = millisec / 1000 / 60;
	millisec -= minute * 1000 * 60;
	UINT second = millisec / 1000;
	millisec -= second * 1000;

	wchar_t temp [ 256 ];
	wsprintf ( temp, TEXT ( "%02dː%02dː%02d˙%03d.%s" ), hour, minute, second, millisec, ext );

	return temp;
}

void ErrorExit ( HWND owner, unsigned exitCode )
{
	TaskDialog ( owner, nullptr, TEXT ( "오류" ), TEXT ( "오류가 발생했습니다." ), 
		TEXT ( "Windows가 N 또는 KN 에디션이면서 미디어 기능 팩이 설치되어 있지 않거나, 동영상 파일이 잘못된 것으로 보입니다." ), 
		TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr );

	ExitProcess ( exitCode );
}

bool EncodingImageToFile ( IVideoSample * readedSample, LONGLONG readedTimeStamp,
	UINT width, UINT height, UINT stride ) noexcept
{
	CComPtr<IVideoSample> sample;
	*&sample = readedSample;

	BYTE * colorBuffer;
	uint64_t colorBufferLength;
	if ( FAILED ( sample->Lock ( ( LPVOID* ) &colorBuffer, &colorBufferLength ) ) )
		return false;

	std::wstring filename = ConvertTimeStamp ( readedTimeStamp, g_saveFileFormat == SFF_PNG ? TEXT ( "png" ) : TEXT ( "jpg" ) );
	wchar_t outputPath [ MAX_PATH ];
	PathCombine ( outputPath, g_saveTo.c_str (), filename.c_str () );
	
	ImageEncoderSettings settings;
	switch ( g_saveFileFormat )
	{
		case SFF_PNG:
			settings.codecType = IEC_PNG;
			settings.settings.png.interlace = false;
			settings.settings.png.filtering = true;
			break;

		case SFF_JPEG_100:
			settings.codecType = IEC_JPEG;
			settings.settings.jpeg.quality = 1.0f;
			settings.settings.jpeg.chromaSubsample = false;
			break;

		case SFF_JPEG_80:
			settings.codecType = IEC_JPEG;
			settings.settings.jpeg.quality = 0.8f;
			settings.settings.jpeg.chromaSubsample = true;
			break;
		case SFF_JPEG_60:
			settings.codecType = IEC_JPEG;
			settings.settings.jpeg.quality = 0.6f;
			settings.settings.jpeg.chromaSubsample = true;
			break;
	}
	settings.imageProp.width = width;
	settings.imageProp.height = height;
	settings.imageProp.stride = stride;

	if ( FAILED ( SaveImage ( outputPath, &settings, colorBuffer, colorBufferLength ) ) )
	{
		sample->Unlock ();
		return false;
	}

	sample->Unlock ();

	return true;
}

DWORD WINAPI DoSushi ( LPVOID ) noexcept
{
	CComPtr<IVideoDecoder> videoDecoder;
	//if ( FAILED ( CreateMediaFoundationVideoDecoder ( &videoDecoder ) ) )
	if ( FAILED ( CreateFFmpegVideoDecoder ( &videoDecoder ) ) )
	{
		ErrorExit ( nullptr, -5 );
		return -1;
	}

	if ( FAILED ( videoDecoder->Initialize ( g_openedVideoFile.c_str () ) ) )
	{
		ErrorExit ( nullptr, -5 );
		return -1;
	}

	uint64_t duration;
	if ( FAILED ( videoDecoder->GetDuration ( &duration ) ) )
	{
		ErrorExit ( nullptr, -5 );
		return -1;
	}

	uint32_t width, height, stride;
	if ( FAILED ( videoDecoder->GetVideoSize ( &width, &height, &stride ) ) )
	{
		ErrorExit ( nullptr, -5 );
		return -1;
	}
	
	g_progress = 0;
	g_isStarted = true;

	{
		ThreadPool threadPool ( std::thread::hardware_concurrency () );

		while ( g_isStarted )
		{
			if ( threadPool.taskSize () >= std::thread::hardware_concurrency () * 4 )
			{
				Sleep ( 1 );
				continue;
			}

			uint64_t readedTimeStamp = 0;
			CComPtr<IVideoSample> readedSample;

			if ( FAILED ( videoDecoder->ReadSample ( &readedSample, &readedTimeStamp ) ) )
				continue;

			if ( nullptr == readedSample )
				break;

			auto result = threadPool.enqueue ( EncodingImageToFile,
				readedSample.Detach (), readedTimeStamp,
				width, height, stride );
			
			g_progress = ( readedTimeStamp / 10000 ) / ( double ) ( duration / 10000 );
		}
	}

	g_progress = 1;

	return 0;
}

int WINAPI WinMain ( HINSTANCE hInstance, HINSTANCE, LPSTR, int )
{
#ifdef _DEBUG
	_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

	if ( !IsWindows7OrGreater () )
	{
		MessageBox ( nullptr, TEXT ( "이 프로그램은 Windows 7 이상만 대응하고 있습니다." ),
			TEXT ( "오류" ), MB_OK );
		return -1;
	}

	if ( FAILED ( CoInitializeEx ( nullptr, COINIT_APARTMENTTHREADED ) ) )
		return -1;

	HICON hIcon = LoadIcon ( hInstance, MAKEINTRESOURCE ( IDI_MAIN_ICON ) );

	TASKDIALOG_BUTTON buttonArray [] =
	{
		{ 101, TEXT ( "동영상 선택하기" ) },
		{ 102, TEXT ( "저장할 경로 지정하기" ) },
	};
	TASKDIALOG_BUTTON radioButtonArray [] =
	{
		{ 201, TEXT ( "PNG로 저장하기" ) },
		{ 202, TEXT ( "JPEG로 저장하기(100% 화질)" ) },
		{ 203, TEXT ( "JPEG로 저장하기(80% 화질)" ) },
		{ 204, TEXT ( "JPEG로 저장하기(60% 화질)" ) },
	};

	TASKDIALOGCONFIG mainConfig = { 0, };
	mainConfig.cbSize = sizeof ( TASKDIALOGCONFIG );
	mainConfig.hInstance = hInstance;
	mainConfig.hMainIcon = hIcon;
	mainConfig.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_USE_COMMAND_LINKS | TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER;
	mainConfig.pszWindowTitle = TEXT ( "영상 회뜨는 프로그램" );
	mainConfig.pszMainInstruction = TEXT ( "영상 회 떠드립니다." );
	mainConfig.pszContent = TEXT ( "지정된 경로에 선택한 동영상을 회떠서 프레임 하나하나 이미지 파일로 정성스럽게 저장해드립니다. 확인 버튼을 누르면 회 뜨기가 시작됩니다." );
	mainConfig.pszFooter = TEXT ( "이 프로그램은 Windows N/KN 에디션에서는 동작하지 않습니다. KN 및 N 에디션에서 구동하려면 아래 링크에서 소프트웨어를 설치해주세요.\n<A HREF=\"https://www.microsoft.com/ko-kr/download/details.aspx?id=16546\">Windows 7용 미디어 기능 팩</A>\n<A HREF=\"https://www.microsoft.com/ko-kr/download/details.aspx?id=40744\">Windows 8.1용 미디어 기능 팩</A>\n<A HREF=\"https://www.microsoft.com/ko-kr/download/details.aspx?id=48231\">Windows 10용 미디어 기능 팩</A>" );
	mainConfig.pButtons = buttonArray;
	mainConfig.cButtons = _countof ( buttonArray );
	mainConfig.pRadioButtons = radioButtonArray;
	mainConfig.cRadioButtons = _countof ( radioButtonArray );
	mainConfig.nDefaultRadioButton = 202;
	mainConfig.dwCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON;
	mainConfig.pfCallback = [] ( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData ) -> HRESULT
	{
		switch ( msg )
		{
			case TDN_CREATED:
				{
					SendMessage ( hWnd, TDM_ENABLE_BUTTON, IDOK, FALSE );
					SendMessage ( hWnd, TDM_ENABLE_BUTTON, 102, FALSE );
				}
				break;

			case TDN_BUTTON_CLICKED:
				{
					if ( wParam == 101 )
					{
						CComPtr<IFileOpenDialog> dialog;
						if ( FAILED ( CoCreateInstance ( CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
							IID_IFileOpenDialog, ( void ** ) &dialog ) ) )
							ErrorExit ( nullptr, -3 );

						COMDLG_FILTERSPEC fileTypes [] =
						{
							{ TEXT ( "지원하는 모든 파일(*.mp4;*.m4v;*.avi;*.wmv)" ), TEXT ( "*.mp4;*.m4v;*.avi;*.wmv" ) },
						};
						dialog->SetFileTypes ( _countof ( fileTypes ), fileTypes );

						dialog->SetOptions ( FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOTESTFILECREATE );

						if ( FAILED ( dialog->Show ( hWnd ) ) )
							return 2;

						IShellItem * selectedItem;
						if ( FAILED ( dialog->GetResult ( &selectedItem ) ) )
							ErrorExit ( nullptr, -4 );

						PWSTR filePath;
						if ( FAILED ( selectedItem->GetDisplayName ( SIGDN_FILESYSPATH, &filePath ) ) )
						{
							ErrorExit ( nullptr, -4 );
							return 0;
						}
						::g_openedVideoFile = filePath;
						CoTaskMemFree ( filePath );

						SendMessage ( hWnd, TDM_ENABLE_BUTTON, 101, FALSE );
						SendMessage ( hWnd, TDM_ENABLE_BUTTON, 102, TRUE );

						return 1;
					}
					else if ( wParam == 102 )
					{
						CComPtr<IFileOpenDialog> dialog;
						if ( FAILED ( CoCreateInstance ( CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
							IID_IFileOpenDialog, ( void ** ) &dialog ) ) )
							ErrorExit ( nullptr, -3 );

						dialog->SetOptions ( FOS_PATHMUSTEXIST | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOTESTFILECREATE );

						if ( FAILED ( dialog->Show ( hWnd ) ) )
							return 2;

						IShellItem * selectedItem;
						if ( FAILED ( dialog->GetResult ( &selectedItem ) ) )
							ErrorExit ( nullptr, -4 );

						PWSTR filePath;
						if ( FAILED ( selectedItem->GetDisplayName ( SIGDN_FILESYSPATH, &filePath ) ) )
						{
							ErrorExit ( nullptr, -4 );
							return 0;
						}
						::g_saveTo = filePath;
						CoTaskMemFree ( filePath );

						SendMessage ( hWnd, TDM_ENABLE_BUTTON, 102, FALSE );
						SendMessage ( hWnd, TDM_ENABLE_BUTTON, IDOK, TRUE );

						return 1;
					}
					else if ( wParam == IDOK )
					{
						SendMessage ( hWnd, TDM_ENABLE_BUTTON, IDOK, FALSE );
						::g_thread = CreateThread ( nullptr, 0, DoSushi, nullptr, 0, &g_threadId );
						return 1;
					}
					else if ( wParam == IDCANCEL )
					{
						TASKDIALOGCONFIG askDialog = { 0, };
						askDialog.cbSize = sizeof ( TASKDIALOGCONFIG );
						askDialog.pszMainIcon = TD_WARNING_ICON;
						askDialog.hwndParent = hWnd;
						askDialog.pszWindowTitle = TEXT ( "질문" );
						askDialog.pszMainInstruction = TEXT ( "종료하시겠습니까?" );
						askDialog.pszContent = TEXT ( "진행 중이던 작업을 모두 잃게 됩니다." );
						askDialog.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;

						int button;
						HRESULT hr;
						if ( FAILED ( hr = TaskDialogIndirect ( &askDialog, &button, nullptr, nullptr ) ) )
							return 0;

						if ( button == IDNO )
							return 1;

						if ( ::g_thread != NULL )
						{
							g_isStarted = false;
							WaitForSingleObject ( g_thread, INFINITE );
						}
					}
				}
				break;

			case TDN_RADIO_BUTTON_CLICKED:
				{
					::g_saveFileFormat = ( SAVEFILEFORMAT ) wParam;
				}
				break;

			case TDN_HYPERLINK_CLICKED:
				{
					ShellExecute ( nullptr, TEXT ( "open" ), ( LPWSTR ) lParam,
						nullptr, nullptr, SW_SHOW );
				}
				break;

			case TDN_TIMER:
				{
					if ( !::g_isStarted )
						return 1;

					SendMessage ( hWnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM ( 0, 100 ) );
					SendMessage ( hWnd, TDM_SET_PROGRESS_BAR_POS, ( WPARAM ) ( ::g_progress * 100 ), 0 );

					if ( abs ( ::g_progress - 1 ) <= FLT_EPSILON )
					{
						TASKDIALOGCONFIG taskDialog = { 0, };
						taskDialog.cbSize = sizeof ( TASKDIALOGCONFIG );
						taskDialog.pszMainIcon = TD_INFORMATION_ICON;
						taskDialog.pszWindowTitle = TEXT ( "안내" );
						taskDialog.pszMainInstruction = TEXT ( "작업이 완료되었습니다." );
						taskDialog.pszContent = TEXT ( "작업이 완료되어 프로그램을 종료합니다." );
						taskDialog.dwCommonButtons = TDCBF_OK_BUTTON;
						SendMessage ( hWnd, TDM_NAVIGATE_PAGE, 0, ( LPARAM ) &taskDialog );
					}
				}
				break;
		}

		return 0;
	};

	if ( FAILED ( TaskDialogIndirect ( &mainConfig, nullptr, nullptr, nullptr ) ) )
		return -2;

	CoUninitialize ();

	return 0;
}