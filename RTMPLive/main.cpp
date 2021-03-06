#define _CRTDBG_MAP_ALLOC
#include <stdio.h>
#ifdef _WIN32
#include "windows.h"
#else
#include <string.h>
#include <unistd.h>
#endif
#include "getopt.h"
#include <stdio.h> 
#include <iostream> 
#include <time.h> 
#include <stdlib.h>
//#include <vector>
#include <list>

#include "EasyRTSPClientAPI.h"
#include "EasyAACEncoderAPI.h"
#include "EasyRTMPAPI.h"
#include "ini.h"
#include "trace.h"

#ifdef _WIN32
#pragma comment(lib,"libEasyRTSPClient.lib")
#pragma comment(lib,"libEasyAACEncoder.lib")
#pragma comment(lib,"libeasyrtmp.lib")
#endif

#define MAX_RTMP_URL_LEN 256

#ifdef _WIN32
#define KEY "79397037795969576B5A754174366C6170565A48792F4E535645315154476C325A53356C6547572B567778576F50394C34456468646D6C754A6B4A68596D397A595541794D4445325257467A65555268636E6470626C526C5957316C59584E35"
#define RTSP_KEY "79393674363469576B5A75415170646170576938792F4E535645315154476C325A53356C65475570567778576F50394C34456468646D6C754A6B4A68596D397A595541794D4445325257467A65555268636E6470626C526C5957316C59584E35"
#else // linux
#define KEY "79397037795A4F576B5971417A71466170563477792F64796447317762476C325A654658444661672F307667523246326157346D516D466962334E68514449774D545A4659584E355247467964326C75564756686257566863336B3D"
#define RTSP_KEY "7939367436354F576B597141464B646170566A71792F64796447317762476C325A577858444661672F307667523246326157346D516D466962334E68514449774D545A4659584E355247467964326C75564756686257566863336B3D"
#endif

#define BUFFER_SIZE  1024*1024
#define MAX_CHANNEL_INDEX 1024
#define CONF_FILE_PATH  "Config.ini"  

typedef struct _channel_cfg_struct_t
{
	int channelId;
	int option;
	char channelName[64];
	char srcRtspAddr[256];
	char destRtmpAddr[256];
}_channel_cfg;

typedef struct _rtmp_pusher_struct_t
{
	Easy_RTMP_Handle rtmpHandle;
	unsigned int u32AudioCodec;	
	unsigned int u32AudioSamplerate;
	unsigned int u32AudioChannel;
	EasyAACEncoder_Handle m_pAACEncoderHandle;
	unsigned char m_pAACEncBufer[64*1024];
}_rtmp_pusher;

typedef struct _channel_info_struct_t
{
	_channel_cfg		fCfgInfo;
	_rtmp_pusher		fPusherInfo;
	Easy_RTSP_Handle	fNVSHandle;
	FILE*				fLogHandle;
	bool				fHavePrintKeyInfo;
	EASY_MEDIA_INFO_T	fMediainfo;
}_channel_info;

static std::list <_channel_info*> gChannelInfoList;

static int testaddr = 5;

int __EasyRTMP_Callback(int _frameType, char *pBuf, EASY_RTMP_STATE_T _state, void *_userPtr)
{
	_channel_info* pChannel = (_channel_info*)_userPtr;

	switch(_state)
	{
	case EASY_RTMP_STATE_CONNECTING:
		TRACE_LOG(pChannel->fLogHandle, "Connecting...\n");
		break;
	case EASY_RTMP_STATE_CONNECTED:
		TRACE_LOG(pChannel->fLogHandle, "Connected\n");
		break;
	case EASY_RTMP_STATE_CONNECT_FAILED:
		TRACE_LOG(pChannel->fLogHandle, "Connect failed\n");
		break;
	case EASY_RTMP_STATE_CONNECT_ABORT:
		TRACE_LOG(pChannel->fLogHandle, "Connect abort\n");
		break;
	case EASY_RTMP_STATE_DISCONNECTED:
		TRACE_LOG(pChannel->fLogHandle, "Disconnect.\n");
		break;
	default:
		break;
	}

	return 0;
}

