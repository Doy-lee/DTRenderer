#include "DRenderer.h"
#include "DRendererPlatform.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb_rect_pack.h"
#include "external/stb_image.h"
#include "external/stb_truetype.h"

// #define DR_DEBUG_RENDER_FONT_BITMAP
#ifdef DR_DEBUG_RENDER_FONT_BITMAP
	#define STB_IMAGE_WRITE_IMPLEMENTATION
	#include "external/stb_image_write.h"
#endif

#define DQN_IMPLEMENTATION
#include "dqn.h"

#define DR_DEBUG 1
typedef struct DRFont
{
	u8    *bitmap;
	DqnV2i bitmapDim;
	DqnV2i codepointRange;
	f32    sizeInPt;

	stbtt_packedchar *atlas;
} DRFont;

typedef struct DRState
{
	DRFont font;
} DRState;

FILE_SCOPE inline void SetPixel(PlatformRenderBuffer *const renderBuffer,
                                const i32 x, const i32 y, DqnV3 color)
{
	if (!renderBuffer) return;
	if (x < 0 || x > renderBuffer->width - 1) return;
	if (y < 0 || y > renderBuffer->height - 1) return;

	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	const u32 pitchInU32 = (renderBuffer->width * renderBuffer->bytesPerPixel) / 4;
	u32 pixel = ((i32)color.r << 16) | ((i32)color.g << 8) | ((i32)color.b << 0);
	bitmapPtr[x + (y * pitchInU32)] = pixel;
}

FILE_SCOPE void DrawLine(PlatformRenderBuffer *const renderBuffer, DqnV2i a,
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

	for (i32 iterateX = 0; iterateX < numIterations; iterateX++)
	{
		newX = a.x + iterateX;
		SetPixel(renderBuffer, *plotX, *plotY, color);

		distAccumulator += distFromPixelOrigin;
   		if (distAccumulator > run)
		{
			newY += delta;
			distAccumulator -= (run * 2);
		}
	}
}

