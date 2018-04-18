#define _CRT_SECURE_NO_WARNINGS
#include <string>

#include <Windows.h>
#include <CommCtrl.h>
#include <ShlObj.h>
#include <atlbase.h>
#include <Propvarutil.h>
#include <Wincodec.h>

#include <VersionHelpers.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfplay.h>
#include <mftransform.h>

#include "Resources/resource.h"

#if defined _M_IX86
#pragma comment ( linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"" )
#elif defined _M_X64
#pragma comment ( linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"" )
#else
#pragma comment ( linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"" )
#endif

#pragma comment ( lib, "comctl32.lib" )
#pragma comment ( lib, "mfplat.lib" )
#pragma comment ( lib, "mfuuid.lib" )
#pragma comment ( lib, "mfreadwrite.lib" )
#pragma comment ( lib, "Propsys.lib" )
#pragma comment ( lib, "windowscodecs.lib" )

enum SAVEFILEFORMAT
{
	SFF_PNG = 201,
	SFF_JPEG = 202,
};

std::wstring g_openedVideoFile;
std::wstring g_saveTo;

bool g_isStarted;
double g_progress;

HANDLE g_thread;
DWORD g_threadId;

SAVEFILEFORMAT g_saveFileFormat = SFF_JPEG;

IMFMediaType * CreateInputMediaType ( IMFMediaType * inputMediaType )
{
	CComPtr<IMFMediaType> ret;
	if ( FAILED ( MFCreateMediaType ( &ret ) ) ) return nullptr;
	ret->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Video );
	ret->SetGUID ( MF_MT_SUBTYPE, MFVideoFormat_RGB24 );
	return ret.Detach ();
}

DWORD WINAPI DoSushi ( LPVOID )
{
	MFStartup ( MF_VERSION );

	CComPtr<IWICImagingFactory> imagingFactory;
	if ( FAILED ( CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory, ( LPVOID* ) &imagingFactory ) ) )
		ExitProcess ( -5 );

	CComPtr<IMFAttributes> attribute;
	if ( FAILED ( MFCreateAttributes ( &attribute, 0 ) ) )
		ExitProcess ( -5 );

	attribute->SetUINT32 ( MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1 );
	attribute->SetUINT32 ( MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE );

	CComPtr<IMFSourceReader> sourceReader;

	HRESULT hr;
	if ( FAILED ( hr = MFCreateSourceReaderFromURL ( g_openedVideoFile.c_str (), attribute, &sourceReader ) ) )
		ExitProcess ( -5 );

	DWORD target, temp1; LONGLONG temp2; IMFSample * temp3;
	if ( FAILED ( sourceReader->ReadSample ( MF_SOURCE_READER_FIRST_VIDEO_STREAM, MF_SOURCE_READER_CONTROLF_DRAIN, &target, &temp1, &temp2, &temp3 ) ) )
		ExitProcess ( -5 );
	DWORD readerVideoStreamIndex = target;

	CComPtr<IMFMediaType> videoMediaType, decoderMediaType;
	if ( FAILED ( sourceReader->GetNativeMediaType ( readerVideoStreamIndex, 0, &videoMediaType ) ) )
		ExitProcess ( -5 );
	decoderMediaType = CreateInputMediaType ( videoMediaType );
	if ( FAILED ( sourceReader->SetCurrentMediaType ( readerVideoStreamIndex, nullptr, decoderMediaType ) ) )
		ExitProcess ( -5 );

	UINT width, height;
	if ( FAILED ( MFGetAttributeSize ( videoMediaType, MF_MT_FRAME_SIZE, &width, &height ) ) )
		ExitProcess ( -5 );
	UINT stride = ( width * 24 + 7 ) / 8;

	PROPVARIANT var;
	if ( FAILED ( sourceReader->GetPresentationAttribute ( MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var ) ) )
		ExitProcess ( -5 );

	LONGLONG duration;
	PropVariantToInt64 ( var, &duration );
	PropVariantClear ( &var );

	g_progress = 0;
	g_isStarted = true;

	while ( true )
	{
		DWORD actualStreamIndex, streamFlags;
		LONGLONG readedTimeStamp = 0;
		CComPtr<IMFSample> readedSample;

		if ( FAILED ( sourceReader->ReadSample ( MF_SOURCE_READER_FIRST_VIDEO_STREAM, 
			0, &actualStreamIndex, &streamFlags, &readedTimeStamp, &readedSample ) ) )
			continue;

		if ( MF_SOURCE_READERF_ENDOFSTREAM == streamFlags && nullptr == readedSample )
			break;

		CComPtr<IMFMediaBuffer> buffer;
		if ( FAILED ( readedSample->ConvertToContiguousBuffer ( &buffer ) ) )
			continue;

		BYTE * colorBuffer;
		DWORD colorBufferMaxLength, colorBufferLength;
		if ( FAILED ( buffer->Lock ( &colorBuffer, &colorBufferMaxLength, &colorBufferLength ) ) )
			continue;

		CComPtr<IWICBitmapEncoder> encoder;
		if ( FAILED ( imagingFactory->CreateEncoder (
			g_saveFileFormat == SFF_PNG ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg,
			nullptr, &encoder ) ) )
			ExitProcess ( -6 );

		CComPtr<IStream> outputStream;
		wchar_t filename [ MAX_PATH ];
		_itow ( ( int ) ( readedTimeStamp / 100000 ), filename, 10 );
		wcscat ( filename, g_saveFileFormat == SFF_PNG ? TEXT ( ".png" ) : TEXT ( ".jpg" ) );
		wchar_t outputPath [ MAX_PATH ];
		PathCombine ( outputPath, g_saveTo.c_str (), filename );
		if ( FAILED ( SHCreateStreamOnFile ( outputPath, STGM_WRITE | STGM_CREATE, &outputStream ) ) )
			continue;

		if ( FAILED ( encoder->Initialize ( outputStream, WICBitmapEncoderNoCache ) ) )
			ExitProcess ( -6 );

		CComPtr<IWICBitmapFrameEncode> frameEncode;
		CComPtr<IPropertyBag2> encoderOptions;
		if ( FAILED ( encoder->CreateNewFrame ( &frameEncode, &encoderOptions ) ) )
			ExitProcess ( -6 );

		PROPBAG2 propBag2 = { 0 };
		VARIANT variant;
		if ( g_saveFileFormat == SFF_PNG )
		{
			propBag2.pstrName = ( LPOLESTR ) L"InterlaceOption";
			VariantInit ( &variant );
			variant.vt = VT_BOOL;
			variant.boolVal = VARIANT_FALSE;
			encoderOptions->Write ( 0, &propBag2, &variant );

			propBag2.pstrName = ( LPOLESTR ) L"FilterOption";
			VariantInit ( &variant );
			variant.vt = VT_UI1;
			variant.bVal = WICPngFilterAdaptive;
			encoderOptions->Write ( 1, &propBag2, &variant );
		}
		else if ( g_saveFileFormat == SFF_JPEG )
		{
			propBag2.pstrName = ( LPOLESTR ) L"ImageQuality";
			VariantInit ( &variant );
			variant.vt = VT_R4;
			variant.fltVal = 0.8f;
			encoderOptions->Write ( 0, &propBag2, &variant );

			propBag2.pstrName = ( LPOLESTR ) L"ImageQuality";
			VariantInit ( &variant );
			variant.vt = VT_UI1;
			variant.bVal = WICJpegYCrCbSubsampling420;
			encoderOptions->Write ( 0, &propBag2, &variant );
		}

		if ( FAILED ( frameEncode->Initialize ( encoderOptions ) ) )
			ExitProcess ( -6 );

		WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
		frameEncode->SetPixelFormat ( &pixelFormat );
		frameEncode->SetSize ( width, height );
		frameEncode->WritePixels ( height, stride, colorBufferLength, colorBuffer );

		frameEncode->Commit ();
		encoder->Commit ();

		buffer->Unlock ();

		g_progress = readedTimeStamp / ( double ) duration;
	}

	MFShutdown ();

	g_progress = 1;

	return 0;
}