int EasyInitAACEncoder(_channel_info* pChannel, RTSP_FRAME_INFO *frameinfo)
{
	if(pChannel->fPusherInfo.m_pAACEncoderHandle == NULL)
	{
		InitParam initParam;
		initParam.u32AudioSamplerate = frameinfo->sample_rate;
		initParam.ucAudioChannel =  frameinfo->channels;
		initParam.u32PCMBitSize =  frameinfo->bits_per_sample;

		if(frameinfo->codec == EASY_SDK_AUDIO_CODEC_G711A)
			initParam.ucAudioCodec = Law_ALaw;
		else if(frameinfo->codec == EASY_SDK_AUDIO_CODEC_G711U)
			initParam.ucAudioCodec = Law_ULaw;
		else if(frameinfo->codec == EASY_SDK_AUDIO_CODEC_G726)
			initParam.ucAudioCodec = Law_G726;
		else
			return -1;

		pChannel->fPusherInfo.m_pAACEncoderHandle = Easy_AACEncoder_Init( initParam);
	}
	return 0;
}

/* EasyRTSPClient callback */
int Easy_APICALL __RTSPSourceCallBack( int _chid, void *_chPtr, int _mediatype, char *pbuf, RTSP_FRAME_INFO *frameinfo)
{
	if (NULL != frameinfo)
	{
		if (frameinfo->height==1088)		frameinfo->height=1080;
		else if (frameinfo->height==544)	frameinfo->height=540;
	}
	Easy_Bool bRet = 0;
	int iRet = 0;
	
	_channel_info* pChannel = (_channel_info*)_chPtr;

	if (_mediatype == EASY_SDK_VIDEO_FRAME_FLAG)
	{
		if(frameinfo && frameinfo->length)
		{
			if( frameinfo->type == EASY_SDK_VIDEO_FRAME_I)
			{
				if(pChannel->fPusherInfo.rtmpHandle == 0)
				{
					pChannel->fPusherInfo.rtmpHandle = EasyRTMP_Create();
					if (pChannel->fPusherInfo.rtmpHandle == NULL)
					{
						TRACE_LOG(pChannel->fLogHandle, "Fail to rtmp create failed ...\n");
						return -1;
					}
					EasyRTMP_SetCallback(pChannel->fPusherInfo.rtmpHandle, __EasyRTMP_Callback, pChannel);
					bRet = EasyRTMP_Connect(pChannel->fPusherInfo.rtmpHandle, pChannel->fCfgInfo.destRtmpAddr);
					if (!bRet)
					{
						TRACE_LOG(pChannel->fLogHandle, "Fail to rtmp connect failed ...\n");
					}

					EASY_MEDIA_INFO_T mediaInfo;
					memset(&mediaInfo, 0, sizeof(EASY_MEDIA_INFO_T));
					mediaInfo.u32VideoFps = pChannel->fMediainfo.u32VideoFps;
					mediaInfo.u32AudioSamplerate = 8000;
					iRet = EasyRTMP_InitMetadata(pChannel->fPusherInfo.rtmpHandle, &mediaInfo, 1024);
					if (iRet < 0)
					{
						TRACE_LOG(pChannel->fLogHandle, "Fail to Init Metadata ...\n");
					}
				}

				EASY_AV_Frame avFrame;
				memset(&avFrame, 0, sizeof(EASY_AV_Frame));
				avFrame.u32AVFrameFlag = EASY_SDK_VIDEO_FRAME_FLAG;
				avFrame.u32AVFrameLen = frameinfo->length;
				avFrame.pBuffer = (unsigned char*)pbuf;
				avFrame.u32VFrameType = EASY_SDK_VIDEO_FRAME_I;
				//avFrame.u32TimestampSec = frameinfo->timestamp_sec;
				//avFrame.u32TimestampUsec = frameinfo->timestamp_usec;
				//
				iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
				if (iRet < 0)
				{
					TRACE_LOG(pChannel->fLogHandle, "Fail to Send H264 Packet(I-frame) ...\n");
				}
				else
				{
					if(!pChannel->fHavePrintKeyInfo)
					{
						TRACE_LOG(pChannel->fLogHandle, "I\n");
						pChannel->fHavePrintKeyInfo = true;
					}
				}
			}
			else
			{
				if(pChannel->fPusherInfo.rtmpHandle)
				{
					EASY_AV_Frame avFrame;
					memset(&avFrame, 0, sizeof(EASY_AV_Frame));
					avFrame.u32AVFrameFlag = EASY_SDK_VIDEO_FRAME_FLAG;
					avFrame.u32AVFrameLen = frameinfo->length-4;
					avFrame.pBuffer = (unsigned char*)pbuf+4;
					avFrame.u32VFrameType = EASY_SDK_VIDEO_FRAME_P;
					//avFrame.u32TimestampSec = frameinfo->timestamp_sec;
					//avFrame.u32TimestampUsec = frameinfo->timestamp_usec;
					iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
					if (iRet < 0)
					{
						TRACE_LOG(pChannel->fLogHandle, "Fail to Send H264 Packet(P-frame) ...\n");
					}
					else
					{
						if(!pChannel->fHavePrintKeyInfo)
						{
							TRACE_LOG(pChannel->fLogHandle, "P\n");
						}
					}
				}
			}				
		}	
	}
	else if (_mediatype == EASY_SDK_MEDIA_INFO_FLAG)//回调出媒体信息
	{
		if(pbuf != NULL)
		{
			EASY_MEDIA_INFO_T mediainfo;
			memset(&(pChannel->fMediainfo), 0x00, sizeof(EASY_MEDIA_INFO_T));
			memcpy(&(pChannel->fMediainfo), pbuf, sizeof(EASY_MEDIA_INFO_T));
			TRACE_LOG(pChannel->fLogHandle,"RTSP DESCRIBE Get Media Info: video:%u fps:%u audio:%u channel:%u sampleRate:%u \n", 
				pChannel->fMediainfo.u32VideoCodec, pChannel->fMediainfo.u32VideoFps, pChannel->fMediainfo.u32AudioCodec, pChannel->fMediainfo.u32AudioChannel, pChannel->fMediainfo.u32AudioSamplerate);
		}
	}
	else if (_mediatype == EASY_SDK_AUDIO_FRAME_FLAG)
	{
		EASY_AV_Frame	avFrame;
		memset(&avFrame, 0x00, sizeof(EASY_AV_Frame));
		avFrame.u32AVFrameFlag = EASY_SDK_AUDIO_FRAME_FLAG;
		//avFrame.u32TimestampSec = frameinfo->timestamp_sec;
		//avFrame.u32TimestampUsec = frameinfo->timestamp_usec;

		if(frameinfo->codec == EASY_SDK_AUDIO_CODEC_AAC)
		{
			avFrame.pBuffer = (Easy_U8*)(pbuf);
			avFrame.u32AVFrameLen  = frameinfo->length;	
			//printf("*");
			iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
		}
		else if ((frameinfo->codec == EASY_SDK_AUDIO_CODEC_G711A) || (frameinfo->codec == EASY_SDK_AUDIO_CODEC_G711U) || (frameinfo->codec == EASY_SDK_AUDIO_CODEC_G726))
		{
			if(EasyInitAACEncoder(pChannel, frameinfo) == 0)
			{
				memset(pChannel->fPusherInfo.m_pAACEncBufer, 0, 64*1024);
				unsigned int iAACBufferLen = 0;

				if(Easy_AACEncoder_Encode(pChannel->fPusherInfo.m_pAACEncoderHandle, (unsigned char*)pbuf,  frameinfo->length, pChannel->fPusherInfo.m_pAACEncBufer, &iAACBufferLen) > 0)
				{
					//printf("*");
					avFrame.pBuffer = (Easy_U8*)(pChannel->fPusherInfo.m_pAACEncBufer);
					avFrame.u32AVFrameLen  = iAACBufferLen;	
					iRet = EasyRTMP_SendPacket(pChannel->fPusherInfo.rtmpHandle, &avFrame);
				}
			}
		}
	}

	return 0;
}

