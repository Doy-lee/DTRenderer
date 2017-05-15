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

#include <math.h>

#define DR_DEBUG 1
typedef struct DRFont
{
	u8    *bitmap;
	DqnV2i bitmapDim;
	DqnV2i codepointRange;
	f32    sizeInPt;

	stbtt_packedchar *atlas;
} DRFont;

typedef struct DRBitmap
{
	u8    *memory;
	DqnV2i dim;
	i32    bytesPerPixel;
} DRBitmap;

typedef struct DRState
{
	DRFont   font;
	DRBitmap bitmap;
} DRState;

typedef struct DRDebug
{
	DRFont *font;
	DqnV2   displayP;
	i32     displayYOffset;

	u64 setPixelsPerFrame;
	u64 totalSetPixels;

} DRDebug;

FILE_SCOPE inline DqnV4 PreMultiplyAlpha(DqnV4 color)
{
	DqnV4 result;
	f32 normA = color.a / 255.0f;
	result.r  = color.r * normA;
	result.g  = color.g * normA;
	result.b  = color.b * normA;
	result.a  = color.a;

	return result;
}

FILE_SCOPE DRDebug globalDebug;

// IMPORTANT(doyle): Color is expected to be premultiplied already
FILE_SCOPE inline void SetPixel(PlatformRenderBuffer *const renderBuffer,
                                const i32 x, const i32 y, const DqnV4 color)
{
	if (!renderBuffer) return;
	if (x <= 0 || x > (renderBuffer->width - 1)) return;
	if (y <= 0 || y > (renderBuffer->height - 1)) return;

	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	const u32 pitchInU32 = (renderBuffer->width * renderBuffer->bytesPerPixel) / 4;

	f32 newA     = color.a;
	f32 newANorm = newA / 255.0f;
	f32 newR     = color.r;
	f32 newG     = color.g;
	f32 newB     = color.b;

	u32 src  = bitmapPtr[x + (y * pitchInU32)];
	f32 srcR = (f32)((src >> 16) & 0xFF);
	f32 srcG = (f32)((src >> 8) & 0xFF);
	f32 srcB = (f32)((src >> 0) & 0xFF);

	// NOTE(doyle): AlphaBlend equations is (alpha * new) + (1 - alpha) * src.
	// IMPORTANT(doyle): We pre-multiply so we can take out the (alpha * new)
	f32 invANorm  = 1 - newANorm;
	// f32 destA = (((1 - srcA) * newA) + srcA) * 255.0f;
	f32 destR = newR + (invANorm * srcR);
	f32 destG = newG + (invANorm * srcG);
	f32 destB = newB + (invANorm * srcB);

	// DQN_ASSERT(destA >= 0 && destA <= 255.0f);
	DQN_ASSERT(destR >= 0 && destR <= 255.0f);
	DQN_ASSERT(destG >= 0 && destG <= 255.0f);
	DQN_ASSERT(destB >= 0 && destB <= 255.0f);

	u32 pixel = ((u32)(destR) << 16 |
	             (u32)(destG) << 8 |
	             (u32)(destB) << 0);
	bitmapPtr[x + (y * pitchInU32)] = pixel;

	globalDebug.setPixelsPerFrame++;
}

FILE_SCOPE void DrawText(PlatformRenderBuffer *const renderBuffer,
                         const DRFont font, DqnV2 pos, const char *const text,
                         DqnV4 color = DqnV4_4f(255, 255, 255, 255), i32 len = -1)
{
	if (!text) return;
	if (len == -1) len = Dqn_strlen(text);

	i32 index = 0;
	color     = PreMultiplyAlpha(color);
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
				i32 yOffset = fontHeight - y;
				u8 srcA     = fontPtr[x + (yOffset * fontPitch)];
				if (srcA == 0) continue;

				f32 srcANorm = srcA / 255.0f;
				DqnV4 resultColor = {};
				resultColor.r     = color.r * srcANorm;
				resultColor.g     = color.g * srcANorm;
				resultColor.b     = color.b * srcANorm;
				resultColor.a     = color.a * srcANorm;

				i32 actualX = (i32)(screenRect.min.x + x);
				i32 actualY = (i32)(screenRect.min.y + y - fontHeightOffset);
				SetPixel(renderBuffer, actualX, actualY, resultColor);
			}
		}
	}
}

