#ifndef ENCODER_H
#define ENCODER_H

extern "C" {
	#include "ffmpeg/include/libavutil/avutil.h"
	#include "ffmpeg/include/libavcodec/avcodec.h"
	#include "ffmpeg/include/libswscale/swscale.h"	
}

typedef struct {
	AVCodec *codec; //存储编解码器信息的结构体
	AVCodecContext *context; //包含很多变量，重要的几个变量和AVCodec里的差不多
	AVFrame *frame; //包含码流参数较多的结构体
	void *frame_buffer;

	int in_width, in_height;
	int out_width, out_height;
	
	AVPacket packet; //存储压缩编码数据相关信息,可以存放多个帧
	//位于libswscale类库, 该类库主要用于处理图片像素数据, 可以完成图片像素格式的转换, 图片的拉伸等工作
	SwsContext *sws; 
} encoder_t;


encoder_t *encoder_create(int in_width, int in_height, int out_width, int out_height, int bitrate);
void encoder_destroy(encoder_t *self);
void encoder_encode(encoder_t *self, void *rgb_pixels, void *encoded_data, size_t *encoded_size);

#endif