bool InitCfgInfo(void)
{
	int i = 0;
	gChannelInfoList.clear();
	for(i = 0; i < MAX_CHANNEL_INDEX; i++)
	{
		_channel_info* pChannelInfo = new _channel_info();
		if(pChannelInfo)
		{
			memset(pChannelInfo, 0, sizeof(_channel_info));
			pChannelInfo->fCfgInfo.channelId = i;
			pChannelInfo->fHavePrintKeyInfo = false;
			sprintf(pChannelInfo->fCfgInfo.channelName, "channel%d",i);
			strcpy(pChannelInfo->fCfgInfo.srcRtspAddr, GetIniKeyString(pChannelInfo->fCfgInfo.channelName, "rtsp", CONF_FILE_PATH));
			strcpy(pChannelInfo->fCfgInfo.destRtmpAddr, GetIniKeyString(pChannelInfo->fCfgInfo.channelName, "rtmp", CONF_FILE_PATH));
			pChannelInfo->fCfgInfo.option = GetIniKeyInt(pChannelInfo->fCfgInfo.channelName, "option", CONF_FILE_PATH);
			if(strlen(pChannelInfo->fCfgInfo.srcRtspAddr) > 0 && strlen(pChannelInfo->fCfgInfo.destRtmpAddr) > 0)
			{
				gChannelInfoList.push_back(pChannelInfo);
			}
		}
	}
	return true;
}

