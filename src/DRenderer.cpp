#include "DRenderer.h"

#include "DRendererPlatform.h"

extern "C" void DR_Update(PlatformRenderBuffer *const renderBuffer,
                          PlatformInput *const input,
                          PlatformMemory *const memory)
{
	u32 *bitmapPtr = (u32 *)renderBuffer->memory;
	for (i32 y = 0; y < renderBuffer->height; y++)
	{
		for (i32 x = 0; x < renderBuffer->width; x++)
		{
			u8 red   = 255;
			u8 green = 0;
			u8 blue  = 0;

			u32 color = (red << 16) | (green << 8) | (blue << 0);
			bitmapPtr[x + (y * renderBuffer->width)] = color;
		}
	}
}