FILE_SCOPE void DebugPushText(PlatformRenderBuffer *const renderBuffer,
                              const char *const formatStr, ...)
{
#ifdef DR_DEBUG
	DRDebug *const debug = &globalDebug;
	char str[1024]       = {};

	va_list argList;
	va_start(argList, formatStr);
	{
		i32 numCopied = Dqn_vsprintf(str, formatStr, argList);
		DQN_ASSERT(numCopied < DQN_ARRAY_COUNT(str));
	}
	va_end(argList);

	DrawText(renderBuffer, *debug->font, debug->displayP, str);
	debug->displayP.y += globalDebug.displayYOffset;
#endif
}

FILE_SCOPE void DrawLine(PlatformRenderBuffer *const renderBuffer, DqnV2i a,
                         DqnV2i b, DqnV4 color)
{
	if (!renderBuffer) return;
	color = PreMultiplyAlpha(color);

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

FILE_SCOPE void TransformVertexes(const DqnV2 origin, DqnV2 *const vertexList,
                                  const i32 numVertexes, const DqnV2 scale,
                                  const f32 rotation)
{
	if (!vertexList || numVertexes == 0) return;

	DqnV2 xAxis = (DqnV2_2f(cosf(rotation), sinf(rotation)));
	DqnV2 yAxis = DqnV2_2f(-xAxis.y, xAxis.x);
	xAxis *= scale.x;
	yAxis *= scale.y;

	for (i32 i = 0; i < numVertexes; i++)
	{
		DqnV2 p       = vertexList[i];
		vertexList[i] = origin + (xAxis * p.x) + (yAxis * p.y);
	}
}

FILE_SCOPE void DrawTriangle(PlatformRenderBuffer *const renderBuffer, DqnV2 p1,
                             DqnV2 p2, DqnV2 p3, DqnV4 color,
                             DqnV2 scale = DqnV2_1f(1.0f), f32 rotation = 0,
                             DqnV2 anchor = DqnV2_1f(0.5f))
{
	f32 area2Times = ((p2.x - p1.x) * (p2.y + p1.y)) +
	                 ((p3.x - p2.x) * (p3.y + p2.y)) +
	                 ((p1.x - p3.x) * (p1.y + p3.y));
	if (area2Times < 0)
	{
		// Counter-clockwise, do nothing this is what we want.
	}
	else
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV2, p2, p3);
	}

	// Transform vertexes
#if 1
	{
		DqnV2 max = DqnV2_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x),
		                     DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
		DqnV2 min = DqnV2_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x),
		                     DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));

		DqnV2 boundsDim = DqnV2_2f(max.x - min.x, max.y - min.y);
		DQN_ASSERT(boundsDim.w > 0 && boundsDim.h > 0);
		DqnV2 origin = DqnV2_2f(min.x + (anchor.x * boundsDim.w), min.y + (anchor.y * boundsDim.h));

		DqnV2 vertexList[3] = {p1 - origin, p2 - origin, p3 - origin};
		TransformVertexes(origin, vertexList, DQN_ARRAY_COUNT(vertexList),
		                  scale, rotation);
		p1 = vertexList[0];
		p2 = vertexList[1];
		p3 = vertexList[2];
	}
#endif

	color      = PreMultiplyAlpha(color);
	DqnV2i max = DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x),
	                       DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
	DqnV2i min = DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x),
	                       DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));

	min.x = DQN_MAX(min.x, 0);
	min.y = DQN_MAX(min.y, 0);

	max.x = DQN_MIN(max.x, renderBuffer->width - 1);
	max.y = DQN_MIN(max.y, renderBuffer->height - 1);

