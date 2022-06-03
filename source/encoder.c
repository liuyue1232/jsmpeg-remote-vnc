#include <stdio.h>
#include <stdlib.h>

#include "encoder.h"

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

//创建一编码器
encoder_t *encoder_create(int in_width, int in_height, int out_width, int out_height, int bitrate) {
	encoder_t *self = (encoder_t *)malloc(sizeof(encoder_t));
	memset(self, 0, sizeof(encoder_t));

	self->in_width = in_width;
	self->in_height = in_height;
	self->out_width = out_width;
	self->out_height = out_height;
	
	avcodec_register_all(); //注册所有编解码器,不过这里只有一个
	//查找ID AV_CODEC_ID_MPEG1VIDEO对应的编码器，返回一个AVCodec结构体
	self->codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
	//self->codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	
	//给self->context赋值一个初始化了的AVCodecContext结构体
	self->context = avcodec_alloc_context3(self->codec);
	//设置编码器上下文的参数
	self->context->dct_algo = FF_DCT_FASTINT; //编码使用的离散余弦变换算法的设置
	self->context->bit_rate = bitrate;
	self->context->width = out_width;
	self->context->height = out_height;
	self->context->time_base.num = 1; //表示时间戳的基本时间单位,num 是分子,den 是分母
	self->context->time_base.den = 30;
	self->context->gop_size = 30; //一组图片中的图片数量
	self->context->max_b_frames = 0; //非B帧之间的最大B帧数
	self->context->pix_fmt = PIX_FMT_YUV420P; //图片的像素格式是YUV420P
	//用给定的AVCodec(第二个参数)初始化AVCodecContext(第一个参数)
	avcodec_open2(self->context, self->codec, NULL);
	//给AVFrame结构体分配空间
	self->frame = avcodec_alloc_frame();
	//设置帧数据的参数
	self->frame->format = PIX_FMT_YUV420P; //像素格式
	self->frame->width  = out_width;
	self->frame->height = out_height;
	self->frame->pts = 0; //显示时间戳
	
	//计算给定宽度和高度的YUV420像素格式图片占用的大小(Byte)
	int frame_size = avpicture_get_size(PIX_FMT_YUV420P, out_width, out_height);
	self->frame_buffer = malloc(frame_size); //分配帧空间
	//根据指定的图像参数和frame_buffer设置图片字段
	avpicture_fill((AVPicture*)self->frame, (uint8_t*)self->frame_buffer, PIX_FMT_YUV420P, out_width, out_height);
	
	//sws_getContext返回一个SwsContext结构体，用于对图片的缩放和像素格式转换
	self->sws = sws_getContext(
		in_width, in_height, AV_PIX_FMT_RGB32,
		out_width, out_height, AV_PIX_FMT_YUV420P,
		SWS_FAST_BILINEAR, 0, 0, 0
	);
	
	return self;
}

//释放编解码器结构体
void encoder_destroy(encoder_t *self) {
	if( self == NULL ) { return; }

	sws_freeContext(self->sws);
	avcodec_close(self->context);
	av_free(self->context);	
	av_free(self->frame);
	free(self->frame_buffer);
	free(self);
}

//app.c里调用了该函数
//为什么encoded_size = APP_FRAME_BUFFER_SIZE - sizeof(jsmpeg_frame_t)还没弄明白，请各位指教
void encoder_encode(encoder_t *self, void *rgb_pixels, void *encoded_data, size_t *encoded_size) {
	uint8_t *in_data[1] = {(uint8_t *)rgb_pixels}; //源图像(RGB像素格式)
	int in_linesize[1] = {self->in_width * 4};
	//对源图像中的slice(in_data)进行缩放和转换像素格式处理, 将得到的结果放到dst(self->frame->data)中
	/*
	视频流中的一个packet不一定就是一帧图像, 
	它有可能是一帧图像中的一部分(一帧图像超过了一个packet的容纳大小, 被切分为几份逐个发送),
	slice 代着一副图像的一部分图像数据, 当然也有可能是全部.本项目里代表全部图像.
	*/
	sws_scale(self->sws, in_data, in_linesize, 0, self->in_height, self->frame->data, self->frame->linesize);
		
	int available_size = *encoded_size; //编码后图片的大小
	*encoded_size = 0;
	self->frame->pts++; //以time_base为单位的显示时间戳（应向用户显示帧的时间）++
	
	av_init_packet(&self->packet); //初始化AVPacket
	int success = 0;
	/*把一帧原始视频数据(self->frame)编码，然后放到&self->packet里.
		用户可以通过在调用该函数之前设置packet->data和packet->size 来提供输出缓冲区，
		如果packet的缓冲区大小不够大，编码会失败。
		这里没有手动设置，是自动分配的。
		如果编码后packet非空，则success被设为1，否则被设置为0。
	*/
	avcodec_encode_video2(self->context, &self->packet, self->frame, &success); 
	//如果编码后packet非空
	if( success ) {	
		if( self->packet.size <= available_size ) {
			//void *memcpy(void *str1, const void *str2, size_t n) 从存储区str2复制n个字节到存储区str1
			//把packet里的编码后内容放到encoded_data里
			memcpy(encoded_data, self->packet.data, self->packet.size); 
			*encoded_size = self->packet.size; //把编码后实际的大小写入*encoded_size
		}
		else {
			printf("Frame too large for buffer (size: %d needed: %d)\n", available_size, self->packet.size);
		}
	}
	av_free_packet(&self->packet); //编码完成，释放packet
}