FILE_SCOPE void DrawTriangle(PlatformRenderBuffer *const renderBuffer,
                             const DqnV2 p1, const DqnV2 p2, const DqnV2 p3,
                             const DqnV3 color)
{
	__rdtsc();
	DqnV2i max = DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x),
	                       DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
	DqnV2i min = DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x),
	                       DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));

	min.x = DQN_MAX(min.x, 0);
	min.y = DQN_MAX(min.y, 0);

	max.x = DQN_MIN(max.x, renderBuffer->width - 1);
	max.y = DQN_MIN(max.y, renderBuffer->height - 1);

	DrawLine(renderBuffer, DqnV2i_2i(min.x, min.y), DqnV2i_2i(min.x, max.y), color);
	DrawLine(renderBuffer, DqnV2i_2i(min.x, max.y), DqnV2i_2i(max.x, max.y), color);
	DrawLine(renderBuffer, DqnV2i_2i(max.x, max.y), DqnV2i_2i(max.x, min.y), color);
	DrawLine(renderBuffer, DqnV2i_2i(max.x, min.y), DqnV2i_2i(min.x, min.y), color);

	DqnV2 a = p1;
	DqnV2 b = p2;
	DqnV2 c = p3;

	f32 area2Times = ((b.x - a.x) * (b.y + a.y)) + ((c.x - b.x) * (c.y + b.y)) +
	                 ((a.x - c.x) * (a.y + c.y));
	if (area2Times < 0)
	{
		// Counter-clockwise, do nothing this is what we want.
	}
	else
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV2, b, c);
	}

	/*
	   /////////////////////////////////////////////////////////////////////////
	   // Rearranging the Determinant
	   /////////////////////////////////////////////////////////////////////////
	   Given two points that form a line and an extra point to test, we can
	   determine whether a point lies on the line, or is to the left or right of
	   a the line.

	   First forming a 3x3 matrix of our terms and deriving a 2x2 matrix by
	   subtracting the 1st column from the 2nd and 1st column from the third.

	       | ax bx cx |     | (bx - ax)  (cx - ax) |
	   m = | ay by cy | ==> | (by - ay)  (cy - ay) |
	       | 1  1  1  |

	   From our 2x2 representation we can calculate the determinant which gives
	   us the signed area of the triangle extended into a parallelogram.

	   det(m) = (bx - ax)(cy - ay) - (by - ay)(cx - ax)

	   Depending on the order of the vertices supplied, if it's
	   - CCW and c(x,y) is outside the triangle, the signed area is negative
	   - CCW and c(x,y) is inside  the triangle, the signed area is positive
	   - CW  and c(x,y) is outside the triangle, the signed area is positive
	   - CW  and c(x,y) is inside  the triangle, the signed area is negative

	   /////////////////////////////////////////////////////////////////////////
	   // Optimising the Determinant Calculation
	   /////////////////////////////////////////////////////////////////////////
	   The det(m) can be rearranged if expanded to be
	   SignedArea(cx, cy) = (ay - by)cx + (bx - ay)cy + (ax*by - ay*bx)

	   When we scan to fill our triangle we go pixel by pixel, left to right,
	   bottom to top, notice that this translates to +1 for x and +1 for y, i.e.

	   The first pixel's signed area is cx, then cx+1, cx+2 .. etc
	   SignedArea(cx, cy)   = (ay - by)cx   + (bx - ax)cy + (ax*by - ay*bx)
	   SignedArea(cx+1, cy) = (ay - by)cx+1 + (bx - ax)cy + (ax*by - ay*bx)

	   Then
	   SignedArea(cx+1, cy) - SignedArea(cx, cy) =
	     (ay - by)cx+1 + (bx - ax)cy + (ax*by - ay*bx)
	   - (ay - by)cx   + (bx - ax)cy + (ax*by - ay*bx)
	   = (ay - by)cx+1 - (ay - by)cx
	   = (ay - by)(cx+1 - cx)
	   = (ay - by)(1)         = (ay - by)

	   Similarly when progressing in y
	   SignedArea(cx, cy)   = (ay - by)cx + (bx - ay)cy   + (ax*by - ay*bx)
	   SignedArea(cx, cy+1) = (ay - by)cx + (bx - ay)cy+1 + (ax*by - ay*bx)

	   Then
	   SignedArea(cx, cy+1) - SignedArea(cx, cy) =
	     (ay - by)cx + (bx - ax)cy+1 + (ax*by - ay*bx)
	   - (ay - by)cx + (bx - ax)cy   + (ax*by - ay*bx)
	   = (bx - ax)cy+1 - (bx - ax)cy
	   = (bx - ax)(cy+1 - cy)
	   = (bx - ax)(1)         = (bx - ax)

	   Then we can see that when we progress along x, we only need to change by
	   the value of SignedArea by (ay - by) and similarly for y, (bx - ay)

	   /////////////////////////////////////////////////////////////////////////
	   // Barycentric Coordinates
	   /////////////////////////////////////////////////////////////////////////
	   At this point we have an equation that can be used to calculate the
	   2x the signed area of a triangle, or the signed area of a parallelogram,
	   the two of which are equivalent.

	   det(m)             = (bx - ax)(cy - ay) - (by - ay)(cx - ax)
	   SignedArea(cx, cy) = (ay - by)cx + (bx - ay)cy + (ax*by - ay*bx)

	   A barycentric coordinate is some coefficient on A, B, C that allows us to
	   specify an arbitrary point in the triangle as a linear combination of the
	   three usually with some coefficient [0, 1].

	   The SignedArea turns out to be actually the barycentric coord for c(x, y)
	   normalised to the sum of the parallelogram area. For example a triangle
	   with points, A, B, C and an arbitrary point P inside the triangle. Then

	   SignedArea(P) with vertex A and B = Barycentric Coordinate for C
	   SignedArea(P) with vertex B and C = Barycentric Coordinate for A
	   SignedArea(P) with vertex C and A = Barycentric Coordinate for B

	       B
	      / \
	     /   \
	    /  P  \
	   /_______\
	  A        C

	   This is normalised to the area's sum, but we can trivially turn this into
	   a normalised version by dividing the area of the parallelogram, i.e.

	   BaryCentricA(P) = (SignedArea(P) w. vertex C and B)/SignedArea(of the orig triangle)
	   BaryCentricB(P) = (SignedArea(P) w. vertex A and C)/SignedArea(of the orig triangle)
	   BaryCentricC(P) = (SignedArea(P) w. vertex A and B)/SignedArea(of the orig triangle)
	*/

	DqnV2i scanP          = DqnV2i_2i(min.x, min.y);
	f32 signedArea1       = ((b.x - a.x) * (scanP.y - a.y)) - ((b.y - a.y) * (scanP.x - a.x));
	f32 signedArea1DeltaX = a.y - b.y;
	f32 signedArea1DeltaY = b.x - a.x;

	f32 signedArea2       = ((c.x - b.x) * (scanP.y - b.y)) - ((c.y - b.y) * (scanP.x - b.x));
	f32 signedArea2DeltaX = b.y - c.y;
	f32 signedArea2DeltaY = c.x - b.x;

	f32 signedArea3       = ((a.x - c.x) * (scanP.y - c.y)) - ((a.y - c.y) * (scanP.x - c.x));
	f32 signedArea3DeltaX = c.y - a.y;
	f32 signedArea3DeltaY = a.x - c.x;

	for (scanP.y = min.y; scanP.y < max.y; scanP.y++)
	{

		f32 signedArea1Row = signedArea1;
		f32 signedArea2Row = signedArea2;
		f32 signedArea3Row = signedArea3;

		for (scanP.x = min.x; scanP.x < max.x; scanP.x++)
		{
			if (signedArea1Row >= 0 && signedArea2Row >= 0 && signedArea3Row >= 0)
			{
				SetPixel(renderBuffer, scanP.x, scanP.y, color);
			}

			signedArea1Row += signedArea1DeltaX;
			signedArea2Row += signedArea2DeltaX;
			signedArea3Row += signedArea3DeltaX;
		}

		signedArea1 += signedArea1DeltaY;
		signedArea2 += signedArea2DeltaY;
		signedArea3 += signedArea3DeltaY;
	}
}

