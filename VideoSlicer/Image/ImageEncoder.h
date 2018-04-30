#ifndef __IMAGEENCODER_H__
#define __IMAGEENCODER_H__

#include <Windows.h>

#include <cstdint>

enum ImageEncoderCodec
{
	IEC_UNKNOWN,
	IEC_PNG,
	IEC_JPEG
};

struct ImageEncoderSettings
{
	ImageEncoderCodec codecType;
	struct
	{
		uint32_t width, height, stride;
	} imageProp;
	union
	{
		struct
		{
			bool interlace;
			bool filtering;
		} png;
		struct
		{
			float quality;
			bool chromaSubsample;
		} jpeg;
	} settings;
};

HRESULT SaveImage ( LPCWSTR filename, const ImageEncoderSettings * settings,
	LPVOID buffer, uint64_t bufferLength );

#endif