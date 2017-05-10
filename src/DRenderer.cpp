#include "DRenderer.h"
#include "DRendererPlatform.h"

#define DQN_IMPLEMENTATION
#include "dqn.h"

#include <math.h>
FILE_SCOPE void DR_DrawLine(PlatformRenderBuffer *const renderBuffer, DqnV2i a,
                            DqnV2i b, DqnV3 color)
{
	if (!renderBuffer) return;

	bool yTallerThanX = false;
	if (DQN_ABS(a.x - b.x) < DQN_ABS(a.y - b.y))
	{
		// NOTE(doyle): Enforce that the X component is always longer than the
		// Y component. When drawing this we just reverse the order back.
		// This is to ensure that the gradient is always < 1, such that we can
		// use the gradient to calculate the distance from the pixel origin, and
		// at which point we want to increment the y.
		yTallerThanX = true;
		DQN_SWAP(i32, a.x, a.y);
		DQN_SWAP(i32, b.x, b.y);
	}

	if (b.x < a.x) DQN_SWAP(DqnV2i, a, b);

	i32 rise = b.y - a.y;
	i32 run  = b.x - a.x;

	i32 delta         = (b.y > a.y) ? 1 : -1;
	i32 numIterations = b.x - a.x;

	i32 distFromPixelOrigin = DQN_ABS(rise) * 2;
	i32 distAccumulator     = 0;

	i32 newX = a.x;
	i32 newY = a.y;

	// Unflip the points if we did for plotting the pixels
	i32 *plotX, *plotY;
	if (yTallerThanX)
	{
		plotX = &newY;
		plotY = &newX;
	}
	else
	{
		plotX = &newX;
		plotY = &newY;
	}

	const u32 pitchInU32 =
	    (renderBuffer->width * renderBuffer->bytesPerPixel) / 4;
	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	u32 pixel = ((i32)color.r << 16) | ((i32)color.g << 8) | ((i32)color.b << 0);
	for (i32 iterateX = 0; iterateX < numIterations; iterateX++)
	{
		newX = a.x + iterateX;
		bitmapPtr[*plotX + (*plotY * pitchInU32)] = pixel;

		distAccumulator += distFromPixelOrigin;
		if (distAccumulator > run)
		{
			newY += delta;
			distAccumulator -= (run * 2);
		}
	}
}

FILE_SCOPE void DR_DrawTriangle(PlatformRenderBuffer *const renderBuffer,
                                DqnV2 p1, DqnV2 p2, DqnV2 p3, DqnV3 color)
{
	DR_DrawLine(renderBuffer, p1, p2, color);
	DR_DrawLine(renderBuffer, p2, p3, color);
	DR_DrawLine(renderBuffer, p3, p1, color);

	// NOTE(doyle): This is just an desc sort using bubble sort on 3 elements
	if (p1.y < p2.y) DQN_SWAP(DqnV2i, p1, p2);
	if (p2.y < p3.y) DQN_SWAP(DqnV2i, p1, p3);
	if (p1.y < p2.y) DQN_SWAP(DqnV2i, p2, p3);

	i32 y1i = (i32)(p1.y + 0.5f);
	i32 y2i = (i32)(p2.y + 0.5f);
	i32 y3i = (i32)(p3.y + 0.5f);

	if (y1i == y3i) return; // Zero height triangle
}

FILE_SCOPE void DR_ClearRenderBuffer(PlatformRenderBuffer *const renderBuffer, DqnV3 color)
{
	if (!renderBuffer) return;

	DQN_ASSERT(color.r >= 0.0f && color.r <= 255.0f);
	DQN_ASSERT(color.g >= 0.0f && color.g <= 255.0f);
	DQN_ASSERT(color.b >= 0.0f && color.b <= 255.0f);

	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	for (i32 y = 0; y < renderBuffer->height; y++)
	{
		for (i32 x = 0; x < renderBuffer->width; x++)
		{
			u32 pixel = ((i32)color.r << 16) | ((i32)color.g << 8) |
			            ((i32)color.b << 0);
			bitmapPtr[x + (y * renderBuffer->width)] = pixel;
		}
	}
}

extern "C" void DR_Update(PlatformRenderBuffer *const renderBuffer,
                          PlatformInput *const input,
                          PlatformMemory *const memory)
{
	DR_ClearRenderBuffer(renderBuffer, DqnV3_3f(0, 0, 0));

	DqnV3 colorRed    = DqnV3_3i(255, 0, 0);
	DqnV2i bufferMidP = DqnV2i_2f(renderBuffer->width * 0.5f, renderBuffer->height * 0.5f);
	i32 boundsOffset  = 50;

	DqnV2 t0[3] = {DqnV2_2i(10, 70), DqnV2_2i(50, 160), DqnV2_2i(70, 80)};
	DqnV2 t1[3] = {DqnV2_2i(180, 50),  DqnV2_2i(150, 1),   DqnV2_2i(70, 180)};
	DqnV2 t2[3] = {DqnV2_2i(180, 150), DqnV2_2i(120, 160), DqnV2_2i(130, 180)};
	DqnV2 t3[3] = {
	    DqnV2_2i(boundsOffset, boundsOffset),
	    DqnV2_2i(bufferMidP.w, renderBuffer->height - boundsOffset),
	    DqnV2_2i(renderBuffer->width - boundsOffset, boundsOffset)};

	DR_DrawTriangle(renderBuffer, t0[0], t0[1], t0[2], colorRed);
	// DR_DrawTriangle(renderBuffer, t1[0], t1[1], t1[2], colorRed);
	// DR_DrawTriangle(renderBuffer, t2[0], t2[1], t2[2], colorRed);
	// DR_DrawTriangle(renderBuffer, t3[0], t3[1], t3[2], colorRed);
}