int WINAPI WinMain ( HINSTANCE hInstance, HINSTANCE, LPSTR, int )
{
	if ( !IsWindows7OrGreater () )
	{
		MessageBox ( nullptr, TEXT ( "이 프로그램은 Windows 7 이상만 대응하고 있습니다." ),
			TEXT ( "오류" ), MB_OK );
		return -1;
	}

	if ( FAILED ( CoInitialize ( nullptr ) ) )
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
		{ 202, TEXT ( "JPEG로 저장하기" ) },
	};

	TASKDIALOGCONFIG mainConfig = { 0, };
	mainConfig.cbSize = sizeof ( TASKDIALOGCONFIG );
	mainConfig.hInstance = hInstance;
	mainConfig.hMainIcon = hIcon;
	mainConfig.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_USE_HICON_MAIN | TDF_USE_COMMAND_LINKS | TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER;
	mainConfig.pszWindowTitle = TEXT ( "영상 회뜨는 프로그램" );
	mainConfig.pszMainInstruction = TEXT ( "영상 회 떠드립니다." );
	mainConfig.pszContent = TEXT ( "지정된 경로에 선택한 동영상을 회떠서 프레임 하나하나 이미지 파일로 정성스럽게 저장해드립니다. 확인 버튼을 누르면 회 뜨기가 시작됩니다." );
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
							ExitProcess ( -3 );

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
							ExitProcess ( -4 );

						PWSTR filePath;
						if ( FAILED ( selectedItem->GetDisplayName ( SIGDN_FILESYSPATH, &filePath ) ) )
							ExitProcess ( -4 );
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
							ExitProcess ( -3 );

						dialog->SetOptions ( FOS_PATHMUSTEXIST | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOTESTFILECREATE );

						if ( FAILED ( dialog->Show ( hWnd ) ) )
							return 2;

						IShellItem * selectedItem;
						if ( FAILED ( dialog->GetResult ( &selectedItem ) ) )
							ExitProcess ( -4 );

						PWSTR filePath;
						if ( FAILED ( selectedItem->GetDisplayName ( SIGDN_FILESYSPATH, &filePath ) ) )
							ExitProcess ( -4 );
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
							TerminateThread ( g_thread, 0 );
					}
				}
				break;

			case TDN_RADIO_BUTTON_CLICKED:
				{
					::g_saveFileFormat = ( SAVEFILEFORMAT ) wParam;
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