#if 1
	DrawLine(renderBuffer, DqnV2i_2i(min.x, min.y), DqnV2i_2i(min.x, max.y), color);
	DrawLine(renderBuffer, DqnV2i_2i(min.x, max.y), DqnV2i_2i(max.x, max.y), color);
	DrawLine(renderBuffer, DqnV2i_2i(max.x, max.y), DqnV2i_2i(max.x, min.y), color);
	DrawLine(renderBuffer, DqnV2i_2i(max.x, min.y), DqnV2i_2i(min.x, min.y), color);
#endif

	/*
	   /////////////////////////////////////////////////////////////////////////
	   // Rearranging the Determinant
	   /////////////////////////////////////////////////////////////////////////
	   Given two points that form a line and an extra point to test, we can
	   determine whether a point lies on the line, or is to the left or right of
	   a the line.

	   Then you can imagine if we iterate over the triangle vertexes, 2 at at
	   time and a last point P, being the point we want to test if it's inside
	   the triangle, then if the point is considered to lie on the side of the
	   line forming the interior of the triangle for all 3 vertexes then the
	   point is inside the triangle. We can do this using the determinant.

	   First forming a 3x3 matrix of our terms with a, b being from the triangle
	   and test point c, we can derive a 2x2 matrix by subtracting the 1st
	   column from the 2nd and 1st column from the third.

	       | ax bx cx |     | (bx - ax)  (cx - ax) |
	   m = | ay by cy | ==> | (by - ay)  (cy - ay) |
	       | 1  1  1  |

	   From our 2x2 representation we can calculate the determinant which gives
	   us the signed area of the triangle extended into a parallelogram.

	   det(m) = (bx - ax)(cy - ay) - (by - ay)(cx - ax)

	   Depending on the order of the vertices supplied, if it's
	   - CCW and c(x,y) is outside the line (triangle), the signed area is negative
	   - CCW and c(x,y) is inside  the line (triangle), the signed area is positive
	   - CW  and c(x,y) is outside the line (triangle), the signed area is positive
	   - CW  and c(x,y) is inside  the line (triangle), the signed area is negative

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

	   BaryCentricC(P) = (SignedArea(P) with vertex A and B)/SignedArea(with the orig triangle vertex)
	   BaryCentricA(P) = (SignedArea(P) with vertex B and C)/SignedArea(with the orig triangle vertex)
	   BaryCentricB(P) = (SignedArea(P) with vertex C and A)/SignedArea(with the orig triangle vertex)
	*/

	const DqnV2 a = p1;
	const DqnV2 b = p2;
	const DqnV2 c = p3;

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

	f32 invSignedAreaParallelogram = 1 / ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));

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

#if 1
	{
		DqnV2 xAxis = DqnV2_2f(cosf(rotation), sinf(rotation)) * scale.x;
		DqnV2 yAxis = DqnV2_2f(-xAxis.y, xAxis.x) * scale.y;

		DqnV2 vertexList[3] = {p1, p2, p3};
		DqnRect bounds = {};
		bounds.min     = vertexList[0];
		bounds.max     = vertexList[0];
		for (i32 i = 1; i < DQN_ARRAY_COUNT(vertexList); i++)
		{
			const DqnV2 p = vertexList[i];
			bounds.min    = DqnV2_2f(DQN_MIN(p.x, bounds.min.x),
			                      DQN_MIN(p.y, bounds.min.y));
			bounds.max = DqnV2_2f(DQN_MAX(p.x, bounds.max.x),
			                      DQN_MAX(p.y, bounds.max.y));
		}

		DqnV2 boundsDim = DqnRect_GetSizeV2(bounds);
		DqnV2 origin    = DqnV2_2f(bounds.min.x + (anchor.x * boundsDim.w),
		                           bounds.min.y + (anchor.y * boundsDim.h));

		DqnV4 coordSysColor = DqnV4_4f(0, 255, 255, 255);
		i32 axisLen = 50;
		DrawLine(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(xAxis * axisLen), coordSysColor);
		DrawLine(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(yAxis * axisLen), coordSysColor);
	}
#endif
}