void ReleaseSpace(void)
{
	std::list<_channel_info*>::iterator it;
	for(it = gChannelInfoList.begin(); it != gChannelInfoList.end(); it++)
	{
		_channel_info* pChannel = *it;

		if (NULL != pChannel->fNVSHandle) 
		{
			EasyRTSP_CloseStream(pChannel->fNVSHandle);
			EasyRTSP_Deinit(&(pChannel->fNVSHandle));
			pChannel->fNVSHandle = NULL;
		}

	/*	if (NULL != pChannel->fPusherInfo.m_pAACEncoderHandle)
		{
			Easy_AACEncoder_Release(pChannel->fPusherInfo.m_pAACEncoderHandle);
			pChannel->fPusherInfo.m_pAACEncoderHandle = NULL;
		}*/

		if (NULL != pChannel->fPusherInfo.rtmpHandle)
		{
			EasyRTMP_Release(pChannel->fPusherInfo.rtmpHandle);
			pChannel->fPusherInfo.rtmpHandle = NULL;
		}

		if(pChannel->fLogHandle)
		{
			TRACE_CloseLogFile(pChannel->fLogHandle);
			pChannel->fLogHandle = NULL;
		}

		delete pChannel;
	}

	gChannelInfoList.clear();
}

int main(int argc, char * argv[])
{
	InitCfgInfo();

	int iret = EasyRTMP_Activate(KEY);
	if (iret != 0)
	{
		printf("RTMP Activate error. ret=%d!!!\n", iret);
		getchar();
		return -1;
	}

#ifdef _WIN32
	extern char* optarg;
#endif
	int ch;

	atexit(ReleaseSpace);

	iret = EasyRTSP_Activate(RTSP_KEY);
	if(iret != 0)
	{
		printf("rtsp Activate error. ret=%d!!!\n", iret);
		return -2;
	}

	std::list<_channel_info*>::iterator it;
	for(it = gChannelInfoList.begin(); it != gChannelInfoList.end(); it++)
	{
		_channel_info* pChannel = *it;
		pChannel->fLogHandle = TRACE_OpenLogFile(pChannel->fCfgInfo.channelName);

		TRACE_LOG(pChannel->fLogHandle, "channel[%d] rtsp addr : %s\n", pChannel->fCfgInfo.channelId, pChannel->fCfgInfo.srcRtspAddr);
		TRACE_LOG(pChannel->fLogHandle, "channel[%d] rtmp addr : %s\n", pChannel->fCfgInfo.channelId, pChannel->fCfgInfo.destRtmpAddr);

		EasyRTSP_Init(&(pChannel->fNVSHandle));

		if (NULL == pChannel->fNVSHandle)
		{
			TRACE_LOG(pChannel->fLogHandle, "%s rtsp init error. ret=%d!!!\n", pChannel->fCfgInfo.channelName , iret);
			continue;
		}
		unsigned int mediaType = EASY_SDK_VIDEO_FRAME_FLAG | EASY_SDK_AUDIO_FRAME_FLAG;
	
		EasyRTSP_SetCallback(pChannel->fNVSHandle, __RTSPSourceCallBack);

		EasyRTSP_OpenStream(pChannel->fNVSHandle, pChannel->fCfgInfo.channelId, pChannel->fCfgInfo.srcRtspAddr, EASY_RTP_OVER_TCP, mediaType, 0, 0, pChannel, 1000, 0, pChannel->fCfgInfo.option, 0);
	}

	while(true)
	{
#ifdef _WIN32
		Sleep(1000);
#else
		sleep(1);
#endif
	}

    return 0;
}