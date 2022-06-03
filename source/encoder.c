#include <stdio.h>
#include <stdlib.h>

#include "encoder.h"

#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

//����һ������
encoder_t *encoder_create(int in_width, int in_height, int out_width, int out_height, int bitrate) {
	encoder_t *self = (encoder_t *)malloc(sizeof(encoder_t));
	memset(self, 0, sizeof(encoder_t));

	self->in_width = in_width;
	self->in_height = in_height;
	self->out_width = out_width;
	self->out_height = out_height;
	
	avcodec_register_all(); //ע�����б������,��������ֻ��һ��
	//����ID AV_CODEC_ID_MPEG1VIDEO��Ӧ�ı�����������һ��AVCodec�ṹ��
	self->codec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
	//self->codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	
	//��self->context��ֵһ����ʼ���˵�AVCodecContext�ṹ��
	self->context = avcodec_alloc_context3(self->codec);
	//���ñ����������ĵĲ���
	self->context->dct_algo = FF_DCT_FASTINT; //����ʹ�õ���ɢ���ұ任�㷨������
	self->context->bit_rate = bitrate;
	self->context->width = out_width;
	self->context->height = out_height;
	self->context->time_base.num = 1; //��ʾʱ����Ļ���ʱ�䵥λ,num �Ƿ���,den �Ƿ�ĸ
	self->context->time_base.den = 30;
	self->context->gop_size = 30; //һ��ͼƬ�е�ͼƬ����
	self->context->max_b_frames = 0; //��B֮֡������B֡��
	self->context->pix_fmt = PIX_FMT_YUV420P; //ͼƬ�����ظ�ʽ��YUV420P
	//�ø�����AVCodec(�ڶ�������)��ʼ��AVCodecContext(��һ������)
	avcodec_open2(self->context, self->codec, NULL);
	//��AVFrame�ṹ�����ռ�
	self->frame = avcodec_alloc_frame();
	//����֡���ݵĲ���
	self->frame->format = PIX_FMT_YUV420P; //���ظ�ʽ
	self->frame->width  = out_width;
	self->frame->height = out_height;
	self->frame->pts = 0; //��ʾʱ���
	
	//���������Ⱥ͸߶ȵ�YUV420���ظ�ʽͼƬռ�õĴ�С(Byte)
	int frame_size = avpicture_get_size(PIX_FMT_YUV420P, out_width, out_height);
	self->frame_buffer = malloc(frame_size); //����֡�ռ�
	//����ָ����ͼ�������frame_buffer����ͼƬ�ֶ�
	avpicture_fill((AVPicture*)self->frame, (uint8_t*)self->frame_buffer, PIX_FMT_YUV420P, out_width, out_height);
	
	//sws_getContext����һ��SwsContext�ṹ�壬���ڶ�ͼƬ�����ź����ظ�ʽת��
	self->sws = sws_getContext(
		in_width, in_height, AV_PIX_FMT_RGB32,
		out_width, out_height, AV_PIX_FMT_YUV420P,
		SWS_FAST_BILINEAR, 0, 0, 0
	);
	
	return self;
}

//�ͷű�������ṹ��
void encoder_destroy(encoder_t *self) {
	if( self == NULL ) { return; }

	sws_freeContext(self->sws);
	avcodec_close(self->context);
	av_free(self->context);	
	av_free(self->frame);
	free(self->frame_buffer);
	free(self);
}

//app.c������˸ú���
//Ϊʲôencoded_size = APP_FRAME_BUFFER_SIZE - sizeof(jsmpeg_frame_t)��ûŪ���ף����λָ��
void encoder_encode(encoder_t *self, void *rgb_pixels, void *encoded_data, size_t *encoded_size) {
	uint8_t *in_data[1] = {(uint8_t *)rgb_pixels}; //Դͼ��(RGB���ظ�ʽ)
	int in_linesize[1] = {self->in_width * 4};
	//��Դͼ���е�slice(in_data)�������ź�ת�����ظ�ʽ����, ���õ��Ľ���ŵ�dst(self->frame->data)��
	/*
	��Ƶ���е�һ��packet��һ������һ֡ͼ��, 
	���п�����һ֡ͼ���е�һ����(һ֡ͼ�񳬹���һ��packet�����ɴ�С, ���з�Ϊ�����������),
	slice ����һ��ͼ���һ����ͼ������, ��ȻҲ�п�����ȫ��.����Ŀ�����ȫ��ͼ��.
	*/
	sws_scale(self->sws, in_data, in_linesize, 0, self->in_height, self->frame->data, self->frame->linesize);
		
	int available_size = *encoded_size; //�����ͼƬ�Ĵ�С
	*encoded_size = 0;
	self->frame->pts++; //��time_baseΪ��λ����ʾʱ�����Ӧ���û���ʾ֡��ʱ�䣩++
	
	av_init_packet(&self->packet); //��ʼ��AVPacket
	int success = 0;
	/*��һ֡ԭʼ��Ƶ����(self->frame)���룬Ȼ��ŵ�&self->packet��.
		�û�����ͨ���ڵ��øú���֮ǰ����packet->data��packet->size ���ṩ�����������
		���packet�Ļ�������С�����󣬱����ʧ�ܡ�
		����û���ֶ����ã����Զ�����ġ�
		��������packet�ǿգ���success����Ϊ1����������Ϊ0��
	*/
	avcodec_encode_video2(self->context, &self->packet, self->frame, &success); 
	//��������packet�ǿ�
	if( success ) {	
		if( self->packet.size <= available_size ) {
			//void *memcpy(void *str1, const void *str2, size_t n) �Ӵ洢��str2����n���ֽڵ��洢��str1
			//��packet��ı�������ݷŵ�encoded_data��
			memcpy(encoded_data, self->packet.data, self->packet.size); 
			*encoded_size = self->packet.size; //�ѱ����ʵ�ʵĴ�Сд��*encoded_size
		}
		else {
			printf("Frame too large for buffer (size: %d needed: %d)\n", available_size, self->packet.size);
		}
	}
	av_free_packet(&self->packet); //������ɣ��ͷ�packet
}