FILE_SCOPE void DrawRectangle(PlatformRenderBuffer *const renderBuffer,
                              DqnV2 min, DqnV2 max, DqnV4 color,
                              const DqnV2 scale  = DqnV2_1f(1.0f),
                              const f32 rotation = 0,
                              const DqnV2 anchor = DqnV2_1f(0.5f))
{
	// TODO(doyle): Do edge test for quads
	if (rotation > 0)
	{
		DqnV2 p1 = min;
		DqnV2 p2 = DqnV2_2f(max.x, min.y);
		DqnV2 p3 = max;
		DqnV2 p4 = DqnV2_2f(min.x, max.y);
		DrawTriangle(renderBuffer, p1, p2, p3, color, scale, rotation, anchor);
		DrawTriangle(renderBuffer, p1, p3, p4, color, scale, rotation, anchor);
		return;
	}

	// Transform vertexes
	{
		DqnV2 dim = DqnV2_2f(max.x - min.x, max.y - min.y);
		DQN_ASSERT(dim.w > 0 && dim.h > 0);
		DqnV2 origin = DqnV2_2f(min.x + (anchor.x * dim.w), min.y + (anchor.y * dim.h));

		DqnV2 p1 = min - origin;
		DqnV2 p2 = max - origin;
		DqnV2 vertexList[4] = {p1, p2};
		TransformVertexes(origin, vertexList, DQN_ARRAY_COUNT(vertexList),
		                  scale, rotation);
		min = vertexList[0];
		max = vertexList[1];
	}

	color = PreMultiplyAlpha(color);

	DqnRect rect = DqnRect_4f(min.x, min.y, max.x, max.y);
	DqnRect clip = DqnRect_4i(0, 0, renderBuffer->width, renderBuffer->height);

	DqnRect clippedRect = DqnRect_ClipRect(rect, clip);
	DqnV2 clippedSize  = DqnRect_GetSizeV2(clippedRect);

	DebugPushText(renderBuffer, "ClippedSized: %5.2f, %5.2f", clippedSize.w, clippedSize.h);
	for (i32 y = 0; y < clippedSize.w; y++)
	{
		i32 bufferY = (i32)clippedRect.min.y + y;
		for (i32 x = 0; x < clippedSize.h; x++)
		{
			i32 bufferX = (i32)clippedRect.min.x + x;
			SetPixel(renderBuffer, bufferX, bufferY, color);
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
		// stbtt_PackSetOversampling(&fontPackContext, 2, 2);

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
			DQN_ASSERT(preMulAlphaColor >= 0.0f && preMulAlphaColor <= 255.0f);

			font->bitmap[index] = (u8)(preMulAlphaColor * 255.0f);
		}
	}

#ifdef DR_DEBUG_RENDER_FONT_BITMAP
	stbi_write_bmp("test.bmp", bitmapDim.w, bitmapDim.h, 1, font->bitmap);
#endif

	DqnMemBuffer_EndTempRegion(transientTempBufferRegion);
}

FILE_SCOPE void DrawBitmap(PlatformRenderBuffer *const renderBuffer,
                           DRBitmap *const bitmap, i32 x, i32 y)
{
	if (!bitmap || !bitmap->memory || !renderBuffer) return;

	DqnRect viewport   = DqnRect_4i(0, 0, renderBuffer->width, renderBuffer->height);
	DqnRect bitmapRect = DqnRect_4i(x, y, x + bitmap->dim.w, y + bitmap->dim.h);
	bitmapRect         = DqnRect_ClipRect(bitmapRect, viewport);
	if (bitmapRect.max.x < 0 || bitmapRect.max.y < 0) return;

	i32 startX = (x > 0) ? 0 : DQN_ABS(x);
	i32 startY = (y > 0) ? 0 : DQN_ABS(y);

	i32 endX, endY;
	DqnRect_GetSize2i(bitmapRect, &endX, &endY);

	const i32 pitch  = bitmap->dim.w * bitmap->bytesPerPixel;
	for (i32 bitmapY = startY; bitmapY < endY; bitmapY++)
	{
		u8 *const srcRow = bitmap->memory + (bitmapY * pitch);
		i32 bufferY      = (i32)bitmapRect.min.y + bitmapY;

		for (i32 bitmapX = startX; bitmapX < endX; bitmapX++)
		{
			u32 *pixelPtr = (u32 *)srcRow;
			u32 pixel     = pixelPtr[bitmapX];
			i32 bufferX   = (i32)bitmapRect.min.x + bitmapX;

			DqnV4 color = {};
			color.a     = (f32)(pixel >> 24);
			color.b     = (f32)((pixel >> 16) & 0xFF);
			color.g     = (f32)((pixel >> 8) & 0xFF);
			color.r     = (f32)((pixel >> 0) & 0xFF);

			SetPixel(renderBuffer, bufferX, bufferY, color);
		}
	}
}

