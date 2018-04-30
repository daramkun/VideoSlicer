#include "VideoDecoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#include <VersionHelpers.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfplay.h>
#include <mftransform.h>

#include <Propvarutil.h>

#include <atlbase.h>

#pragma comment ( lib, "mfplat.lib" )
#pragma comment ( lib, "mfuuid.lib" )
#pragma comment ( lib, "mfreadwrite.lib" )

#pragma comment ( lib, "Propsys.lib" )

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

class MFVideoSample : public IVideoSample
{
public:
	MFVideoSample ( IMFSample * sample );
	virtual ~MFVideoSample ();

public:
	virtual HRESULT QueryInterface ( REFIID riid,
		_COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject );
	virtual ULONG AddRef ();
	virtual ULONG Release ();

public:
	virtual HRESULT Lock ( LPVOID * buffer, uint64_t * length );
	virtual HRESULT Unlock ();

private:
	ULONG _refCount;

	CComPtr<IMFSample> _sample;
	CComPtr<IMFMediaBuffer> _buffer;
};

class MFVideoDecoder : public IVideoDecoder
{
public:
	MFVideoDecoder ();
	virtual ~MFVideoDecoder ();

public:
	virtual HRESULT QueryInterface ( REFIID riid,
		_COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject );
	virtual ULONG AddRef ();
	virtual ULONG Release ();

public:
	virtual HRESULT Initialize ( LPCWSTR filename );

public:
	virtual HRESULT GetVideoSize ( uint32_t * width, uint32_t * height, uint32_t * stride );
	virtual HRESULT GetDuration ( uint64_t * ret );

public:
	virtual HRESULT SetReadPosition ( uint64_t pos );

public:
	virtual HRESULT ReadSample ( IVideoSample ** sample, uint64_t * readPosition );

private:
	ULONG _refCount;

	CComPtr<IMFSourceReader> _source;
	DWORD _streamIndex;

	CComPtr<IMFMediaType> _videoMediaType;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

HRESULT CreateMediaFoundationVideoDecoder ( IVideoDecoder ** decoder )
{
	*decoder = new MFVideoDecoder;
	return S_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

MFVideoSample::MFVideoSample ( IMFSample * sample )
	: _refCount ( 1 )
{
	*&_sample = sample;
	sample->ConvertToContiguousBuffer ( &_buffer );
}
MFVideoSample::~MFVideoSample () { }

HRESULT MFVideoSample::QueryInterface ( REFIID riid, void ** ppvObject )
{
	if ( riid == __uuidof ( IUnknown ) )
	{
		*ppvObject = this;
		return S_OK;
	}
	return E_FAIL;
}
ULONG MFVideoSample::AddRef ()
{
	return InterlockedIncrement ( &_refCount );
}
ULONG MFVideoSample::Release ()
{
	ULONG ret = InterlockedDecrement ( &_refCount );
	if ( ret <= 0 )
		delete this;
	return ret;
}

HRESULT MFVideoSample::Lock ( LPVOID * buffer, uint64_t * length )
{
	DWORD maxLength;
	return _buffer->Lock ( ( BYTE ** ) buffer, &maxLength, ( DWORD* ) length );
}
HRESULT MFVideoSample::Unlock ()
{
	return _buffer->Unlock ();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

MFVideoDecoder::MFVideoDecoder () : _refCount ( 1 ) { }
MFVideoDecoder::~MFVideoDecoder () { MFShutdown (); }

HRESULT MFVideoDecoder::QueryInterface ( REFIID riid, void ** ppvObject )
{
	if ( riid == __uuidof ( IUnknown ) )
	{
		*ppvObject = this;
		return S_OK;
	}
	return E_FAIL;
}
ULONG MFVideoDecoder::AddRef ()
{
	return InterlockedIncrement ( &_refCount );
}
ULONG MFVideoDecoder::Release ()
{
	ULONG ret = InterlockedDecrement ( &_refCount );
	if ( ret <= 0 )
		delete this;
	return ret;
}

HRESULT MFVideoDecoder::Initialize ( LPCWSTR filename )
{
	HRESULT hr;
	if ( FAILED ( hr = MFStartup ( MF_VERSION ) ) )
		return hr;

	CComPtr<IMFAttributes> attribute;
	if ( FAILED ( hr = MFCreateAttributes ( &attribute, 0 ) ) )
		return hr;

	attribute->SetUINT32 ( MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE );
	if ( IsWindows8OrGreater () )
		attribute->SetUINT32 ( MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE );

	if ( FAILED ( hr = MFCreateSourceReaderFromURL ( filename, attribute, &_source ) ) )
		return hr;

	DWORD temp1; LONGLONG temp2; IMFSample * temp3;
	if ( FAILED ( hr = _source->ReadSample ( MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		MF_SOURCE_READER_CONTROLF_DRAIN, &_streamIndex, &temp1, &temp2, &temp3 ) ) )
		return hr;

	if ( FAILED ( hr = _source->GetNativeMediaType ( _streamIndex, 0, &_videoMediaType ) ) )
		return hr;

	CComPtr<IMFMediaType> decoderMediaType;
	if ( FAILED ( hr = MFCreateMediaType ( &decoderMediaType ) ) ) return hr;
	decoderMediaType->SetGUID ( MF_MT_MAJOR_TYPE, MFMediaType_Video );
	decoderMediaType->SetGUID ( MF_MT_SUBTYPE, MFVideoFormat_RGB24 );
	if ( FAILED ( hr = _source->SetCurrentMediaType ( _streamIndex, nullptr, decoderMediaType ) ) )
		return hr;

	return S_OK;
}

HRESULT MFVideoDecoder::GetVideoSize ( uint32_t * width, uint32_t * height, uint32_t * stride )
{
	HRESULT hr;
	if ( FAILED ( hr = MFGetAttributeSize ( _videoMediaType, MF_MT_FRAME_SIZE, width, height ) ) )
		return hr;

	*stride = ( *width * 24 + 7 ) / 8;

	return S_OK;
}

HRESULT MFVideoDecoder::GetDuration ( uint64_t * ret )
{
	PROPVARIANT var;
	HRESULT hr = _source->GetPresentationAttribute ( MF_SOURCE_READER_MEDIASOURCE,
		MF_PD_DURATION, &var );
	if ( FAILED ( hr ) ) return hr;

	PropVariantToInt64 ( var, ( LONGLONG* ) ret );
	PropVariantClear ( &var );

	return S_OK;
}

HRESULT MFVideoDecoder::SetReadPosition ( uint64_t pos )
{
	PROPVARIANT prop = { 0, };
	prop.vt = VT_I8;
	prop.hVal.QuadPart = pos;
	return _source->SetCurrentPosition(GUID_NULL, prop );
}

HRESULT MFVideoDecoder::ReadSample ( IVideoSample ** sample, uint64_t * readPosition )
{
	DWORD streamFlags;
	CComPtr<IMFSample> s;
	HRESULT hr = _source->ReadSample ( _streamIndex, 0, nullptr,
		&streamFlags, ( LONGLONG * ) readPosition, &s );

	if ( MF_SOURCE_READERF_ENDOFSTREAM == streamFlags && nullptr == s )
	{
		*sample = nullptr;
		return S_OK;
	}

	if ( s == nullptr )
		return E_FAIL;

	*sample = new MFVideoSample ( s.Detach () );

	return S_OK;
}