FILE_SCOPE void DrawText(PlatformRenderBuffer *const renderBuffer,
                         const DRFont font, DqnV2 pos, const char *const text,
                         i32 len = -1)
{
	if (!text) return;
	if (len == -1) len = Dqn_strlen(text);

	i32 index = 0;
	while (index < len)
	{
		if (text[index] < font.codepointRange.min &&
		    text[index] > font.codepointRange.max)
		{
			return;
		}

		i32 charIndex = text[index++] - (i32)font.codepointRange.min;
		DQN_ASSERT(charIndex >= 0 &&
		           charIndex < (i32)(font.codepointRange.max -
		                             font.codepointRange.min));

		stbtt_aligned_quad alignedQuad = {};
		stbtt_GetPackedQuad(font.atlas, font.bitmapDim.w, font.bitmapDim.h,
		                    charIndex, &pos.x, &pos.y, &alignedQuad, true);

		DqnRect fontRect = {};
		fontRect.min     = DqnV2_2f(alignedQuad.s0 * font.bitmapDim.w, alignedQuad.t1 * font.bitmapDim.h);
		fontRect.max     = DqnV2_2f(alignedQuad.s1 * font.bitmapDim.w, alignedQuad.t0 * font.bitmapDim.h);

		DqnRect screenRect = {};
		screenRect.min     = DqnV2_2f(alignedQuad.x0, alignedQuad.y0);
		screenRect.max     = DqnV2_2f(alignedQuad.x1, alignedQuad.y1);

		// TODO: Assumes 1bpp and pitch of font bitmap
		const u32 fontPitch = font.bitmapDim.w;
		u32 fontOffset      = (u32)(fontRect.min.x + (fontRect.max.y * fontPitch));
		u8 *fontPtr         = font.bitmap + fontOffset;

		DQN_ASSERT(sizeof(u32) == renderBuffer->bytesPerPixel);

		// NOTE(doyle): This offset, yOffset and flipping t1, t0 is necessary
		// for reversing the order of the font since its convention is 0,0 top
		// left and -ve Y.
		stbtt_packedchar *const charData = font.atlas + charIndex;
		f32 fontHeightOffset             = charData->yoff2 + charData->yoff;

		u32 screenOffset = (u32)(screenRect.min.x + (screenRect.min.y - fontHeightOffset) * renderBuffer->width);
		u32 *screenPtr   = ((u32 *)renderBuffer->memory) + screenOffset;

		i32 fontWidth    = DQN_ABS((i32)(fontRect.min.x - fontRect.max.x));
		i32 fontHeight   = DQN_ABS((i32)(fontRect.min.y - fontRect.max.y));
		for (i32 y = 0; y < fontHeight; y++)
		{
			for (i32 x = 0; x < fontWidth; x++)
			{
				i32 yOffset  = fontHeight - y;
				u8 fontPixel = fontPtr[x + (yOffset * fontPitch)];
				if (fontPixel == 0) continue;

				f32 srcA = (fontPixel / 255.0f);
				f32 srcR = (fontPixel / 255.0f);
				f32 srcG = (fontPixel / 255.0f);
				f32 srcB = (fontPixel / 255.0f);

				u32 offset = x + (y * renderBuffer->width);
				u32 old    = screenPtr[offset];
				f32 oldR   = (f32)((old >> 16) & 0xFF) / 255.0f;
				f32 oldG   = (f32)((old >> 8) &  0xFF) / 255.0f;
				f32 oldB   = (f32)((old >> 0) &  0xFF) / 255.0f;

				f32 invA  = 1 - srcA;
				f32 destR = srcR + (invA * oldR);
				f32 destG = srcG + (invA * oldG);
				f32 destB = srcB + (invA * oldB);
				u32 dest  = ((u32)(destR * 255.0f) << 16) |
				            ((u32)(destG * 255.0f) << 8) |
				            ((u32)(destB * 255.0f) << 0);

				screenPtr[offset] = dest;
			}
		}
	}
}


