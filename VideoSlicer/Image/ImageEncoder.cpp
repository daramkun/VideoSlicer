#include "ImageEncoder.h"

#include <atlbase.h>
#include <Wincodec.h>

#pragma comment ( lib, "windowscodecs.lib" )

HRESULT SaveImage ( LPCWSTR filename, const ImageEncoderSettings * settings, LPVOID buffer, uint64_t bufferLength )
{
	HRESULT hr;

	CComPtr<IWICImagingFactory> imagingFactory;
	if ( FAILED ( hr = CoCreateInstance ( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory, ( LPVOID* ) &imagingFactory ) ) )
		return hr;

	GUID containerFormat;
	switch ( settings->codecType )
	{
		case IEC_JPEG: containerFormat = GUID_ContainerFormatJpeg; break;
		case IEC_PNG: containerFormat = GUID_ContainerFormatPng; break;
	}

	CComPtr<IWICBitmapEncoder> encoder;
	if ( FAILED ( hr = imagingFactory->CreateEncoder (
		containerFormat, nullptr, &encoder ) ) )
		return hr;

	CComPtr<IStream> outputStream;
	if ( FAILED ( hr = SHCreateStreamOnFile ( filename, STGM_WRITE | STGM_CREATE, &outputStream ) ) )
		return hr;

	if ( FAILED ( hr = encoder->Initialize ( outputStream, WICBitmapEncoderNoCache ) ) )
		return hr;

	CComPtr<IWICBitmapFrameEncode> frameEncode;
	CComPtr<IPropertyBag2> encoderOptions;
	if ( FAILED ( encoder->CreateNewFrame ( &frameEncode, &encoderOptions ) ) )
		return hr;

	PROPBAG2 propBag2 = { 0 };
	VARIANT variant;
	if ( containerFormat == GUID_ContainerFormatPng )
	{
		propBag2.pstrName = ( LPOLESTR ) L"InterlaceOption";
		VariantInit ( &variant );
		variant.vt = VT_BOOL;
		variant.boolVal = settings->settings.png.interlace ? VARIANT_TRUE : VARIANT_FALSE;
		encoderOptions->Write ( 0, &propBag2, &variant );

		propBag2.pstrName = ( LPOLESTR ) L"FilterOption";
		VariantInit ( &variant );
		variant.vt = VT_UI1;
		variant.bVal = WICPngFilterAdaptive;
		encoderOptions->Write ( 1, &propBag2, &variant );
	}
	else if ( containerFormat == GUID_ContainerFormatJpeg )
	{
		propBag2.pstrName = ( LPOLESTR ) L"ImageQuality";
		VariantInit ( &variant );
		variant.vt = VT_R4;
		variant.fltVal = settings->settings.jpeg.quality;
		encoderOptions->Write ( 0, &propBag2, &variant );

		propBag2.pstrName = ( LPOLESTR ) L"JpegYCrCbSubsampling";
		VariantInit ( &variant );
		variant.vt = VT_UI1;
		variant.bVal = settings->settings.jpeg.chromaSubsample
			? WICJpegYCrCbSubsampling444
			: WICJpegYCrCbSubsampling420;
		encoderOptions->Write ( 1, &propBag2, &variant );
	}

	if ( FAILED ( hr = frameEncode->Initialize ( encoderOptions ) ) )
		return hr;

	WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
	if ( settings->imageProp.width * 4 == settings->imageProp.stride )
		pixelFormat = GUID_WICPixelFormat32bppBGR;
	frameEncode->SetPixelFormat ( &pixelFormat );
	frameEncode->SetSize ( settings->imageProp.width, settings->imageProp.height );
	frameEncode->WritePixels ( settings->imageProp.height, settings->imageProp.stride, bufferLength, ( BYTE* ) buffer );

	frameEncode->Commit ();
	encoder->Commit ();

	return S_OK;
}
