#include "DTRendererRender.h"
#include "DTRendererPlatform.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb_rect_pack.h"
#include "external/stb_truetype.h"

// #define DTR_DEBUG_RENDER_FONT_BITMAP
#ifdef DTR_DEBUG_RENDER_FONT_BITMAP
	#define STB_IMAGE_WRITE_IMPLEMENTATION
	#include "external/stb_image_write.h"
#endif

inline DqnV4 DTRRender_PreMultiplyAlpha(const DqnV4 color)
{
	DqnV4 result;
	f32 normA = color.a / 255.0f;
	result.r  = color.r * normA;
	result.g  = color.g * normA;
	result.b  = color.b * normA;
	result.a  = color.a;

	return result;
}

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
	f32 destR = newR + (invANorm * srcR);
	f32 destG = newG + (invANorm * srcG);
	f32 destB = newB + (invANorm * srcB);

	DQN_ASSERT(destR >= 0 && destR <= 255.0f);
	DQN_ASSERT(destG >= 0 && destG <= 255.0f);
	DQN_ASSERT(destB >= 0 && destB <= 255.0f);

	u32 pixel = ((u32)(destR) << 16 |
	             (u32)(destG) << 8 |
	             (u32)(destB) << 0);
	bitmapPtr[x + (y * pitchInU32)] = pixel;

	globalDebug.setPixelsPerFrame++;
}