FILE_SCOPE void ClearRenderBuffer(PlatformRenderBuffer *const renderBuffer, DqnV3 color)
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

FILE_SCOPE void BitmapFontCreate(const PlatformAPI api,
                                 PlatformMemory *const memory,
                                 DRFont *const font, const char *const path,
                                 const DqnV2i bitmapDim,
                                 const DqnV2i codepointRange,
                                 const f32 sizeInPt)
{
	font->bitmapDim      = bitmapDim;
	font->codepointRange = codepointRange;
	font->sizeInPt       = sizeInPt;

	DqnTempBuffer transientTempBufferRegion =
	    DqnMemBuffer_BeginTempRegion(&memory->transientBuffer);

	////////////////////////////////////////////////////////////////////////////
	// Load font data
	////////////////////////////////////////////////////////////////////////////
	PlatformFile file = {};
	bool result = api.FileOpen(path, &file, PlatformFilePermissionFlag_Read);
	DQN_ASSERT(result);

	u8 *fontBuf = (u8 *)DqnMemBuffer_Allocate(&memory->transientBuffer, file.size);
	size_t bytesRead = api.FileRead(&file, fontBuf, file.size);
	DQN_ASSERT(bytesRead == file.size);
	api.FileClose(&file);

	stbtt_fontinfo fontInfo = {};
	DQN_ASSERT(stbtt_InitFont(&fontInfo, fontBuf, 0) != 0);
#if DR_DEBUG
	DQN_ASSERT(stbtt_GetNumberOfFonts(fontBuf) == 1);
#endif

	////////////////////////////////////////////////////////////////////////////
	// Pack font data to bitmap
	////////////////////////////////////////////////////////////////////////////
	font->bitmap = (u8 *)DqnMemBuffer_Allocate(
	    &memory->permanentBuffer,
	    (size_t)(font->bitmapDim.w * font->bitmapDim.h));

	stbtt_pack_context fontPackContext = {};
	DQN_ASSERT(stbtt_PackBegin(&fontPackContext, font->bitmap, bitmapDim.w,
	                           bitmapDim.h, 0, 1, NULL) == 1);
	{
		i32 numCodepoints =
		    (i32)((codepointRange.max + 1) - codepointRange.min);

		font->atlas = (stbtt_packedchar *)DqnMemBuffer_Allocate(
		    &memory->permanentBuffer, numCodepoints * sizeof(stbtt_packedchar));
		stbtt_PackFontRange(&fontPackContext, fontBuf, 0,
		                    STBTT_POINT_SIZE(sizeInPt), (i32)codepointRange.min,
		                    numCodepoints, font->atlas);
	}
	stbtt_PackEnd(&fontPackContext);

	////////////////////////////////////////////////////////////////////////////
	// Premultiply Alpha of Bitmap
	////////////////////////////////////////////////////////////////////////////
	for (i32 y = 0; y < bitmapDim.h; y++)
	{
		for (i32 x = 0; x < bitmapDim.w; x++)
		{
			// NOTE: Bitmap from stb_truetype is 1BPP. So the actual color
			// value represents its' alpha value but also its' color.
			u32 index            = x + (y * bitmapDim.w);
			f32 alpha            = (f32)(font->bitmap[index]) / 255.0f;
			f32 color            = alpha;
			f32 preMulAlphaColor = color * alpha;

			font->bitmap[index] = (u8)(preMulAlphaColor * 255.0f);
		}
	}

#ifdef DR_DEBUG_RENDER_FONT_BITMAP
	stbi_write_bmp("test.bmp", bitmapDim.w, bitmapDim.h, 1, font->bitmap);
#endif

	DqnMemBuffer_EndTempRegion(transientTempBufferRegion);
}