FILE_SCOPE bool BitmapLoad(const PlatformAPI api, DRBitmap *bitmap,
                           const char *const path,
                           DqnMemBuffer *const transientBuffer)
{
	if (!bitmap) return false;

	PlatformFile file = {};
	if (!api.FileOpen(path, &file, PlatformFilePermissionFlag_Read))
		return false;

	DqnTempBuffer tempBuffer = DqnMemBuffer_BeginTempRegion(transientBuffer);
	{
		u8 *const rawData =
		    (u8 *)DqnMemBuffer_Allocate(transientBuffer, file.size);
		size_t bytesRead = api.FileRead(&file, rawData, file.size);
		api.FileClose(&file);

		if (bytesRead != file.size)
		{
			DqnMemBuffer_EndTempRegion(tempBuffer);
			return false;
		}

		bitmap->memory =
		    stbi_load_from_memory(rawData, (i32)file.size, &bitmap->dim.w,
		                          &bitmap->dim.h, &bitmap->bytesPerPixel, 4);
	}
	DqnMemBuffer_EndTempRegion(tempBuffer);
	if (!bitmap->memory) return false;

	const i32 pitch = bitmap->dim.w * bitmap->bytesPerPixel;
	for (i32 y = 0; y < bitmap->dim.h; y++)
	{
		u8 *const srcRow = bitmap->memory + (y * pitch);
		for (i32 x = 0; x < bitmap->dim.w; x++)
		{
			u32 *pixelPtr = (u32 *)srcRow;
			u32 pixel     = pixelPtr[x];

			DqnV4 color = {};
			color.a     = (f32)(pixel >> 24);
			color.b     = (f32)((pixel >> 16) & 0xFF);
			color.g     = (f32)((pixel >> 8) & 0xFF);
			color.r     = (f32)((pixel >> 0) & 0xFF);
			color       = PreMultiplyAlpha(color);

			pixel = (((u32)color.a << 24) |
			         ((u32)color.b << 16) |
			         ((u32)color.g << 8) |
			         ((u32)color.r << 0));

			pixelPtr[x] = pixel;
		}
	}

	return true;
}

void DebugDisplayMemBuffer(PlatformRenderBuffer *const renderBuffer,
                           const char *const name,
                           const DqnMemBuffer *const buffer,
                           DqnV2 *const debugP, const DRFont font)
{
	if (!name && !buffer && !debugP) return;

	size_t totalUsed   = 0;
	size_t totalSize   = 0;
	size_t totalWasted = 0;
	i32 numBlocks      = 0;

	DqnMemBufferBlock *blockPtr = buffer->block;
	while (blockPtr)
	{
		totalUsed += blockPtr->used;
		totalSize += blockPtr->size;
		blockPtr = blockPtr->prevBlock;
		numBlocks++;
	}

	size_t totalUsedKb   = totalUsed / 1024;
	size_t totalSizeKb   = totalSize / 1024;
	size_t totalWastedKb = totalWasted / 1024;

	char str[128] = {};
	Dqn_sprintf(str, "%s: %d block(s): %_$lld/%_$lld", name, numBlocks, totalUsed,
	            totalSize);

	DrawText(renderBuffer, font, *debugP, str);
	debugP->y += globalDebug.displayYOffset;
}

