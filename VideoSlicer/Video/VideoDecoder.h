#ifndef __VIDEODECODER_H__
#define __VIDEODECODER_H__

#include <Windows.h>
#include <cstdint>

interface IVideoSample : public IUnknown
{
public:
	virtual HRESULT Lock ( LPVOID * buffer, uint64_t * length ) PURE;
	virtual HRESULT Unlock () PURE;
};

interface IVideoDecoder : public IUnknown
{
public:
	virtual HRESULT Initialize ( LPCWSTR filename ) PURE;

public:
	virtual HRESULT GetVideoSize ( uint32_t * width, uint32_t * height, uint32_t * stride ) PURE;
	virtual HRESULT GetDuration ( uint64_t * ret ) PURE;

public:
	virtual HRESULT SetReadPosition ( uint64_t pos ) PURE;

public:
	virtual HRESULT ReadSample ( IVideoSample ** sample, uint64_t * readPosition ) PURE;
};

HRESULT CreateMediaFoundationVideoDecoder ( IVideoDecoder ** decoder );
HRESULT CreateFFmpegVideoDecoder ( IVideoDecoder ** decoder );

#endif