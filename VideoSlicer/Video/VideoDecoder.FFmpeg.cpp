#include "VideoDecoder.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

#include <cinttypes>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

#include <atlconv.h>

#pragma comment ( lib, "avcodec.lib" )
#pragma comment ( lib, "avfilter.lib" )
#pragma comment ( lib, "avformat.lib" )
#pragma comment ( lib, "avutil.lib" )
#pragma comment ( lib, "swscale.lib" )

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

class FFVideoSample : public IVideoSample
{
public:
	FFVideoSample ( AVCodecContext * codecContext, AVFrame * frame );
	virtual ~FFVideoSample ();

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

	uint8_t * array;
	uint64_t arraySize;
};

class FFVideoDecoder : public IVideoDecoder
{
public:
	FFVideoDecoder ();
	virtual ~FFVideoDecoder ();

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

	AVFormatContext* _formatContext;
	AVFrame * _frame;
	AVCodec * _codec;
	AVCodecContext * _codecContext;
	AVPacket * _packet;

	int64_t _duration;

	int _streamIndex;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

HRESULT CreateFFmpegVideoDecoder ( IVideoDecoder ** decoder )
{
	*decoder = new FFVideoDecoder ();
	return S_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

FFVideoSample::FFVideoSample ( AVCodecContext * codecContext, AVFrame * frame )
	: _refCount ( 1 )
{
	SwsContext * swsContext = sws_getContext ( codecContext->width, codecContext->height,
		codecContext->pix_fmt, codecContext->width, codecContext->height,
		AV_PIX_FMT_BGR24, SWS_BICUBIC, NULL, NULL, NULL );

	int width  = codecContext->width;
	int height = codecContext->height;
	int stride = ( width * 24 + 7 ) / 8;
	arraySize = av_image_get_buffer_size ( AV_PIX_FMT_BGR24, width, height, stride );
	array = ( uint8_t* ) av_mallocz ( arraySize );

	sws_scale ( swsContext, frame->data, frame->linesize,
		0, height, &array, &stride );
}

FFVideoSample::~FFVideoSample ()
{
	if ( array )
	{
		av_free ( array );
	}
}

HRESULT FFVideoSample::QueryInterface ( REFIID riid, void ** ppvObject )
{
	if ( riid == __uuidof ( IUnknown ) )
	{
		*ppvObject = this;
		return S_OK;
	}
	return E_FAIL;
}
ULONG FFVideoSample::AddRef ()
{
	return InterlockedIncrement ( &_refCount );
}
ULONG FFVideoSample::Release ()
{
	ULONG ret = InterlockedDecrement ( &_refCount );
	if ( ret <= 0 )
		delete this;
	return ret;
}

HRESULT FFVideoSample::Lock ( LPVOID * buffer, uint64_t * length )
{
	*buffer = array;
	*length = arraySize;
	return S_OK;
}

HRESULT FFVideoSample::Unlock ()
{
	return S_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

FFVideoDecoder::FFVideoDecoder ()
	: _refCount ( 1 )
	, _formatContext ( nullptr )
	, _frame ( nullptr )
	, _codec ( nullptr )
	, _codecContext ( nullptr )
{

}

FFVideoDecoder::~FFVideoDecoder ()
{
	if ( _packet )
	{
		av_free ( _packet->data );
		_packet->data = nullptr;
		av_packet_free ( &_packet );
	}

	if ( _frame )
		av_frame_free ( &_frame );

	if ( _codecContext )
	{
		avcodec_close ( _codecContext );
		avcodec_free_context ( &_codecContext );
	}

	if ( _formatContext )
		avformat_close_input ( &_formatContext );
}

HRESULT FFVideoDecoder::QueryInterface ( REFIID riid, void ** ppvObject )
{
	if ( riid == __uuidof ( IUnknown ) )
	{
		*ppvObject = this;
		return S_OK;
	}
	return E_FAIL;
}
ULONG FFVideoDecoder::AddRef ()
{
	return InterlockedIncrement ( &_refCount );
}
ULONG FFVideoDecoder::Release ()
{
	ULONG ret = InterlockedDecrement ( &_refCount );
	if ( ret <= 0 )
		delete this;
	return ret;
}

HRESULT FFVideoDecoder::Initialize ( LPCWSTR filename )
{
	USES_CONVERSION;

	_formatContext = avformat_alloc_context ();
	if ( 0 != avformat_open_input ( &_formatContext, W2A ( filename ), NULL, NULL ) )
	{
		avformat_free_context ( _formatContext );
		return E_FAIL;
	}

	if ( avformat_find_stream_info ( _formatContext, nullptr ) < 0 )
	{
		avformat_close_input ( &_formatContext );
		return E_FAIL;
	}

	for ( int i = 0; i < _formatContext->nb_streams; ++i )
	{
		auto stream = _formatContext->streams [ _streamIndex ];

		_codec = avcodec_find_decoder ( stream->codecpar->codec_id );

		_codecContext = avcodec_alloc_context3 ( _codec );
		if ( _codecContext == nullptr )
		{
			avformat_close_input ( &_formatContext );
			return E_FAIL;
		}

		avcodec_parameters_to_context ( _codecContext, stream->codecpar );
		
		if ( avcodec_open2 ( _codecContext, _codec, nullptr ) < 0 )
		{
			avformat_close_input ( &_formatContext );
			return E_FAIL;
		}

		if ( _codecContext->codec_type != AVMEDIA_TYPE_VIDEO )
		{
			avcodec_close ( _codecContext );
			avcodec_free_context ( &_codecContext );
			_codec = nullptr;
		}
		else
		{
			_streamIndex = i;
			float timeBase = stream->time_base.num / ( double ) stream->time_base.den;
			_duration = ( uint64_t ) ( stream->duration * timeBase * 1000 * 10000 );
			break;
		}
	}

	if ( _streamIndex < 0 )
	{
		avformat_close_input ( &_formatContext );
		return E_FAIL;
	}

	_frame = av_frame_alloc ();
	if ( _frame == nullptr )
	{
		avcodec_free_context ( &_codecContext );
		avformat_close_input ( &_formatContext );
		return E_FAIL;
	}

	_packet = av_packet_alloc ();
	if ( _packet == nullptr )
	{
		avcodec_free_context ( &_codecContext );
		avformat_close_input ( &_formatContext );
		return E_FAIL;
	}
	av_init_packet ( _packet );
	_packet->size = _codecContext->width * 4 * _codecContext->height;
	_packet->data = ( uint8_t* ) av_malloc ( _packet->size );

	return S_OK;
}

HRESULT FFVideoDecoder::GetVideoSize ( uint32_t * width, uint32_t * height, uint32_t * stride )
{
	if ( _formatContext == nullptr )
		return E_FAIL;

	*width = _codecContext->width;
	*height = _codecContext->height;
	*stride = ( _codecContext->width * 24 + 7 ) / 8;

	return S_OK;
}

HRESULT FFVideoDecoder::GetDuration ( uint64_t * ret )
{
	*ret = _duration;
	return S_OK;
}

HRESULT FFVideoDecoder::SetReadPosition ( uint64_t pos )
{
	AVStream * stream = _formatContext->streams [ _streamIndex ];

	return E_NOTIMPL;
}

HRESULT FFVideoDecoder::ReadSample ( IVideoSample ** sample, uint64_t * readPosition )
{
	HRESULT ret = S_OK;

	while ( 0 == av_read_frame ( _formatContext, _packet ) )
	{
		if ( _packet->stream_index != _streamIndex )
		{
			//av_packet_unref ( _packet );
			continue;
		}

		int result = avcodec_send_packet ( _codecContext, _packet );
		if ( result == AVERROR ( EAGAIN ) ) ret = E_FAIL;
		else if ( result == AVERROR ( ENOMEM ) ) ret = E_FAIL;
		else if ( result == AVERROR ( EINVAL ) ) ret = E_FAIL;
		else if ( result == AVERROR_EOF ) break;
		else if ( result < 0 ) continue;

		if ( ret == E_FAIL ) break;

		result = avcodec_receive_frame ( _codecContext, _frame );

		if ( result == AVERROR_EOF )
		{
			avcodec_flush_buffers ( _codecContext );
			*sample = nullptr;
			*readPosition = 0;
		}
		else if ( result == 0 )
		{
			*sample = new FFVideoSample ( _codecContext, _frame );
			float timeBase = _formatContext->streams [ _streamIndex ]->time_base.num
				/ ( double ) _formatContext->streams [ _streamIndex ]->time_base.den;
			*readPosition = ( uint64_t ) ( _frame->pts * timeBase * 1000 ) * 10000;
		}
		else if ( result == AVERROR ( EAGAIN ) )
		{
			ret = E_FAIL;
		}
		else if ( result == AVERROR ( EINVAL ) ) ret = E_FAIL;
		else ret = E_FAIL;

		break;
	}
	av_packet_unref ( _packet );

	return ret;
}