void DebugUpdate(PlatformRenderBuffer *const renderBuffer,
                 PlatformInput *const input, PlatformMemory *const memory)
{
#ifdef DR_DEBUG
	DRDebug *const debug = &globalDebug;

	debug->totalSetPixels += debug->setPixelsPerFrame;
	debug->totalSetPixels = DQN_MAX(0, debug->totalSetPixels);

	// totalSetPixels
	{
		char str[128] = {};
		Dqn_sprintf(str, "%s: %'lld", "TotalSetPixels", debug->totalSetPixels);
		DrawText(renderBuffer, *debug->font, debug->displayP, str);
		debug->displayP.y += globalDebug.displayYOffset;
	}

	// setPixelsPerFrame
	{
		char str[128] = {};
		Dqn_sprintf(str, "%s: %'lld", "SetPixelsPerFrame", debug->setPixelsPerFrame);
		DrawText(renderBuffer, *debug->font, debug->displayP, str);
		debug->displayP.y += globalDebug.displayYOffset;
	}

	// memory
	{
		DebugDisplayMemBuffer(renderBuffer, "PermBuffer",
		                      &memory->permanentBuffer, &debug->displayP, *debug->font);
		DebugDisplayMemBuffer(renderBuffer, "TransBuffer",
		                      &memory->transientBuffer, &debug->displayP, *debug->font);
	}

	debug->setPixelsPerFrame = 0;
	debug->displayP =
	    DqnV2_2i(0, renderBuffer->height + globalDebug.displayYOffset);
#endif
}

extern "C" void DR_Update(PlatformRenderBuffer *const renderBuffer,
                          PlatformInput *const input,
                          PlatformMemory *const memory)
{
	DRState *state = (DRState *)memory->context;
	if (!memory->isInit)
	{
		stbi_set_flip_vertically_on_load(true);
		memory->isInit = true;
		memory->context =
		    DqnMemBuffer_Allocate(&memory->permanentBuffer, sizeof(DRState));
		DQN_ASSERT(memory->context);

		state = (DRState *)memory->context;
		BitmapFontCreate(input->api, memory, &state->font, "Roboto-bold.ttf",
		                 DqnV2i_2i(256, 256), DqnV2i_2i(' ', '~'), 16);
		DQN_ASSERT(BitmapLoad(input->api, &state->bitmap, "lune_logo.png",
		           &memory->transientBuffer));
	}

#ifdef DR_DEBUG
	if (input->executableReloaded || !memory->isInit)
	{
		globalDebug.font           = &state->font;
		globalDebug.displayYOffset = -(i32)(state->font.sizeInPt + 0.5f);
		globalDebug.displayP =
		    DqnV2_2i(0, renderBuffer->height + globalDebug.displayYOffset);
		DQN_ASSERT(globalDebug.displayYOffset < 0);
	}
#endif

	ClearRenderBuffer(renderBuffer, DqnV3_3f(0, 0, 0));
	DqnV4 colorRed    = DqnV4_4i(50, 0, 0, 255);
	DqnV2i bufferMidP = DqnV2i_2f(renderBuffer->width * 0.5f, renderBuffer->height * 0.5f);
	i32 boundsOffset  = 100;

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
		// t3[i].x += input->deltaForFrame * 2.0f;
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

	DqnV4 colorRedHalfA = DqnV4_4i(255, 0, 0, 190);
	LOCAL_PERSIST f32 rotation = 0;
	rotation += input->deltaForFrame * 0.25f;
	DqnV2 scale         = DqnV2_1f(1.0f);
	DrawTriangle(renderBuffer, t3[0], t3[1], t3[2], colorRedHalfA, scale,
	             rotation, DqnV2_1f(0.5f));

	DrawRectangle(renderBuffer, DqnV2_1f(300.0f), DqnV2_1f(300 + 20.0f), colorRed,
	              DqnV2_1f(1.0f), rotation);

	DqnV2 fontP = DqnV2_2i(200, 180);
	DrawText(renderBuffer, state->font, fontP, "hello world!");

	DrawBitmap(renderBuffer, &state->bitmap, 300, 250);
	DebugUpdate(renderBuffer, input, memory);
}
