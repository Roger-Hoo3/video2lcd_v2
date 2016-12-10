#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "video_ss.h"

extern struct DispOpr *display_get_module(const char *name);
extern struct VideoOpr *video_get_module(const char *name);
extern struct VideoConvert *GetVideoConvertForFormats(int iPixelFormatIn, int iPixelFormatOut);
extern int video_convert2rgb(struct VideoConvert *pModule, struct VideoBuf *ptVideoBufIn, struct VideoBuf *ptVideoBufOut);

#define DEFAULT_DISPLAY_MODULE "fb"
#define DEFAULT_VIDEO_MODULE "v4l2"

int main(int argc, char *argv[])
{
	int i, j;
	int lcd_row, lcd_col;
	int cam_row, cam_col;

	/*
	 * 分配两块内存区域用于临时存放视频数据
	 * 因为一个像素点用16bpp表示
	 * 所以数据类型用short
	 * 摄像头采集到的数据是320x240的,放入cam_mem
	 * LCD显示器的尺寸是240x320
	 * 把cam_mem里的数据放入lcd_mem
	 * 最后把lcd_mem放入到framebuffer
	 */

	/*
	 *
	 * cam_mem---->	-----------320------------>x
	 * 					|         |
	 * 					|         |
	 * 					240       |
	 * 					|---------p(x, y)
	 * 					|
	 * 					V
	 * 					y
	 */

	/*
	 *
	 * lcd_mem---->	------240----->x
	 * 					|    |
	 * 					|    |
	 * 					|----p(y, 320 - x)
	 * 					|
	 * 					320
	 * 					|
	 * 					|
	 * 					|
	 * 					|
	 * 					V
	 * 					y
	 */
	unsigned short **cam_mem;
	unsigned short **lcd_mem;

	/* 用于操作每一个像素点 */
	unsigned short *s = NULL;
	unsigned short *d = NULL;

	int iError;
	int iLcdWidth;
	int iLcdHeight;
	int iLcdBpp;
	int iPixelFormatOfDisp;
	int iPixelFormatOfVideo;

	struct VideoDevice tVideoDevice;
	struct VideoConvert *ptVideoConvert;
	struct DispOpr *pDispOpr;
	struct VideoOpr *pVideoOpr;
	struct VideoBuf	*ptVideoBufCur;

	struct VideoBuf tVideoBuf;//摄像头采集到的数据
	struct VideoBuf tConvertBuf;//转换后的数据
	struct VideoBuf tZoomBuf;//缩放后的数据
	struct VideoBuf tFrameBuf;//最终刷入framebuf的数据

	/* 显示子系统初始化 */
	display_init();

	/* 所有显示模块初始化 */
	display_modules_init();

	/* 选取一个默认的显示模块 */
	pDispOpr = display_get_module(DEFAULT_DISPLAY_MODULE);
	GetDispResolution(pDispOpr, &iLcdWidth, &iLcdHeight, &iLcdBpp);
	printf("LCD display format [%d x %d]\n", iLcdWidth, iLcdHeight);
	lcd_row = iLcdWidth;
	lcd_col = iLcdHeight;

	/* 动态分配二维数组 */
	lcd_mem = (unsigned short **)malloc(sizeof(unsigned short *) * lcd_row);
	if (NULL == lcd_mem)
	{
		printf("no mem ERROR\n");
		return -1;
	}
	for (i = 0; i < lcd_row; i++)
		lcd_mem[i] = (unsigned short *)malloc(sizeof(unsigned short) * lcd_col);

	/* 设置framebuffer */
	GetVideoBufForDisplay(pDispOpr, &tFrameBuf);
	iPixelFormatOfDisp = tFrameBuf.iPixelFormat;

	/* 视频子系统初始化 */
	video_init();

	/* 选取一个视频模块并初始化 */
	pVideoOpr = video_get_module(DEFAULT_VIDEO_MODULE);
	video_modules_init(pVideoOpr, &tVideoDevice);
	printf("CAMERA data format [%d x %d]\n", tVideoDevice.iWidth, tVideoDevice.iHeight);

	/* 动态分配二维数组 */
	cam_row = tVideoDevice.iWidth;
	cam_col = tVideoDevice.iHeight;
	cam_mem = (unsigned short **)malloc(sizeof(unsigned short *) * cam_row);
	if (NULL == cam_mem)
	{
		printf("no mem ERROR\n");
		return -1;
	}
	for (i = 0; i < cam_row; i++)
		cam_mem[i] = (unsigned short *)malloc(sizeof(unsigned short) * cam_col);

	iPixelFormatOfVideo = tVideoDevice.iPixelFormat;

	ShowVideoOpr();

	/* 视频转换子系统初始化 */
	VideoConvertInit();
	ShowVideoConvert();

	/* 根据采集到的视频数据格式选取一个合适的转换函数 */
	ptVideoConvert = GetVideoConvertForFormats(tVideoDevice.iPixelFormat, iPixelFormatOfDisp);
	if (NULL == ptVideoConvert)
	{
		printf("can not support this format convert\n");
		return -1;
	}

	ShowVideoConvertInfo(ptVideoConvert);

	/* 启动摄像头 */
	iError = tVideoDevice.ptVideoOpr->StartDevice(&tVideoDevice);
	if (iError)
	{
		printf("StartDevice %s error!!\n", argv[1]);
		return -1;
	}

	/* 分配视频数据缓冲区 */
	/* 1. 给直接通过摄像头采集到的数据使用 */
	memset(&tVideoBuf, 0, sizeof(T_VideoBuf));

	/* 2. 转化后的数据区 */
	memset(&tConvertBuf, 0, sizeof(T_VideoBuf));
	tConvertBuf.iPixelFormat = iPixelFormatOfDisp;
	tConvertBuf.tPixelDatas.iBpp = iLcdBpp;

	/* 3. 缩放后的数据区 */
	memset(&tZoomBuf, 0, sizeof(T_VideoBuf));

	/* 从摄像头读出数据后处理 */
	while (1)
	{
		/* 1. 读摄像头数据 */
		iError = tVideoDevice.ptVideoOpr->GetFrame(&tVideoDevice, &tVideoBuf);
		if (iError)
		{
			printf("####get frame ERROR####\n");
			return -1;
		}

		/* 保存当前视频数据的地址 */
		ptVideoBufCur = &tVideoBuf;

		/* 2. 判断是否需要转换为RGB */
		if (iPixelFormatOfVideo != iPixelFormatOfDisp)
		{
			iError = video_convert2rgb(ptVideoConvert, &tVideoBuf, &tConvertBuf);
			if (iError)
			{
				printf("Convert error\n");
				return -1;
			}

			/* 设置当前数据指针 */
			ptVideoBufCur = &tConvertBuf;
		}

		/* 操作源数据 */
		s = (unsigned short *)ptVideoBufCur->tPixelDatas.aucPixelDatas;

		/* 操作framebuffer */
		d = (unsigned short *)tFrameBuf.tPixelDatas.aucPixelDatas;

		/* 把摄像头采集的数据放入cam_mem */
		for (i = 0; i < cam_col; i++)
			for (j = 0; j < cam_row; j++)
				cam_mem[j][i] = *s++;

		/* 把cam_mem里的数据转存到lcd_mem */
		for (i = 0; i < cam_col; i++)
			for (j = 0; j < cam_row; j++)
				lcd_mem[i][cam_row - j] = cam_mem[j][i];

		for (i = 0; i < lcd_col; i++)
			for (j = 0; j < lcd_row; j++)
				*d++ = lcd_mem[j][i];

		/* 释放该帧数据,重新放入采集视频的队列 */
		iError = tVideoDevice.ptVideoOpr->PutFrame(&tVideoDevice, &tVideoBuf);
		if (iError)
		{
			printf("Put frame error\n");
			return -1;
		}
	}

	/* 释放内存 */
    for (i = 0; i < lcd_row; i++)
        free(lcd_mem[i]);
    free(lcd_mem);

    for (i = 0; i < cam_row; i++)
        free(cam_mem[i]);
    free(cam_mem);

	return 0;
}