void DTRRender_Text(PlatformRenderBuffer *const renderBuffer,
                    const DTRFont font, DqnV2 pos, const char *const text,
                    DqnV4 color, i32 len)
{
	if (!text) return;
	if (!font.bitmap || !font.atlas || !renderBuffer) return;

	if (len == -1) len = Dqn_strlen(text);

	i32 index = 0;
	color     = DTRRender_PreMultiplyAlpha(color);
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

FILE_SCOPE void TransformPoints(const DqnV2 origin, DqnV2 *const pList,
                                const i32 numP, const DqnV2 scale,
                                const f32 rotation)
{
	if (!pList || numP == 0) return;

	DqnV2 xAxis = (DqnV2_2f(cosf(rotation), sinf(rotation)));
	DqnV2 yAxis = DqnV2_2f(-xAxis.y, xAxis.x);
	xAxis *= scale.x;
	yAxis *= scale.y;

	for (i32 i = 0; i < numP; i++)
	{
		DqnV2 p  = pList[i];
		pList[i] = origin + (xAxis * p.x) + (yAxis * p.y);
	}
}

void DTRRender_Line(PlatformRenderBuffer *const renderBuffer, DqnV2i a,
                    DqnV2i b, DqnV4 color)
{
	if (!renderBuffer) return;
	color = DTRRender_PreMultiplyAlpha(color);

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

void DTRRender_Rectangle(PlatformRenderBuffer *const renderBuffer, DqnV2 min,
                         DqnV2 max, DqnV4 color, const DqnV2 scale,
                         const f32 rotation, const DqnV2 anchor)
{
	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes
	////////////////////////////////////////////////////////////////////////////
	DqnV2 dim = DqnV2_2f(max.x - min.x, max.y - min.y);
	DQN_ASSERT(dim.w > 0 && dim.h > 0);
	DqnV2 initOrigin = DqnV2_2f(min.x + (anchor.x * dim.w), min.y + (anchor.y * dim.h));

	DqnV2 p1      = min - initOrigin;
	DqnV2 p2      = DqnV2_2f(max.x, min.y) - initOrigin;
	DqnV2 p3      = max - initOrigin;
	DqnV2 p4      = DqnV2_2f(min.x, max.y) - initOrigin;
	DqnV2 pList[] = {p1, p2, p3, p4};

	TransformPoints(initOrigin, pList, DQN_ARRAY_COUNT(pList), scale, rotation);
	min = pList[0];
	max = pList[0];
	for (i32 i = 1; i < DQN_ARRAY_COUNT(pList); i++)
	{
		DqnV2 checkP = pList[i];
		min.x = DQN_MIN(min.x, checkP.x);
		min.y = DQN_MIN(min.y, checkP.y);
		max.x = DQN_MAX(max.x, checkP.x);
		max.y = DQN_MAX(max.y, checkP.y);
	}

	color = DTRRender_PreMultiplyAlpha(color);
	////////////////////////////////////////////////////////////////////////////
	// Clip Drawing Space
	////////////////////////////////////////////////////////////////////////////
	DqnRect rect = DqnRect_4f(min.x, min.y, max.x, max.y);
	DqnRect clip = DqnRect_4i(0, 0, renderBuffer->width, renderBuffer->height);

	DqnRect clippedRect = DqnRect_ClipRect(rect, clip);
	DqnV2 clippedSize  = DqnRect_GetSizeV2(clippedRect);

	////////////////////////////////////////////////////////////////////////////
	// Render
	////////////////////////////////////////////////////////////////////////////
	if (rotation != 0)
	{
		for (i32 y = 0; y < clippedSize.w; y++)
		{
			i32 bufferY = (i32)clippedRect.min.y + y;
			for (i32 x = 0; x < clippedSize.h; x++)
			{
				i32 bufferX = (i32)clippedRect.min.x + x;
				bool pIsInside = true;

				for (i32 pIndex = 0; pIndex < DQN_ARRAY_COUNT(pList);
				     pIndex++)
				{
					DqnV2 origin  = pList[pIndex];
					DqnV2 line    = pList[(pIndex + 1) % DQN_ARRAY_COUNT(pList)] - origin;
					DqnV2 axis    = DqnV2_2i(bufferX, bufferY) - origin;
					f32 dotResult = DqnV2_Dot(line, axis);

					if (dotResult < 0)
					{
						pIsInside = false;
						break;
					}
				}

				if (pIsInside) SetPixel(renderBuffer, bufferX, bufferY, color);
			}
		}
	}
	else
	{
		for (i32 y = 0; y < clippedSize.h; y++)
		{
			i32 bufferY = (i32)clippedRect.min.y + y;
			for (i32 x = 0; x < clippedSize.w; x++)
			{
				i32 bufferX = (i32)clippedRect.min.x + x;
				SetPixel(renderBuffer, bufferX, bufferY, color);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	if (DTR_DEBUG)
	{
		// Draw Bounding box
		{
			DTRRender_Line(renderBuffer, DqnV2i_2f(min.x, min.y), DqnV2i_2f(min.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2f(min.x, max.y), DqnV2i_2f(max.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2f(max.x, max.y), DqnV2i_2f(max.x, min.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2f(max.x, min.y), DqnV2i_2f(min.x, min.y), color);
		}

		// Draw rotating outline
		if (rotation > 0)
		{
			DqnV4 green = DqnV4_4f(0, 255, 0, 255);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[0]), DqnV2i_V2(pList[1]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[1]), DqnV2i_V2(pList[2]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[2]), DqnV2i_V2(pList[3]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[3]), DqnV2i_V2(pList[0]), green);
		}

	}
}

void DTRRender_Triangle(PlatformRenderBuffer *const renderBuffer, DqnV2 p1,
                        DqnV2 p2, DqnV2 p3, DqnV4 color, const DqnV2 scale,
                        const f32 rotation, const DqnV2 anchor)
{
	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes
	////////////////////////////////////////////////////////////////////////////
	DqnV2 p1p2         = p2 - p1;
	DqnV2 p1p3         = p3 - p1;
	DqnV2 p1p2Anchored = p1p2 * anchor;
	DqnV2 p1p3Anchored = p1p3 * anchor;

	DqnV2 origin   = p1 + p1p2Anchored + p1p3Anchored;
	DqnV2 pList[3] = {p1 - origin, p2 - origin, p3 - origin};
	TransformPoints(origin, pList, DQN_ARRAY_COUNT(pList), scale, rotation);
	p1 = pList[0];
	p2 = pList[1];
	p3 = pList[2];

	color      = DTRRender_PreMultiplyAlpha(color);

	////////////////////////////////////////////////////////////////////////////
	// Calculate Bounding Box
	////////////////////////////////////////////////////////////////////////////
	DqnV2i max = DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x),
	                       DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
	DqnV2i min = DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x),
	                       DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
	min.x = DQN_MAX(min.x, 0);
	min.y = DQN_MAX(min.y, 0);
	max.x = DQN_MIN(max.x, renderBuffer->width - 1);
	max.y = DQN_MIN(max.y, renderBuffer->height - 1);

	/*
	   /////////////////////////////////////////////////////////////////////////
	   // Rearranging the Determinant
	   /////////////////////////////////////////////////////////////////////////
	   Given two points that form a line and an extra point to test, we can
	   determine whether a point lies on the line, or is to the left or right of
	   a the line.

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

	f32 area2Times = ((p2.x - p1.x) * (p2.y + p1.y)) +
	                 ((p3.x - p2.x) * (p3.y + p2.y)) +
	                 ((p1.x - p3.x) * (p1.y + p3.y));
	if (area2Times > 0)
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV2, p2, p3);
	}

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

	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
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

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	if (DTR_DEBUG)
	{
		// Draw Bounding box
		{
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, min.y), DqnV2i_2i(min.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, max.y), DqnV2i_2i(max.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, max.y), DqnV2i_2i(max.x, min.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, min.y), DqnV2i_2i(min.x, min.y), color);
		}

		// Draw Triangle Coordinate Basis
		{
			DqnV2 xAxis = DqnV2_2f(cosf(rotation), sinf(rotation)) * scale.x;
			DqnV2 yAxis = DqnV2_2f(-xAxis.y, xAxis.x) * scale.y;
			DqnV4 coordSysColor = DqnV4_4f(0, 255, 255, 255);
			i32 axisLen = 50;
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(xAxis * axisLen), coordSysColor);
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(yAxis * axisLen), coordSysColor);
		}

		// Draw axis point
		{
			DqnV4 green  = DqnV4_4f(0, 255, 0, 255);
			DqnV4 blue   = DqnV4_4f(0, 0, 255, 255);
			DqnV4 purple = DqnV4_4f(255, 0, 255, 255);

			DTRRender_Rectangle(renderBuffer, p1 - DqnV2_1f(5), p1 + DqnV2_1f(5), green);
			DTRRender_Rectangle(renderBuffer, p2 - DqnV2_1f(5), p2 + DqnV2_1f(5), blue);
			DTRRender_Rectangle(renderBuffer, p3 - DqnV2_1f(5), p3 + DqnV2_1f(5), purple);
		}
	}
}

void DTRRender_Bitmap(PlatformRenderBuffer *const renderBuffer,
                      DTRBitmap *const bitmap, i32 x, i32 y)
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

void DTRRender_Clear(PlatformRenderBuffer *const renderBuffer,
                     const DqnV3 color)
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