extern "C" void DR_Update(PlatformRenderBuffer *const renderBuffer,
                          PlatformInput *const input,
                          PlatformMemory *const memory)
{
	DRState *state = (DRState *)memory->context;
	if (!memory->isInit)
	{
		memory->isInit = true;
		memory->context =
		    DqnMemBuffer_Allocate(&memory->permanentBuffer, sizeof(DRState));
		DQN_ASSERT(memory->context);

		state = (DRState *)memory->context;
		BitmapFontCreate(input->api, memory, &state->font, "Roboto-bold.ttf",
		                 DqnV2i_2i(256, 256), DqnV2i_2i(' ', '~'), 35);
	}

	ClearRenderBuffer(renderBuffer, DqnV3_3f(0, 0, 0));
	DqnV3 colorRed    = DqnV3_3i(255, 0, 0);
	DqnV2i bufferMidP = DqnV2i_2f(renderBuffer->width * 0.5f, renderBuffer->height * 0.5f);
	i32 boundsOffset  = 50;

	DqnV2 t0[3] = {DqnV2_2i(10, 70), DqnV2_2i(50, 160), DqnV2_2i(70, 80)};
	DqnV2 t1[3] = {DqnV2_2i(180, 50),  DqnV2_2i(150, 1),   DqnV2_2i(70, 180)};
	DqnV2 t2[3] = {DqnV2_2i(180, 150), DqnV2_2i(120, 160), DqnV2_2i(130, 180)};
	LOCAL_PERSIST DqnV2 t3[3] = {
	    DqnV2_2i(boundsOffset, boundsOffset),
	    DqnV2_2i(bufferMidP.w, renderBuffer->height - boundsOffset),
	    DqnV2_2i(renderBuffer->width - boundsOffset, boundsOffset)};

	f32 minX = (f32)(renderBuffer->width - 1);
	f32 maxX = 0;
	for (i32 i = 0; i < DQN_ARRAY_COUNT(t3); i++)
	{
		t3[i].x += input->deltaForFrame * 2.0f;
		minX = DQN_MIN(t3[i].x, minX);
		maxX = DQN_MAX(t3[i].x, maxX);
	}

	if (minX >= renderBuffer->width - 1)
	{
		f32 triangleWidth = maxX - minX;
		for (i32 i = 0; i < DQN_ARRAY_COUNT(t3); i++)
		{
			t3[i].x -= (minX + triangleWidth);
		}
	}

	DrawTriangle(renderBuffer, t0[0], t0[1], t0[2], colorRed);
	DrawTriangle(renderBuffer, t1[0], t1[1], t1[2], colorRed);
	DrawTriangle(renderBuffer, t2[0], t2[1], t2[2], colorRed);
	// DrawTriangle(renderBuffer, t3[0], t3[1], t3[2], colorRed);

	DqnV2 fontP = DqnV2_2i(200, 180);
	DrawText(renderBuffer, state->font, fontP, "hello world!");
}
