#include "DTRendererRender.h"
#include "DTRendererDebug.h"
#include "DTRendererPlatform.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb_rect_pack.h"
#include "external/stb_truetype.h"

#include <intrin.h>

FILE_SCOPE const f32 COLOR_EPSILON = 0.9f;

inline void Make3PointsClockwise(DqnV3 *p1, DqnV3 *p2, DqnV3 *p3)
{
	f32 area2Times = ((p2->x - p1->x) * (p2->y + p1->y)) +
	                 ((p3->x - p2->x) * (p3->y + p2->y)) +
	                 ((p1->x - p3->x) * (p1->y + p3->y));
	if (area2Times > 0)
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV3, *p2, *p3);
	}
}

FILE_SCOPE inline DqnV4 PreMultiplyAlpha1(const DqnV4 color)
{
	DQN_ASSERT(color.a >= 0.0f && color.a <= 1.0f);
	DqnV4 result;
	result.r = color.r * color.a;
	result.g = color.g * color.a;
	result.b = color.b * color.a;
	result.a = color.a;

	DQN_ASSERT(result.r >= 0.0f && result.r <= 1.0f);
	DQN_ASSERT(result.g >= 0.0f && result.g <= 1.0f);
	DQN_ASSERT(result.b >= 0.0f && result.b <= 1.0f);

	DQN_ASSERT(result.a >= result.r);
	DQN_ASSERT(result.a >= result.g);
	DQN_ASSERT(result.a >= result.b);
	return result;
}

FILE_SCOPE inline DqnV4 PreMultiplyAlpha255(const DqnV4 color)
{
	DqnV4 result;
	f32 normA = color.a * DTRRENDER_INV_255;
	DQN_ASSERT(normA >= 0.0f && normA <= 1.0f + COLOR_EPSILON);
	result.r  = color.r * normA;
	result.g  = color.g * normA;
	result.b  = color.b * normA;
	result.a  = color.a;

	return result;
}

enum ColorSpace
{
	ColorSpace_SRGB,
	ColorSpace_Linear,
};

// NOTE(doyle): We are approximating the actual gamma correct value 2.2 to 2 as
// a compromise.
inline f32 DTRRender_SRGB1ToLinearSpacef(f32 val)
{
	DQN_ASSERT(val >= 0.0f && val <= 1.0f + COLOR_EPSILON);
	f32 result = DQN_SQUARED(val);
	return result;
}

inline DqnV4 DTRRender_SRGB1ToLinearSpaceV4(DqnV4 color)
{
	DqnV4 result;
	result.r = DTRRender_SRGB1ToLinearSpacef(color.r);
	result.g = DTRRender_SRGB1ToLinearSpacef(color.g);
	result.b = DTRRender_SRGB1ToLinearSpacef(color.b);
	result.a = color.a;

	return result;
}

inline f32 DTRRender_LinearToSRGB1Spacef(f32 val)
{
	DQN_ASSERT(val >= 0.0f && val <= 1.0f + COLOR_EPSILON);
	if (val == 0) return 0;
	f32 result = DqnMath_Sqrtf(val);
	return result;
}

inline DqnV4 DTRRender_LinearToSRGB1SpaceV4(DqnV4 color)
{
	DqnV4 result;
	result.r = DTRRender_LinearToSRGB1Spacef(color.r);
	result.g = DTRRender_LinearToSRGB1Spacef(color.g);
	result.b = DTRRender_LinearToSRGB1Spacef(color.b);
	result.a = color.a;

	return result;
}

inline DqnV4 DTRRender_PreMultiplyAlphaSRGB1WithLinearConversion(DqnV4 color)
{
	DqnV4 result = color;
	result       = DTRRender_SRGB1ToLinearSpaceV4(result);
	result       = PreMultiplyAlpha1(result);
	result       = DTRRender_LinearToSRGB1SpaceV4(result);

	return result;
}

// IMPORTANT(doyle): Color is expected to be premultiplied already
FILE_SCOPE inline void SetPixel(DTRRenderBuffer *const renderBuffer, const i32 x, const i32 y,
                                DqnV4 color, const enum ColorSpace colorSpace = ColorSpace_SRGB)
{
	if (!renderBuffer) return;
	if (x < 0 || x > (renderBuffer->width - 1)) return;
	if (y < 0 || y > (renderBuffer->height - 1)) return;
	DTR_DEBUG_EP_TIMED_FUNCTION();

	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	const u32 pitchInU32 = (renderBuffer->width * renderBuffer->bytesPerPixel) / 4;

	// If some alpha is involved, we need to apply gamma correction, but if the
	// new pixel is totally opaque or invisible then we're just flat out
	// overwriting/keeping the state of the pixel so we can save cycles by skipping.
	bool needGammaFix = (color.a > 0.0f || color.a < 1.0f + COLOR_EPSILON) && (colorSpace == ColorSpace_SRGB);
	if (needGammaFix) color = DTRRender_SRGB1ToLinearSpaceV4(color);

	u32 src = bitmapPtr[x + (y * pitchInU32)];
	f32 srcR = (f32)((src >> 16) & 0xFF) * DTRRENDER_INV_255;
	f32 srcG = (f32)((src >> 8) & 0xFF)  * DTRRENDER_INV_255;
	f32 srcB = (f32)((src >> 0) & 0xFF)  * DTRRENDER_INV_255;

	srcR = DTRRender_SRGB1ToLinearSpacef(srcR);
	srcG = DTRRender_SRGB1ToLinearSpacef(srcG);
	srcB = DTRRender_SRGB1ToLinearSpacef(srcB);

	// NOTE(doyle): AlphaBlend equations is (alpha * new) + (1 - alpha) * src.
	// IMPORTANT(doyle): We pre-multiply so we can take out the (alpha * new)
	f32 invANorm = 1 - color.a;
	f32 destR    = color.r + (invANorm * srcR);
	f32 destG    = color.g + (invANorm * srcG);
	f32 destB    = color.b + (invANorm * srcB);

	destR = DTRRender_LinearToSRGB1Spacef(destR) * 255.0f;
	destG = DTRRender_LinearToSRGB1Spacef(destG) * 255.0f;
	destB = DTRRender_LinearToSRGB1Spacef(destB) * 255.0f;

	if (DTR_DEBUG)
	{
		DQN_ASSERT((destR - 255.0f) < COLOR_EPSILON);
		DQN_ASSERT((destG - 255.0f) < COLOR_EPSILON);
		DQN_ASSERT((destB - 255.0f) < COLOR_EPSILON);
	}

	if (destR > 255.0f)
	{
		destR = 255;
	}

	if (destG > 255.0f)
	{
		destG = 255;
	}

	if (destB > 255.0f)
	{
		destB = 255;
	}

	u32 pixel = // ((u32)(destA) << 24 |
	             (u32)(destR) << 16 |
	             (u32)(destG) << 8 |
	             (u32)(destB) << 0;
	bitmapPtr[x + (y * pitchInU32)] = pixel;

	DTRDebug_CounterIncrement(DTRDebugCounter_SetPixels);
}

void DTRRender_Text(DTRRenderBuffer *const renderBuffer,
                    const DTRFont font, DqnV2 pos, const char *const text,
                    DqnV4 color, i32 len)
{
	if (!text) return;
	if (!font.bitmap || !font.atlas || !renderBuffer) return;
	DTR_DEBUG_EP_TIMED_FUNCTION();

	if (len == -1) len = Dqn_strlen(text);

	i32 index = 0;
	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);
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

				f32 srcANorm      = srcA / 255.0f;
				DqnV4 resultColor = {};
				resultColor.r     = color.r * srcANorm;
				resultColor.g     = color.g * srcANorm;
				resultColor.b     = color.b * srcANorm;
				resultColor.a     = color.a * srcANorm;

				i32 actualX = (i32)(screenRect.min.x + x);
				i32 actualY = (i32)(screenRect.min.y + y - fontHeightOffset);
				SetPixel(renderBuffer, actualX, actualY, resultColor, ColorSpace_Linear);
			}
		}
	}
}

FILE_SCOPE void TransformPoints(const DqnV2 origin, DqnV2 *const pList,
                                const i32 numP, const DqnV2 scale,
                                const f32 rotation)
{
	if (!pList || numP == 0) return;
	DTR_DEBUG_EP_TIMED_FUNCTION();

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

void DTRRender_Line(DTRRenderBuffer *const renderBuffer, DqnV2i a,
                    DqnV2i b, DqnV4 color)
{
	if (!renderBuffer) return;
	DTR_DEBUG_EP_TIMED_FUNCTION();

	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);

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
		SetPixel(renderBuffer, *plotX, *plotY, color, ColorSpace_Linear);

		distAccumulator += distFromPixelOrigin;
   		if (distAccumulator > run)
		{
			newY += delta;
			distAccumulator -= (run * 2);
		}
	}
}

// NOTE: This information is only particularly relevant for bitmaps so that
// after transformation, we can still programatically find the original
// coordinate system of the bitmap for texture mapping.
enum RectPointsIndex
{
	RectPointsIndex_Basis = 0,
	RectPointsIndex_XAxis,
	RectPointsIndex_Point,
	RectPointsIndex_YAxis,
	RectPointsIndex_Count
};

typedef struct RectPoints
{
	DqnV2 pList[RectPointsIndex_Count];
} RectPoints;

// Apply rotation and scale around the anchored point. This is a helper function that expands the
// min and max into the 4 vertexes of a rectangle then calls the normal transform routine.
// anchor: A normalised [0->1] value the points should be positioned from
FILE_SCOPE RectPoints TransformRectPoints(DqnV2 min, DqnV2 max, DTRRenderTransform transform)
{
	DqnV2 dim    = DqnV2_2f(max.x - min.x, max.y - min.y);
	DqnV2 origin = DqnV2_2f(min.x + (transform.anchor.x * dim.w), min.y + (transform.anchor.y * dim.h));
	DQN_ASSERT(dim.w > 0 && dim.h > 0);

	RectPoints result = {};
	result.pList[RectPointsIndex_Basis] = min - origin;
	result.pList[RectPointsIndex_XAxis] = DqnV2_2f(max.x, min.y) - origin;
	result.pList[RectPointsIndex_Point] = max - origin;
	result.pList[RectPointsIndex_YAxis] = DqnV2_2f(min.x, max.y) - origin;

	TransformPoints(origin, result.pList, DQN_ARRAY_COUNT(result.pList), transform.scale, transform.rotation);

	return result;
}

FILE_SCOPE DqnRect GetBoundingBox(const DqnV2 *const pList, const i32 numP)
{
	DqnRect result = {};
	if (numP == 0 || !pList) return result;

	result.min = pList[0];
	result.max = pList[0];
	for (i32 i = 1; i < numP; i++)
	{
		DqnV2 checkP = pList[i];
		result.min.x = DQN_MIN(result.min.x, checkP.x);
		result.min.y = DQN_MIN(result.min.y, checkP.y);

		result.max.x = DQN_MAX(result.max.x, checkP.x);
		result.max.y = DQN_MAX(result.max.y, checkP.y);
	}

	return result;
}

void DTRRender_Rectangle(DTRRenderBuffer *const renderBuffer, DqnV2 min, DqnV2 max,
                         DqnV4 color, const DTRRenderTransform transform)
{
	DTR_DEBUG_EP_TIMED_FUNCTION();
	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes
	////////////////////////////////////////////////////////////////////////////
	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);

	RectPoints rectPoints     = TransformRectPoints(min, max, transform);
	DqnV2 *const pList        = &rectPoints.pList[0];
	const i32 RECT_PLIST_SIZE = DQN_ARRAY_COUNT(rectPoints.pList);

	DqnRect bounds = GetBoundingBox(pList, RECT_PLIST_SIZE);
	min = bounds.min;
	max = bounds.max;

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
	if (transform.rotation != 0)
	{
		for (i32 y = 0; y < clippedSize.w; y++)
		{
			i32 bufferY = (i32)clippedRect.min.y + y;
			for (i32 x = 0; x < clippedSize.h; x++)
			{
				i32 bufferX = (i32)clippedRect.min.x + x;
				bool pIsInside = true;

				for (i32 pIndex = 0; pIndex < RECT_PLIST_SIZE; pIndex++)
				{
					DqnV2 origin  = pList[pIndex];
					DqnV2 line    = pList[(pIndex + 1) % RECT_PLIST_SIZE] - origin;
					DqnV2 axis    = DqnV2_2i(bufferX, bufferY) - origin;
					f32 dotResult = DqnV2_Dot(line, axis);

					if (dotResult < 0)
					{
						pIsInside = false;
						break;
					}
				}

				if (pIsInside) SetPixel(renderBuffer, bufferX, bufferY, color, ColorSpace_Linear);
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
				SetPixel(renderBuffer, bufferX, bufferY, color, ColorSpace_Linear);
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	if (DTR_DEBUG_RENDER)
	{
		// Draw Bounding box
		{
			DTRRender_Line(renderBuffer, DqnV2i_2f(min.x, min.y), DqnV2i_2f(min.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2f(min.x, max.y), DqnV2i_2f(max.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2f(max.x, max.y), DqnV2i_2f(max.x, min.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2f(max.x, min.y), DqnV2i_2f(min.x, min.y), color);
		}

		// Draw rotating outline
		if (transform.rotation > 0)
		{
			DqnV4 green = DqnV4_4f(0, 1, 0, 1);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[0]), DqnV2i_V2(pList[1]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[1]), DqnV2i_V2(pList[2]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[2]), DqnV2i_V2(pList[3]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[3]), DqnV2i_V2(pList[0]), green);
		}

	}
}

FILE_SCOPE void DebugBarycentricInternal(DqnV2 p, DqnV2 a, DqnV2 b, DqnV2 c, f32 *u, f32 *v, f32 *w)
{
	DqnV2 v0 = b - a;
	DqnV2 v1 = c - a;
	DqnV2 v2 = p - a;

	f32 d00   = DqnV2_Dot(v0, v0);
	f32 d01   = DqnV2_Dot(v0, v1);
	f32 d11   = DqnV2_Dot(v1, v1);
	f32 d20   = DqnV2_Dot(v2, v0);
	f32 d21   = DqnV2_Dot(v2, v1);
	f32 denom = d00 * d11 - d01 * d01;
	*v        = (d11 * d20 - d01 * d21) / denom;
	*w        = (d00 * d21 - d01 * d20) / denom;
	*u        = 1.0f - *v - *w;
}

typedef struct TriangleInclusionTest
{
	DqnV2i boundsMin;
	DqnV2i boundsMax;

	f32 signedAreaP1;
	f32 signedAreaP1DeltaX;
	f32 signedAreaP1DeltaY;

	f32 signedAreaP2;
	f32 signedAreaP2DeltaX;
	f32 signedAreaP2DeltaY;

	f32 signedAreaP3;
	f32 signedAreaP3DeltaX;
	f32 signedAreaP3DeltaY;

	f32 invSignedAreaParallelogram;
} TriangleInclusionTest;

typedef struct SIMDTriangleInclusionTest
{
	__m128 vertexZValues;
	__m128 signedAreaPixelDeltaX;
	__m128 signedAreaPixelDeltaY;
	__m128 invSignedAreaParallelogram_4x;
	__m128 startPixel;

	DqnV2i boundsMin;
	DqnV2i boundsMax;
	DqnV3  p1;
	DqnV3  p2;
	DqnV3  p3;
} SIMDTriangleInclusionTest;

FILE_SCOPE TriangleInclusionTest CreateTriangleInclusionTest(const i32 clipWidth,
                                                             const i32 clipHeight, DqnV3 p1,
                                                             DqnV3 p2, DqnV3 p3)
{
	Make3PointsClockwise(&p1, &p2, &p3);
	TriangleInclusionTest result = {};

	result.boundsMin   = DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x), DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
	result.boundsMax   = DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x), DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
	result.boundsMin.x = DQN_MAX(result.boundsMin.x, 0);
	result.boundsMin.y = DQN_MAX(result.boundsMin.y, 0);
	result.boundsMax.x = DQN_MIN(result.boundsMax.x, clipWidth);
	result.boundsMax.y = DQN_MIN(result.boundsMax.y, clipHeight);

	const DqnV3 a = p1;
	const DqnV3 b = p2;
	const DqnV3 c = p3;

	/*
	   /////////////////////////////////////////////////////////////////////////
	   // Rearranging the Determinant
	   /////////////////////////////////////////////////////////////////////////
	   Given two points that form a line and an extra point to test, we can
	   determine whether a point lies on the line, or is to the left or right of
	   a the line.

	   We can do this using the PerpDotProduct conceptually known as the cross
	   product in 2D. This can be expressed using the determinant and is the
	   method we are using.

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
	   the value of SignedArea by (ay - by) and similarly for y, (bx - ax)

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

	DqnV2i startP = result.boundsMin;

	// signed area for a, where P1 = A, P2 = B, P3 = C
	result.signedAreaP1       = ((p3.x - p2.x) * (startP.y - p2.y)) - ((p3.y - p2.y) * (startP.x - p2.x));
	result.signedAreaP1DeltaX = p2.y - p3.y;
	result.signedAreaP1DeltaY = p3.x - p2.x;

	// signed area for b
	result.signedAreaP2       = ((p1.x - p3.x) * (startP.y - p3.y)) - ((p1.y - p3.y) * (startP.x - p3.x));
	result.signedAreaP2DeltaX = p3.y - p1.y;
	result.signedAreaP2DeltaY = p1.x - p3.x;

	// signed area for c
	result.signedAreaP3       = ((p2.x - p1.x) * (startP.y - p1.y)) - ((p2.y - p1.y) * (startP.x - p1.x));
	result.signedAreaP3DeltaX = p1.y - p2.y;
	result.signedAreaP3DeltaY = p2.x - p1.x;

	f32 signedAreaParallelogram = result.signedAreaP1 + result.signedAreaP2 + result.signedAreaP3;

	if (signedAreaParallelogram == 0)
		result.invSignedAreaParallelogram = 0;
	else
		result.invSignedAreaParallelogram = (1.0f / signedAreaParallelogram);

	return result;
}

FILE_SCOPE SIMDTriangleInclusionTest CreateSimdTriangleInclusionTest(
    const DqnV3 p1, const DqnV3 p2, const DqnV3 p3, const TriangleInclusionTest inclusionTest)
{
	SIMDTriangleInclusionTest result = {};
	result.boundsMin = inclusionTest.boundsMin;
	result.boundsMax = inclusionTest.boundsMax;

	// NOTE: Order is important here!
	result.p1 = p1;
	result.p2 = p2;
	result.p3 = p3;
	result.vertexZValues         = _mm_set_ps(0, p3.z, p2.z, p1.z);
	result.signedAreaPixelDeltaX = _mm_set_ps(0,
	                                          inclusionTest.signedAreaP3DeltaX,
	                                          inclusionTest.signedAreaP2DeltaX,
	                                          inclusionTest.signedAreaP1DeltaX);
	result.signedAreaPixelDeltaY = _mm_set_ps(0,
	                                          inclusionTest.signedAreaP3DeltaY,
	                                          inclusionTest.signedAreaP2DeltaY,
	                                          inclusionTest.signedAreaP1DeltaY);
	result.invSignedAreaParallelogram_4x = _mm_set_ps1(inclusionTest.invSignedAreaParallelogram);

	result.startPixel = _mm_set_ps(0, inclusionTest.signedAreaP3,
	                                  inclusionTest.signedAreaP2,
	                                  inclusionTest.signedAreaP1);
	return result;
}

inline void RasteriseTexturedTriangle(DTRRenderBuffer *const renderBuffer, const DqnV3 p1,
                                      const DqnV3 p2, const DqnV3 p3, const DqnV2 uv1,
                                      const DqnV2 uv2, const DqnV2 uv3, DTRBitmap *const texture,
                                      const DqnV4 color)
{
	DqnV2i max = DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x), DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
	DqnV2i min = DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x), DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
	min.x      = DQN_MAX(min.x, 0);
	min.y      = DQN_MAX(min.y, 0);
	max.x      = DQN_MIN(max.x, renderBuffer->width - 1);
	max.y      = DQN_MIN(max.y, renderBuffer->height - 1);

	const u32 zBufferPitch = renderBuffer->width;
	const DqnV3 a          = p1;
	const DqnV3 b          = p2;
	const DqnV3 c          = p3;

	DqnV2i startP         = min;
	f32 signedArea1       = ((b.x - a.x) * (startP.y - a.y)) - ((b.y - a.y) * (startP.x - a.x));
	f32 signedArea1DeltaX = a.y - b.y;
	f32 signedArea1DeltaY = b.x - a.x;

	f32 signedArea2       = ((c.x - b.x) * (startP.y - b.y)) - ((c.y - b.y) * (startP.x - b.x));
	f32 signedArea2DeltaX = b.y - c.y;
	f32 signedArea2DeltaY = c.x - b.x;

	f32 signedArea3       = ((a.x - c.x) * (startP.y - c.y)) - ((a.y - c.y) * (startP.x - c.x));
	f32 signedArea3DeltaX = c.y - a.y;
	f32 signedArea3DeltaY = a.x - c.x;

	f32 signedAreaParallelogram = signedArea1 + signedArea2 + signedArea3;
	if (signedAreaParallelogram == 0) return;
	f32 invSignedAreaParallelogram = 1 / signedAreaParallelogram;

	for (i32 bufferY = min.y; bufferY < max.y; bufferY++)
	{
		f32 signedArea1Row = signedArea1;
		f32 signedArea2Row = signedArea2;
		f32 signedArea3Row = signedArea3;

		for (i32 bufferX = min.x; bufferX < max.x; bufferX++)
		{
			if (signedArea1Row >= 0 && signedArea2Row >= 0 && signedArea3Row >= 0)
			{
				f32 barycentricB = signedArea3Row * invSignedAreaParallelogram;
				f32 barycentricC = signedArea1Row * invSignedAreaParallelogram;

				if (DTR_DEBUG)
				{
					const f32 EPSILON = 0.1f;

					f32 debugSignedArea1 = ((b.x - a.x) * (bufferY - a.y)) - ((b.y - a.y) * (bufferX - a.x));
					f32 debugSignedArea2 = ((c.x - b.x) * (bufferY - b.y)) - ((c.y - b.y) * (bufferX - b.x));
					f32 debugSignedArea3 = ((a.x - c.x) * (bufferY - c.y)) - ((a.y - c.y) * (bufferX - c.x));

					f32 deltaSignedArea1 = DQN_ABS(debugSignedArea1 - signedArea1Row);
					f32 deltaSignedArea2 = DQN_ABS(debugSignedArea2 - signedArea2Row);
					f32 deltaSignedArea3 = DQN_ABS(debugSignedArea3 - signedArea3Row);
					DQN_ASSERT(deltaSignedArea1 < EPSILON && deltaSignedArea2 < EPSILON &&
					           deltaSignedArea3 < EPSILON)

					f32 debugBarycentricA, debugBarycentricB, debugBarycentricC;
					DebugBarycentricInternal(DqnV2_2i(bufferX, bufferY), a.xy, b.xy, c.xy,
					                         &debugBarycentricA, &debugBarycentricB,
					                         &debugBarycentricC);

					f32 deltaBaryB = DQN_ABS(barycentricB - debugBarycentricB);
					f32 deltaBaryC = DQN_ABS(barycentricC - debugBarycentricC);

					DQN_ASSERT(deltaBaryB < EPSILON && deltaBaryC < EPSILON)
				}

				i32 zBufferIndex = bufferX + (bufferY * zBufferPitch);
				f32 pixelZValue = a.z + (barycentricB * (b.z - a.z)) + (barycentricC * (c.z - a.z));
				f32 currZValue  = renderBuffer->zBuffer[zBufferIndex];
				DQN_ASSERT(zBufferIndex < (renderBuffer->width * renderBuffer->height));

				if (pixelZValue > currZValue)
				{
					renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
					if (texture)
					{
						u8 *texturePtr         = texture->memory;
						const u32 texturePitch = texture->bytesPerPixel * texture->dim.w;

						DqnV2 uv =
						    uv1 + ((uv2 - uv1) * barycentricB) + ((uv3 - uv1) * barycentricC);

						const f32 EPSILON = 0.1f;
						DQN_ASSERT(uv.x >= 0 && uv.x < 1.0f + EPSILON);
						DQN_ASSERT(uv.y >= 0 && uv.y < 1.0f + EPSILON);

						uv.x = DqnMath_Clampf(uv.x, 0.0f, 1.0f);
						uv.y = DqnMath_Clampf(uv.y, 0.0f, 1.0f);

						f32 texelXf = uv.x * texture->dim.w;
						f32 texelYf = uv.y * texture->dim.h;
						DQN_ASSERT(texelXf >= 0 && texelXf < texture->dim.w);
						DQN_ASSERT(texelYf >= 0 && texelYf < texture->dim.h);

						i32 texelX = (i32)texelXf;
						i32 texelY = (i32)texelYf;

						u32 texel1 = *(u32 *)(texturePtr + (texelX * texture->bytesPerPixel) +
						                      (texelY * texturePitch));

						DqnV4 color1;
						color1.a = (f32)(texel1 >> 24);
						color1.b = (f32)((texel1 >> 16) & 0xFF);
						color1.g = (f32)((texel1 >> 8) & 0xFF);
						color1.r = (f32)((texel1 >> 0) & 0xFF);

						color1 *= DTRRENDER_INV_255;
						color1      = DTRRender_SRGB1ToLinearSpaceV4(color1);
						DqnV4 blend = color * color1;
						SetPixel(renderBuffer, bufferX, bufferY, blend, ColorSpace_Linear);
					}
					else
					{
						SetPixel(renderBuffer, bufferX, bufferY, color, ColorSpace_Linear);
					}
				}
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

FILE_SCOPE inline f32 Triangle2TimesSignedArea(const DqnV2 a, const DqnV2 b, const DqnV2 c)
{
	f32 result = ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// SIMD
////////////////////////////////////////////////////////////////////////////////
// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline void SIMDDebug_ColorInRange(__m128 color, f32 min, f32 max)
{
	if (DTR_DEBUG)
	{
		f32 r = ((f32 *)&color)[0];
		f32 g = ((f32 *)&color)[1];
		f32 b = ((f32 *)&color)[2];
		f32 a = ((f32 *)&color)[3];
		DQN_ASSERT(r >= min && r <= max);
		DQN_ASSERT(g >= min && g <= max);
		DQN_ASSERT(b >= min && b <= max);
		DQN_ASSERT(a >= min && a <= max);
	}
}

FILE_SCOPE inline __m128 SIMD_SRGB1ToLinearSpace(__m128 color)
{
	SIMDDebug_ColorInRange(color, 0.0f, 1.0f);

	f32 preserveAlpha   = ((f32 *)&color)[3];
	__m128 result       = _mm_mul_ps(color, color);
	((f32 *)&result)[3] = preserveAlpha;

	return result;
}

FILE_SCOPE inline __m128 SIMD_SRGB255ToLinearSpace1(__m128 color)
{
	LOCAL_PERSIST const __m128 INV255_4X = _mm_set_ps1(DTRRENDER_INV_255);
	color                                = _mm_mul_ps(color, INV255_4X);

	f32 preserveAlpha   = ((f32 *)&color)[3];
	__m128 result       = _mm_mul_ps(color, color);
	((f32 *)&result)[3] = preserveAlpha;

	return result;
}

FILE_SCOPE inline __m128 SIMD_LinearSpace1ToSRGB1(__m128 color)
{
	SIMDDebug_ColorInRange(color, 0.0f, 1.0f);

	f32 preserveAlpha   = ((f32 *)&color)[3];
	__m128 result       = _mm_sqrt_ps(color);
	((f32 *)&result)[3] = preserveAlpha;

	return result;
}


// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline __m128 SIMD_PreMultiplyAlpha1(__m128 color)
{
	f32 alpha        = ((f32 *)&color)[3];
	__m128 simdAlpha = _mm_set_ps(1, alpha, alpha, alpha);
	__m128 result    = _mm_mul_ps(color, simdAlpha);

	return result;
}

FILE_SCOPE inline DqnV2 Get2DOriginFromTransformAnchor(const DqnV2 p1, const DqnV2 p2,
                                                       const DqnV2 p3,
                                                       const DTRRenderTransform transform)
{
	DqnV2 p1p2 = p2 - p1;
	DqnV2 p1p3 = p3 - p1;

	DqnV2 p1p2Anchored = p1p2 * transform.anchor;
	DqnV2 p1p3Anchored = p1p3 * transform.anchor;
	DqnV2 origin       = p1 + p1p2Anchored + p1p3Anchored;

	return origin;
}

// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline void SIMD_SetPixel(DTRRenderBuffer *const renderBuffer, const i32 x, const i32 y,
                                     __m128 color,
                                     const enum ColorSpace colorSpace = ColorSpace_SRGB)
{
	if (!renderBuffer) return;
	if (x < 0 || x > (renderBuffer->width - 1)) return;
	if (y < 0 || y > (renderBuffer->height - 1)) return;

	DTR_DEBUG_EP_TIMED_FUNCTION();
	SIMDDebug_ColorInRange(color, 0.0f, 1.0f);

	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	const u32 pitchInU32 = (renderBuffer->width * renderBuffer->bytesPerPixel) / 4;

	// If some alpha is involved, we need to apply gamma correction, but if the
	// new pixel is totally opaque or invisible then we're just flat out
	// overwriting/keeping the state of the pixel so we can save cycles by skipping.
	f32 alpha = ((f32 *)&color)[3];
	bool needGammaFix = (alpha > 0.0f || alpha < (1.0f + COLOR_EPSILON)) && (colorSpace == ColorSpace_SRGB);
	if (needGammaFix) color = SIMD_SRGB1ToLinearSpace(color);

	// Format: u32 == (XX, RR, GG, BB)
	u32 srcPixel = bitmapPtr[x + (y * pitchInU32)];
	__m128 src = _mm_set_ps(0,
	                        (f32)((srcPixel >> 0) & 0xFF),
	                        (f32)((srcPixel >> 8) & 0xFF),
	                        (f32)((srcPixel >> 16) & 0xFF));
	src = SIMD_SRGB255ToLinearSpace1(src);

	f32 invA       = 1 - alpha;
	__m128 invA_4x = _mm_set_ps1(invA);

	// PreAlphaMulColor + (1 - Alpha) * Src
	__m128 oneMinusAlphaSrc = _mm_mul_ps(invA_4x, src);
	__m128 dest             = _mm_add_ps(color, oneMinusAlphaSrc);
	dest                    = SIMD_LinearSpace1ToSRGB1(dest);
	dest                    = _mm_mul_ps(dest, _mm_set_ps1(255.0f)); // to 0->255 range

	SIMDDebug_ColorInRange(dest, 0.0f, 255.0f);

	f32 destR = ((f32 *)&dest)[0];
	f32 destG = ((f32 *)&dest)[1];
	f32 destB = ((f32 *)&dest)[2];

	u32 pixel = // ((u32)(destA) << 24 |
	             (u32)(destR) << 16 |
	             (u32)(destG) << 8 |
	             (u32)(destB) << 0;
	bitmapPtr[x + (y * pitchInU32)] = pixel;

	DTRDebug_CounterIncrement(DTRDebugCounter_SetPixels);
}

// colorModulate: _mm_set_ps(a, b, g, r)     ie. 0=r, 1=g, 2=b, 3=a
// barycentric:   _mm_set_ps(xx, p3, p2, p1) ie. 0=p1, 1=p2, 2=p3, 3=a
FILE_SCOPE __m128 SIMD_SampleTextureForTriangle(DTRBitmap *const texture, const DqnV2 uv1,
                                                const DqnV2 uv2SubUv1, const DqnV2 uv3SubUv1,
                                                const __m128 barycentric)
{
	DTRDebug_BeginCycleCount("SIMD_TexturedTriangle_SampleTexture",
	                         DTRDebugCycleCount_SIMD_TexturedTriangle_SampleTexture);

	LOCAL_PERSIST const __m128 INV255_4X = _mm_set_ps1(1.0f / 255.0f);

	const f32 barycentricP2 = ((f32 *)&barycentric)[1];
	const f32 barycentricP3 = ((f32 *)&barycentric)[2];
	DqnV2 uv                = uv1 + (uv2SubUv1 * barycentricP2) + (uv3SubUv1 * barycentricP3);

	const f32 EPSILON = 0.1f;
	DQN_ASSERT(uv.x >= 0 && uv.x < 1.0f + EPSILON);
	DQN_ASSERT(uv.y >= 0 && uv.y < 1.0f + EPSILON);
	uv.x = DqnMath_Clampf(uv.x, 0.0f, 1.0f);
	uv.y = DqnMath_Clampf(uv.y, 0.0f, 1.0f);

	f32 texelXf = uv.x * texture->dim.w;
	f32 texelYf = uv.y * texture->dim.h;
	DQN_ASSERT(texelXf >= 0 && texelXf < texture->dim.w);
	DQN_ASSERT(texelYf >= 0 && texelYf < texture->dim.h);

	i32 texelX = (i32)texelXf;
	i32 texelY = (i32)texelYf;

	const u32 texturePitch     = texture->bytesPerPixel * texture->dim.w;
	const u8 *const texturePtr = texture->memory;
	u32 texel1 = *(u32 *)(texturePtr + (texelX * texture->bytesPerPixel) + (texelY * texturePitch));

	__m128 color = _mm_set_ps((f32)(texel1 >> 24),
	                          (f32)((texel1 >> 16) & 0xFF),
	                          (f32)((texel1 >> 8) & 0xFF),
	                          (f32)((texel1 >> 0) & 0xFF));

	color = SIMD_SRGB255ToLinearSpace1(color);
	DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMD_TexturedTriangle_SampleTexture);
	return color;
}

FILE_SCOPE void SIMD_TexturedTriangle(DTRRenderBuffer *const renderBuffer, DqnV3 p1, DqnV3 p2,
                                      DqnV3 p3, DqnV2 uv1, DqnV2 uv2, DqnV2 uv3,
                                      DTRBitmap *const texture, DqnV4 color,
                                      const DTRRenderTransform transform)
{
	DTR_DEBUG_EP_TIMED_FUNCTION();
	DTRDebug_BeginCycleCount("SIMD_TexturedTriangle", DTRDebugCycleCount_SIMD_TexturedTriangle);
	////////////////////////////////////////////////////////////////////////////
	// Convert color
	////////////////////////////////////////////////////////////////////////////
	__m128 simdColor = _mm_set_ps(color.a, color.b, color.g, color.r);
	simdColor        = SIMD_SRGB1ToLinearSpace(simdColor);
	simdColor        = SIMD_PreMultiplyAlpha1(simdColor);

	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes p1, p2, p3 inplace
	////////////////////////////////////////////////////////////////////////////
	{
		Make3PointsClockwise(&p1, &p2, &p3);

		// TODO(doyle): Transform is only in 2d right now
		DqnV2 origin   = Get2DOriginFromTransformAnchor(p1.xy, p2.xy, p3.xy, transform);
		DqnV2 pList[3] = {p1.xy - origin, p2.xy - origin, p3.xy - origin};
		TransformPoints(origin, pList, DQN_ARRAY_COUNT(pList), transform.scale, transform.rotation);

		p1.xy = pList[0];
		p2.xy = pList[1];
		p3.xy = pList[2];
	}

	DqnV2i max = DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x),
	                       DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
	DqnV2i min = DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x),
	                       DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
	min.x = DQN_MAX(min.x, 0);
	min.y = DQN_MAX(min.y, 0);
	max.x = DQN_MIN(max.x, renderBuffer->width - 1);
	max.y = DQN_MIN(max.y, renderBuffer->height - 1);


	////////////////////////////////////////////////////////////////////////////
	// Setup SIMD data
	////////////////////////////////////////////////////////////////////////////
	const u32 NUM_X_PIXELS_TO_SIMD = 2;
	const u32 NUM_Y_PIXELS_TO_SIMD = 1;

	const __m128 INV255_4X    = _mm_set_ps1(1.0f / 255.0f);
	const __m128 ZERO_4X      = _mm_set_ps1(0.0f);
	const u32 IS_GREATER_MASK = 0xF;

	// SignedArea: _mm_set_ps(unused, p3, p2, p1) ie 0=p1, 1=p1, 2=p3, 3=unused
	__m128 signedAreaPixel1;
	__m128 signedAreaPixel2;

	__m128 signedAreaPixelDeltaX;
	__m128 signedAreaPixelDeltaY;
	__m128 invSignedAreaParallelogram_4x;

	__m128 triangleZ = _mm_set_ps(0, p3.z, p2.z, p1.z);
	{
		DqnV2i startP         = min;
		f32 signedArea1Start  = Triangle2TimesSignedArea(p2.xy, p3.xy, DqnV2_V2i(startP));
		f32 signedArea1DeltaX = p2.y - p3.y;
		f32 signedArea1DeltaY = p3.x - p2.x;

		f32 signedArea2Start  = Triangle2TimesSignedArea(p3.xy, p1.xy, DqnV2_V2i(startP));
		f32 signedArea2DeltaX = p3.y - p1.y;
		f32 signedArea2DeltaY = p1.x - p3.x;

		f32 signedArea3Start  = Triangle2TimesSignedArea(p1.xy, p2.xy, DqnV2_V2i(startP));
		f32 signedArea3DeltaX = p1.y - p2.y;
		f32 signedArea3DeltaY = p2.x - p1.x;

		f32 signedAreaParallelogram = signedArea1Start + signedArea2Start + signedArea3Start;
		if (signedAreaParallelogram == 0) return;

		f32 invSignedAreaParallelogram = 1.0f / signedAreaParallelogram;
		invSignedAreaParallelogram_4x  = _mm_set_ps1(invSignedAreaParallelogram);

		// NOTE: Order is important here!
		signedAreaPixelDeltaX = _mm_set_ps(0, signedArea3DeltaX, signedArea2DeltaX, signedArea1DeltaX);
		signedAreaPixelDeltaY = _mm_set_ps(0, signedArea3DeltaY, signedArea2DeltaY, signedArea1DeltaY);

		signedAreaPixel1 = _mm_set_ps(0, signedArea3Start, signedArea2Start, signedArea1Start);
		signedAreaPixel2 = _mm_add_ps(signedAreaPixel1, signedAreaPixelDeltaX);

		// NOTE: Increase step size to the number of pixels rasterised with SIMD
		{
			const __m128 STEP_X_4X = _mm_set_ps1((f32)NUM_X_PIXELS_TO_SIMD);
			const __m128 STEP_Y_4X = _mm_set_ps1((f32)NUM_Y_PIXELS_TO_SIMD);

			signedAreaPixelDeltaX = _mm_mul_ps(signedAreaPixelDeltaX, STEP_X_4X);
			signedAreaPixelDeltaY = _mm_mul_ps(signedAreaPixelDeltaY, STEP_Y_4X);
		}

	}

	const DqnV2 uv2SubUv1      = uv2 - uv1;
	const DqnV2 uv3SubUv1      = uv3 - uv1;
	const u32 texturePitch     = texture->bytesPerPixel * texture->dim.w;
	const u8 *const texturePtr = texture->memory;
	const u32 zBufferPitch     = renderBuffer->width;

	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	DTRDebug_BeginCycleCount("SIMD_TexturedTriangle_Rasterise", DTRDebugCycleCount_SIMD_TexturedTriangle_Rasterise);
	for (i32 bufferY = min.y; bufferY < max.y; bufferY += NUM_Y_PIXELS_TO_SIMD)
	{
		__m128 signedArea1 = signedAreaPixel1;
		__m128 signedArea2 = signedAreaPixel2;

		for (i32 bufferX = min.x; bufferX < max.x; bufferX += NUM_X_PIXELS_TO_SIMD)
		{

			DTRDebug_BeginCycleCount("SIMD_TexturedTriangle_RasterisePixel",
			                         DTRDebugCycleCount_SIMD_TexturedTriangle_RasterisePixel);
			// Rasterise buffer(X, Y) pixel
			{
				__m128 checkArea    = signedArea1;
				__m128 isGreater    = _mm_cmpge_ps(checkArea, ZERO_4X);
				i32 isGreaterResult = _mm_movemask_ps(isGreater);
				i32 posX            = bufferX;
				i32 posY            = bufferY;

				if ((isGreaterResult & IS_GREATER_MASK) == IS_GREATER_MASK)
				{
					__m128 barycentric  = _mm_mul_ps(checkArea, invSignedAreaParallelogram_4x);
					__m128 barycentricZ = _mm_mul_ps(triangleZ, barycentric);

					i32 zBufferIndex = posX + (posY * zBufferPitch);
					f32 pixelZValue  = ((f32 *)&barycentricZ)[0] +
					                   ((f32 *)&barycentricZ)[1] +
					                   ((f32 *)&barycentricZ)[2];
					f32 currZValue = renderBuffer->zBuffer[zBufferIndex];
					if (pixelZValue > currZValue)
					{
						renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
						__m128 texSampledColor = SIMD_SampleTextureForTriangle(texture, uv1, uv2SubUv1, uv3SubUv1, barycentric);
						__m128 finalColor      = _mm_mul_ps(texSampledColor, simdColor);
						SIMD_SetPixel(renderBuffer, posX, posY, finalColor, ColorSpace_Linear);
					}
				}
				signedArea1 = _mm_add_ps(signedArea1, signedAreaPixelDeltaX);
			}
			DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMD_TexturedTriangle_RasterisePixel);

			// Rasterise buffer(X + 1, Y) pixel
			{
				__m128 checkArea    = signedArea2;
				__m128 isGreater    = _mm_cmpge_ps(checkArea, ZERO_4X);
				i32 isGreaterResult = _mm_movemask_ps(isGreater);
				i32 posX            = bufferX + 1;
				i32 posY            = bufferY;
				if ((isGreaterResult & IS_GREATER_MASK) == IS_GREATER_MASK && posX < max.x)
				{
					__m128 barycentric  = _mm_mul_ps(checkArea, invSignedAreaParallelogram_4x);
					__m128 barycentricZ = _mm_mul_ps(triangleZ, barycentric);

					i32 zBufferIndex = posX + (posY * zBufferPitch);
					f32 pixelZValue  = ((f32 *)&barycentricZ)[0] +
					                    ((f32 *)&barycentricZ)[1] +
					                    ((f32 *)&barycentricZ)[2];
					f32 currZValue = renderBuffer->zBuffer[zBufferIndex];
					if (pixelZValue > currZValue)
					{
						renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
						__m128 texSampledColor = SIMD_SampleTextureForTriangle(texture, uv1, uv2SubUv1, uv3SubUv1, barycentric);
						__m128 finalColor      = _mm_mul_ps(texSampledColor, simdColor);
						SIMD_SetPixel(renderBuffer, posX, posY, finalColor, ColorSpace_Linear);
					}
				}
				signedArea2 = _mm_add_ps(signedArea2, signedAreaPixelDeltaX);
			}

		}

		signedAreaPixel1 = _mm_add_ps(signedAreaPixel1, signedAreaPixelDeltaY);
		signedAreaPixel2 = _mm_add_ps(signedAreaPixel2, signedAreaPixelDeltaY);
	}
	DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMD_TexturedTriangle_Rasterise);

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	DTRDebug_CounterIncrement(DTRDebugCounter_RenderTriangle);
	if (DTR_DEBUG_RENDER)
	{
		DqnV2 origin = Get2DOriginFromTransformAnchor(p1.xy, p2.xy, p3.xy, transform);
		// Draw Bounding box
		if (0)
		{
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, min.y), DqnV2i_2i(min.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, max.y), DqnV2i_2i(max.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, max.y), DqnV2i_2i(max.x, min.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, min.y), DqnV2i_2i(min.x, min.y), color);
		}

		// Draw Triangle Coordinate Basis
		if (0)
		{
			DqnV2 xAxis = DqnV2_2f(cosf(transform.rotation), sinf(transform.rotation)) * transform.scale.x;
			DqnV2 yAxis         = DqnV2_2f(-xAxis.y, xAxis.x) * transform.scale.y;
			DqnV4 coordSysColor = DqnV4_4f(0, 1, 1, 1);
			i32 axisLen = 50;
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(xAxis * axisLen), coordSysColor);
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(yAxis * axisLen), coordSysColor);
		}

		// Draw axis point
		if (0)
		{
			DqnV4 green  = DqnV4_4f(0, 1, 0, 1);
			DqnV4 blue   = DqnV4_4f(0, 0, 1, 1);
			DqnV4 purple = DqnV4_4f(1, 0, 1, 1);

			DTRRender_Rectangle(renderBuffer, p1.xy - DqnV2_1f(5), p1.xy + DqnV2_1f(5), green);
			DTRRender_Rectangle(renderBuffer, p2.xy - DqnV2_1f(5), p2.xy + DqnV2_1f(5), blue);
			DTRRender_Rectangle(renderBuffer, p3.xy - DqnV2_1f(5), p3.xy + DqnV2_1f(5), purple);
		}
	}
	DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMD_TexturedTriangle);
}


void DTRRender_TexturedTriangle(DTRRenderBuffer *const renderBuffer, DqnV3 p1, DqnV3 p2, DqnV3 p3,
                                DqnV2 uv1, DqnV2 uv2, DqnV2 uv3, DTRBitmap *const texture,
                                DqnV4 color, const DTRRenderTransform transform)
{
	if (globalDTRPlatformFlags.canUseSSE2)
	{
		SIMD_TexturedTriangle(renderBuffer, p1, p2, p3, uv1, uv2, uv3, texture, color, transform);
		return;
	}

	DTR_DEBUG_EP_TIMED_FUNCTION();
	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes
	////////////////////////////////////////////////////////////////////////////
	DqnV3 p1p2 = p2 - p1;
	DqnV3 p1p3 = p3 - p1;

	// TODO(doyle): Transform is only in 2d right now
	DqnV2 p1p2Anchored = p1p2.xy * transform.anchor;
	DqnV2 p1p3Anchored = p1p3.xy * transform.anchor;
	DqnV2 origin       = p1.xy + p1p2Anchored + p1p3Anchored;
	DqnV2 pList[3]     = {p1.xy - origin, p2.xy - origin, p3.xy - origin};
	TransformPoints(origin, pList, DQN_ARRAY_COUNT(pList), transform.scale, transform.rotation);
	p1.xy = pList[0];
	p2.xy = pList[1];
	p3.xy = pList[2];

	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);

	f32 area2Times = ((p2.x - p1.x) * (p2.y + p1.y)) +
	                 ((p3.x - p2.x) * (p3.y + p2.y)) +
	                 ((p1.x - p3.x) * (p1.y + p3.y));
	if (area2Times > 0)
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV3, p2, p3);
	}

	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	RasteriseTexturedTriangle(renderBuffer, p1, p2, p3, uv1, uv2, uv3, texture, color);

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	DTRDebug_CounterIncrement(DTRDebugCounter_RenderTriangle);
	if (DTR_DEBUG_RENDER)
	{
		DqnV2i max =
		    DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x), DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
		DqnV2i min =
		    DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x), DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
		min.x = DQN_MAX(min.x, 0);
		min.y = DQN_MAX(min.y, 0);
		max.x = DQN_MIN(max.x, renderBuffer->width - 1);
		max.y = DQN_MIN(max.y, renderBuffer->height - 1);

		// Draw Bounding box
		if (0)
		{
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, min.y), DqnV2i_2i(min.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, max.y), DqnV2i_2i(max.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, max.y), DqnV2i_2i(max.x, min.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, min.y), DqnV2i_2i(min.x, min.y), color);
		}

		// Draw Triangle Coordinate Basis
		if (0)
		{
			DqnV2 xAxis = DqnV2_2f(cosf(transform.rotation), sinf(transform.rotation)) * transform.scale.x;
			DqnV2 yAxis         = DqnV2_2f(-xAxis.y, xAxis.x) * transform.scale.y;
			DqnV4 coordSysColor = DqnV4_4f(0, 1, 1, 1);
			i32 axisLen = 50;
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(xAxis * axisLen), coordSysColor);
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(yAxis * axisLen), coordSysColor);
		}

		// Draw axis point
		if (0)
		{
			DqnV4 green  = DqnV4_4f(0, 1, 0, 1);
			DqnV4 blue   = DqnV4_4f(0, 0, 1, 1);
			DqnV4 purple = DqnV4_4f(1, 0, 1, 1);

			DTRRender_Rectangle(renderBuffer, p1.xy - DqnV2_1f(5), p1.xy + DqnV2_1f(5), green);
			DTRRender_Rectangle(renderBuffer, p2.xy - DqnV2_1f(5), p2.xy + DqnV2_1f(5), blue);
			DTRRender_Rectangle(renderBuffer, p3.xy - DqnV2_1f(5), p3.xy + DqnV2_1f(5), purple);
		}
	}
}

void DTRRender_Mesh(DTRRenderBuffer *const renderBuffer, DTRMesh *const mesh, const DqnV3 pos,
                    const f32 scale, const DqnV3 lightVector)
{
	if (!mesh) return;

	for (u32 i = 0; i < mesh->numFaces; i++)
	{
		DTRMeshFace face = mesh->faces[i];
		DQN_ASSERT(face.numVertexIndex == 3);
		i32 vertAIndex = face.vertexIndex[0];
		i32 vertBIndex = face.vertexIndex[1];
		i32 vertCIndex = face.vertexIndex[2];

		DqnV4 vertA = mesh->vertexes[vertAIndex];
		DqnV4 vertB = mesh->vertexes[vertBIndex];
		DqnV4 vertC = mesh->vertexes[vertCIndex];
		// TODO(doyle): Some models have -ve indexes to refer to relative
		// vertices. We should resolve that to positive indexes at run time.
		DQN_ASSERT(vertAIndex < (i32)mesh->numVertexes);
		DQN_ASSERT(vertBIndex < (i32)mesh->numVertexes);
		DQN_ASSERT(vertCIndex < (i32)mesh->numVertexes);

		DqnV4 vertAB = vertB - vertA;
		DqnV4 vertAC = vertC - vertA;
		DqnV3 normal = DqnV3_Cross(vertAC.xyz, vertAB.xyz);

		f32 intensity = DqnV3_Dot(DqnV3_Normalise(normal), lightVector);
		if (intensity < 0) continue;
		DqnV4 modelCol = DqnV4_4f(1, 1, 1, 1);
		modelCol.rgb *= DQN_ABS(intensity);

		DqnV3 screenVA = (vertA.xyz * scale) + pos;
		DqnV3 screenVB = (vertB.xyz * scale) + pos;
		DqnV3 screenVC = (vertC.xyz * scale) + pos;

		// TODO(doyle): Why do we need rounding here? Maybe it's because
		// I don't do any interpolation in the triangle routine for jagged
		// edges.
#if 1
		screenVA.x = (f32)(i32)(screenVA.x + 0.5f);
		screenVA.y = (f32)(i32)(screenVA.y + 0.5f);
		screenVB.x = (f32)(i32)(screenVB.x + 0.5f);
		screenVB.y = (f32)(i32)(screenVB.y + 0.5f);
		screenVC.x = (f32)(i32)(screenVC.x + 0.5f);
		screenVC.y = (f32)(i32)(screenVC.y + 0.5f);
#endif

		i32 textureAIndex = face.texIndex[0];
		i32 textureBIndex = face.texIndex[1];
		i32 textureCIndex = face.texIndex[2];

		DqnV2 texA = mesh->texUV[textureAIndex].xy;
		DqnV2 texB = mesh->texUV[textureBIndex].xy;
		DqnV2 texC = mesh->texUV[textureCIndex].xy;
		DQN_ASSERT(textureAIndex < (i32)mesh->numTexUV);
		DQN_ASSERT(textureBIndex < (i32)mesh->numTexUV);
		DQN_ASSERT(textureCIndex < (i32)mesh->numTexUV);

		bool DEBUG_SIMPLE_MODE = false;
		if (DTR_DEBUG && DEBUG_SIMPLE_MODE)
		{
			DTRRender_Triangle(renderBuffer, screenVA, screenVB, screenVC, modelCol);
		}
		else
		{
			DTRRender_TexturedTriangle(renderBuffer, screenVA, screenVB, screenVC, texA, texB,
			                           texC, &mesh->tex, modelCol);
		}

		bool DEBUG_WIREFRAME = false;
		if (DTR_DEBUG && DEBUG_WIREFRAME)
		{
			DqnV4 wireColor = DqnV4_4f(1.0f, 1.0f, 1.0f, 0.01f);
			DTRRender_Line(renderBuffer, DqnV2i_V2(screenVA.xy), DqnV2i_V2(screenVB.xy),
			               wireColor);
			DTRRender_Line(renderBuffer, DqnV2i_V2(screenVB.xy), DqnV2i_V2(screenVC.xy),
			               wireColor);
			DTRRender_Line(renderBuffer, DqnV2i_V2(screenVC.xy), DqnV2i_V2(screenVA.xy),
			               wireColor);
		}
	}
}

FILE_SCOPE inline void SIMDRasteriseTriangle(DTRRenderBuffer *const renderBuffer,
                                             const SIMDTriangleInclusionTest simdTri,
                                             const i32 posX, const i32 posY, DqnV4 color,
                                             __m128 *const signedArea)
{
	__m128 ZERO_4X         = _mm_set_ps1(0.0f);
	u32 IS_GREATER_MASK    = 0xF;
	const u32 zBufferPitch = renderBuffer->width;

	__m128 isGreater    = _mm_cmpge_ps(*signedArea, ZERO_4X);
	i32 isGreaterResult = _mm_movemask_ps(isGreater);
	if ((isGreaterResult & IS_GREATER_MASK) == IS_GREATER_MASK)
	{
		__m128 barycentric  = _mm_mul_ps(*signedArea, simdTri.invSignedAreaParallelogram_4x);
		__m128 barycentricZ = _mm_mul_ps(simdTri.vertexZValues, barycentric);

		i32 zBufferIndex = posX + (posY * zBufferPitch);
		f32 pixelZValue =
		    ((f32 *)&barycentricZ)[0] + ((f32 *)&barycentricZ)[1] + ((f32 *)&barycentricZ)[2];
		f32 currZValue = renderBuffer->zBuffer[zBufferIndex];
		if (pixelZValue > currZValue)
		{
			renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
#if 1
			// NOTE: Supersampling
			const i32 NUM_SUB_PIXEL        = 8;
			const f32 STEP_SIZE            = 1.0f / NUM_SUB_PIXEL;
			const f32 COVERAGE_GRANULARITY = 1.0f / (f32)DQN_SQUARED(NUM_SUB_PIXEL);

			f32 coverage = 0;
			const __m128 STEP_SIZE_4X = _mm_set_ps1(STEP_SIZE);
			const __m128 SUB_PIXEL_DELTA_X = _mm_mul_ps(simdTri.signedAreaPixelDeltaX, STEP_SIZE_4X);
			const __m128 SUB_PIXEL_DELTA_Y = _mm_mul_ps(simdTri.signedAreaPixelDeltaY, STEP_SIZE_4X);

			DqnV2 sample = DqnV2_2f(posX + (0.5f / NUM_SUB_PIXEL),
			                        posY + (0.5f / NUM_SUB_PIXEL));
			f32 resultP1 = Triangle2TimesSignedArea(simdTri.p2.xy, simdTri.p3.xy, sample);
			f32 resultP2 = Triangle2TimesSignedArea(simdTri.p3.xy, simdTri.p1.xy, sample);
			f32 resultP3 = Triangle2TimesSignedArea(simdTri.p1.xy, simdTri.p2.xy, sample);

			__m128 startSubPixel = _mm_set_ps(0, resultP3, resultP2, resultP1);
			__m128 checkPixel    = startSubPixel;
			for (i32 subY = 0; subY < NUM_SUB_PIXEL; subY++)
			{
				checkPixel = startSubPixel;
				for (i32 subX = 0; subX < NUM_SUB_PIXEL; subX++)
				{
					resultP1 = ((f32 *)&checkPixel)[0];
					resultP2 = ((f32 *)&checkPixel)[1];
					resultP3 = ((f32 *)&checkPixel)[2];
					if ((resultP1 >= 0 && resultP2 >= 0 && resultP3 >= 0) ||
					    (resultP1 < 0 && resultP2 < 0 && resultP3 < 0))
					{
						coverage += COVERAGE_GRANULARITY;
					}

					checkPixel = _mm_add_ps(checkPixel, SUB_PIXEL_DELTA_X);
				}

				startSubPixel = _mm_add_ps(startSubPixel, SUB_PIXEL_DELTA_Y);
			}
			if (coverage > 0)
			{
				color *= coverage;
				SetPixel(renderBuffer, posX, posY, color, ColorSpace_Linear);
			}
#else
			SetPixel(renderBuffer, posX, posY, color, ColorSpace_Linear);
#endif
		}
	}
	*signedArea = _mm_add_ps(*signedArea, simdTri.signedAreaPixelDeltaX);
}

void DTRRender_Triangle(DTRRenderBuffer *const renderBuffer, DqnV3 p1, DqnV3 p2, DqnV3 p3,
                        DqnV4 color, const DTRRenderTransform transform)
{
	DTR_DEBUG_EP_TIMED_FUNCTION();

	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes
	////////////////////////////////////////////////////////////////////////////
	DqnV3 p1p2 = p2 - p1;
	DqnV3 p1p3 = p3 - p1;

	// TODO(doyle): Transform is only in 2d right now
	DqnV2 p1p2Anchored = p1p2.xy * transform.anchor;
	DqnV2 p1p3Anchored = p1p3.xy * transform.anchor;
	DqnV2 origin       = p1.xy + p1p2Anchored + p1p3Anchored;
	DqnV2 pList[3]     = {p1.xy - origin, p2.xy - origin, p3.xy - origin};
	TransformPoints(origin, pList, DQN_ARRAY_COUNT(pList), transform.scale, transform.rotation);
	p1.xy = pList[0];
	p2.xy = pList[1];
	p3.xy = pList[2];

	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);


	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	const u32 zBufferPitch = renderBuffer->width;
	if (globalDTRPlatformFlags.canUseSSE2)
	{
		TriangleInclusionTest inclusionTest = CreateTriangleInclusionTest(
		    renderBuffer->width - 1, renderBuffer->height - 1, p1, p2, p3);
		if (inclusionTest.invSignedAreaParallelogram == 0) return;

		SIMDTriangleInclusionTest simdTri =
		    CreateSimdTriangleInclusionTest(p1, p2, p3, inclusionTest);

		__m128 INV255_4X    = _mm_set_ps1(1.0f / 255.0f);
		__m128 ZERO_4X      = _mm_set_ps1(0.0f);
		u32 IS_GREATER_MASK = 0xF;

		__m128 signedAreaPixel1 = simdTri.startPixel;
		__m128 signedAreaPixel2 = _mm_add_ps(signedAreaPixel1, simdTri.signedAreaPixelDeltaX);
		__m128 signedAreaPixel3 = _mm_add_ps(signedAreaPixel2, simdTri.signedAreaPixelDeltaX);
		__m128 signedAreaPixel4 = _mm_add_ps(signedAreaPixel3, simdTri.signedAreaPixelDeltaX);

		// NOTE: Increase step size to the number of pixels rasterised with SIMD
		const u32 NUM_X_PIXELS_TO_SIMD = 2;
		const u32 NUM_Y_PIXELS_TO_SIMD = 1;
		const __m128 STEP_X_4X         = _mm_set_ps1((f32)NUM_X_PIXELS_TO_SIMD);
		const __m128 STEP_Y_4X         = _mm_set_ps1((f32)NUM_Y_PIXELS_TO_SIMD);

		simdTri.signedAreaPixelDeltaX = _mm_mul_ps(simdTri.signedAreaPixelDeltaX, STEP_X_4X);
		simdTri.signedAreaPixelDeltaY = _mm_mul_ps(simdTri.signedAreaPixelDeltaY, STEP_Y_4X);

		const DqnV2i min = inclusionTest.boundsMin;
		const DqnV2i max = inclusionTest.boundsMax;
		for (i32 bufferY = min.y; bufferY < max.y; bufferY += NUM_Y_PIXELS_TO_SIMD)
		{
			__m128 signedArea1 = signedAreaPixel1;
			__m128 signedArea2 = signedAreaPixel2;

			for (i32 bufferX = min.x; bufferX < max.x; bufferX += NUM_X_PIXELS_TO_SIMD)
			{
				SIMDRasteriseTriangle(renderBuffer, simdTri, bufferX, bufferY, color, &signedArea1);

				if (bufferX + 1 < max.x)
				{
					SIMDRasteriseTriangle(renderBuffer, simdTri, bufferX + 1, bufferY, color,
					                      &signedArea2);
				}
			}

			signedAreaPixel1 = _mm_add_ps(signedAreaPixel1, simdTri.signedAreaPixelDeltaY);
			signedAreaPixel2 = _mm_add_ps(signedAreaPixel2, simdTri.signedAreaPixelDeltaY);
		}
	}
	else
	{
		f32 area2Times = ((p2.x - p1.x) * (p2.y + p1.y)) + ((p3.x - p2.x) * (p3.y + p2.y)) +
		                 ((p1.x - p3.x) * (p1.y + p3.y));
		if (area2Times > 0)
		{
			// Clockwise swap any point to make it clockwise
			DQN_SWAP(DqnV3, p2, p3);
		}

		DqnV2i max =
		    DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x), DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
		DqnV2i min =
		    DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x), DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
		min.x = DQN_MAX(min.x, 0);
		min.y = DQN_MAX(min.y, 0);
		max.x = DQN_MIN(max.x, renderBuffer->width - 1);
		max.y = DQN_MIN(max.y, renderBuffer->height - 1);

		const DqnV3 a = p1;
		const DqnV3 b = p2;
		const DqnV3 c = p3;

		DqnV2i startP         = min;
		f32 signedArea1       = ((b.x - a.x) * (startP.y - a.y)) - ((b.y - a.y) * (startP.x - a.x));
		f32 signedArea1DeltaX = a.y - b.y;
		f32 signedArea1DeltaY = b.x - a.x;

		f32 signedArea2       = ((c.x - b.x) * (startP.y - b.y)) - ((c.y - b.y) * (startP.x - b.x));
		f32 signedArea2DeltaX = b.y - c.y;
		f32 signedArea2DeltaY = c.x - b.x;

		f32 signedArea3       = ((a.x - c.x) * (startP.y - c.y)) - ((a.y - c.y) * (startP.x - c.x));
		f32 signedArea3DeltaX = c.y - a.y;
		f32 signedArea3DeltaY = a.x - c.x;

		f32 signedAreaParallelogram = ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x));
		if (signedAreaParallelogram == 0) return;
		f32 invSignedAreaParallelogram = 1 / signedAreaParallelogram;

		for (i32 bufferY = min.y; bufferY < max.y; bufferY++)
		{
			f32 signedArea1Row = signedArea1;
			f32 signedArea2Row = signedArea2;
			f32 signedArea3Row = signedArea3;

			for (i32 bufferX = min.x; bufferX < max.x; bufferX++)
			{
				if (signedArea1Row >= 0 && signedArea2Row >= 0 && signedArea3Row >= 0)
				{
					f32 barycentricB = signedArea3Row * invSignedAreaParallelogram;
					f32 barycentricC = signedArea1Row * invSignedAreaParallelogram;

					i32 zBufferIndex = bufferX + (bufferY * zBufferPitch);
					f32 pixelZValue =
					    a.z + (barycentricB * (b.z - a.z)) + (barycentricC * (c.z - a.z));
					f32 currZValue = renderBuffer->zBuffer[zBufferIndex];
					DQN_ASSERT(zBufferIndex < (renderBuffer->width * renderBuffer->height));
					if (pixelZValue > currZValue)
					{
						renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
						SetPixel(renderBuffer, bufferX, bufferY, color, ColorSpace_Linear);
					}
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

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	DTRDebug_CounterIncrement(DTRDebugCounter_RenderTriangle);
	if (DTR_DEBUG_RENDER)
	{
		DqnV2i max =
		    DqnV2i_2f(DQN_MAX(DQN_MAX(p1.x, p2.x), p3.x), DQN_MAX(DQN_MAX(p1.y, p2.y), p3.y));
		DqnV2i min =
		    DqnV2i_2f(DQN_MIN(DQN_MIN(p1.x, p2.x), p3.x), DQN_MIN(DQN_MIN(p1.y, p2.y), p3.y));
		min.x = DQN_MAX(min.x, 0);
		min.y = DQN_MAX(min.y, 0);
		max.x = DQN_MIN(max.x, renderBuffer->width - 1);
		max.y = DQN_MIN(max.y, renderBuffer->height - 1);

		// Draw Bounding box
		{
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, min.y), DqnV2i_2i(min.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(min.x, max.y), DqnV2i_2i(max.x, max.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, max.y), DqnV2i_2i(max.x, min.y), color);
			DTRRender_Line(renderBuffer, DqnV2i_2i(max.x, min.y), DqnV2i_2i(min.x, min.y), color);
		}

		// Draw Triangle Coordinate Basis
		{
			DqnV2 xAxis = DqnV2_2f(cosf(transform.rotation), sinf(transform.rotation)) * transform.scale.x;
			DqnV2 yAxis         = DqnV2_2f(-xAxis.y, xAxis.x) * transform.scale.y;
			DqnV4 coordSysColor = DqnV4_4f(0, 1, 1, 1);
			i32 axisLen = 50;
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(xAxis * axisLen), coordSysColor);
			DTRRender_Line(renderBuffer, DqnV2i_V2(origin), DqnV2i_V2(origin) + DqnV2i_V2(yAxis * axisLen), coordSysColor);
		}

		// Draw axis point
		{
			DqnV4 green  = DqnV4_4f(0, 1, 0, 1);
			DqnV4 blue   = DqnV4_4f(0, 0, 1, 1);
			DqnV4 purple = DqnV4_4f(1, 0, 1, 1);

			DTRRender_Rectangle(renderBuffer, p1.xy - DqnV2_1f(5), p1.xy + DqnV2_1f(5), green);
			DTRRender_Rectangle(renderBuffer, p2.xy - DqnV2_1f(5), p2.xy + DqnV2_1f(5), blue);
			DTRRender_Rectangle(renderBuffer, p3.xy - DqnV2_1f(5), p3.xy + DqnV2_1f(5), purple);
		}
	}
}

void DTRRender_Bitmap(DTRRenderBuffer *const renderBuffer, DTRBitmap *const bitmap, DqnV2 pos,
                      const DTRRenderTransform transform, DqnV4 color)
{
	if (!bitmap || !bitmap->memory || !renderBuffer) return;
	DTR_DEBUG_EP_TIMED_FUNCTION();

	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes
	////////////////////////////////////////////////////////////////////////////
	DqnV2 min = pos;
	DqnV2 max = min + DqnV2_V2i(bitmap->dim);

	RectPoints rectPoints     = TransformRectPoints(min, max, transform);
	const DqnV2 *const pList  = &rectPoints.pList[0];
	const i32 RECT_PLIST_SIZE = DQN_ARRAY_COUNT(rectPoints.pList);

	DqnRect bounds = GetBoundingBox(pList, RECT_PLIST_SIZE);
	min            = bounds.min;
	max            = bounds.max;

	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);
	DQN_ASSERT(color.a >= 0 && color.a <= 1.0f);
	DQN_ASSERT(color.r >= 0 && color.r <= 1.0f);
	DQN_ASSERT(color.g >= 0 && color.g <= 1.0f);
	DQN_ASSERT(color.b >= 0 && color.b <= 1.0f);

	////////////////////////////////////////////////////////////////////////////
	// Clip drawing space
	////////////////////////////////////////////////////////////////////////////
	DqnRect drawRect = DqnRect_4f(bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y);
	DqnRect clip     = DqnRect_4i(0, 0, renderBuffer->width, renderBuffer->height);

	DqnRect clippedDrawRect = DqnRect_ClipRect(drawRect, clip);
	DqnV2 clippedSize       = DqnRect_GetSizeV2(clippedDrawRect);
	////////////////////////////////////////////////////////////////////////////
	// Setup Texture Mapping
	////////////////////////////////////////////////////////////////////////////
	const i32 pitch      = bitmap->dim.w * bitmap->bytesPerPixel;
	u8 *const bitmapPtr  = (u8 *)bitmap->memory;

	const DqnV2 rectBasis       = pList[RectPointsIndex_Basis];
	const DqnV2 xAxisRelToBasis = pList[RectPointsIndex_XAxis] - rectBasis;
	const DqnV2 yAxisRelToBasis = pList[RectPointsIndex_YAxis] - rectBasis;

	const f32 invXAxisLenSq = 1 / DqnV2_LengthSquared(DqnV2_1f(0), xAxisRelToBasis);
	const f32 invYAxisLenSq = 1 / DqnV2_LengthSquared(DqnV2_1f(0), yAxisRelToBasis);
	for (i32 y = 0; y < (i32)clippedSize.h; y++)
	{
		const i32 bufferY = (i32)clippedDrawRect.min.y + y;
		for (i32 x = 0; x < (i32)clippedSize.w; x++)
		{
			const i32 bufferX = (i32)clippedDrawRect.min.x + x;

			bool bufXYIsInside = true;
			for (i32 pIndex = 0; pIndex < RECT_PLIST_SIZE; pIndex++)
			{
				DqnV2 origin = pList[pIndex];
				DqnV2 axis   = pList[(pIndex + 1) % RECT_PLIST_SIZE] - origin;
				DqnV2 testP  = DqnV2_2i(bufferX, bufferY)            - origin;

				f32 dot = DqnV2_Dot(testP, axis);
				if (dot < 0)
				{
					bufXYIsInside = false;
					break;
				}
			}

			if (bufXYIsInside)
			{
				DTR_DEBUG_EP_TIMED_BLOCK("DTRRender_Bitmap TexelCalculation");
				DqnV2 bufPRelToBasis = DqnV2_2i(bufferX, bufferY) - rectBasis;

				f32 u = DqnV2_Dot(bufPRelToBasis, xAxisRelToBasis) * invXAxisLenSq;
				f32 v = DqnV2_Dot(bufPRelToBasis, yAxisRelToBasis) * invYAxisLenSq;
				u     = DqnMath_Clampf(u, 0.0f, 1.0f);
				v     = DqnMath_Clampf(v, 0.0f, 1.0f);

				f32 texelXf = u * (f32)(bitmap->dim.w - 1);
				f32 texelYf = v * (f32)(bitmap->dim.h - 1);
				DQN_ASSERT(texelXf >= 0 && texelXf < bitmap->dim.w);
				DQN_ASSERT(texelYf >= 0 && texelYf < bitmap->dim.h);

				i32 texelX           = (i32)texelXf;
				i32 texelY           = (i32)texelYf;
				f32 texelFractionalX = texelXf - texelX;
				f32 texelFractionalY = texelYf - texelY;

				i32 texel1X = texelX;
				i32 texel1Y = texelY;

				i32 texel2X = DQN_MIN((texelX + 1), bitmap->dim.w - 1);
				i32 texel2Y = texelY;

				i32 texel3X = texelX;
				i32 texel3Y = DQN_MIN((texelY + 1), bitmap->dim.h - 1);

				i32 texel4X = DQN_MIN((texelX + 1), bitmap->dim.w - 1);
				i32 texel4Y = DQN_MIN((texelY + 1), bitmap->dim.h - 1);

				{
					DTR_DEBUG_EP_TIMED_BLOCK("DTRRender_Bitmap TexelBilinearInterpolation");
					u32 texel1  = *(u32 *)(bitmapPtr + ((texel1X * bitmap->bytesPerPixel) + (texel1Y * pitch)));
					u32 texel2  = *(u32 *)(bitmapPtr + ((texel2X * bitmap->bytesPerPixel) + (texel2Y * pitch)));
					u32 texel3  = *(u32 *)(bitmapPtr + ((texel3X * bitmap->bytesPerPixel) + (texel3Y * pitch)));
					u32 texel4  = *(u32 *)(bitmapPtr + ((texel4X * bitmap->bytesPerPixel) + (texel4Y * pitch)));

					DqnV4 color1;
					color1.a      = (f32)(texel1 >> 24);
					color1.b      = (f32)((texel1 >> 16) & 0xFF);
					color1.g      = (f32)((texel1 >> 8) & 0xFF);
					color1.r      = (f32)((texel1 >> 0) & 0xFF);

					DqnV4 color2;
					color2.a      = (f32)(texel2 >> 24);
					color2.b      = (f32)((texel2 >> 16) & 0xFF);
					color2.g      = (f32)((texel2 >> 8) & 0xFF);
					color2.r      = (f32)((texel2 >> 0) & 0xFF);

					DqnV4 color3;
					color3.a      = (f32)(texel3 >> 24);
					color3.b      = (f32)((texel3 >> 16) & 0xFF);
					color3.g      = (f32)((texel3 >> 8) & 0xFF);
					color3.r      = (f32)((texel3 >> 0) & 0xFF);

					DqnV4 color4;
					color4.a      = (f32)(texel4 >> 24);
					color4.b      = (f32)((texel4 >> 16) & 0xFF);
					color4.g      = (f32)((texel4 >> 8) & 0xFF);
					color4.r      = (f32)((texel4 >> 0) & 0xFF);

					color1 *= DTRRENDER_INV_255;
					color2 *= DTRRENDER_INV_255;
					color3 *= DTRRENDER_INV_255;
					color4 *= DTRRENDER_INV_255;

					color1 = DTRRender_SRGB1ToLinearSpaceV4(color1);
					color2 = DTRRender_SRGB1ToLinearSpaceV4(color2);
					color3 = DTRRender_SRGB1ToLinearSpaceV4(color3);
					color4 = DTRRender_SRGB1ToLinearSpaceV4(color4);

					DqnV4 color12;
					color12.a = DqnMath_Lerp(color1.a, texelFractionalX, color2.a);
					color12.b = DqnMath_Lerp(color1.b, texelFractionalX, color2.b);
					color12.g = DqnMath_Lerp(color1.g, texelFractionalX, color2.g);
					color12.r = DqnMath_Lerp(color1.r, texelFractionalX, color2.r);

					DqnV4 color34;
					color34.a = DqnMath_Lerp(color3.a, texelFractionalX, color4.a);
					color34.b = DqnMath_Lerp(color3.b, texelFractionalX, color4.b);
					color34.g = DqnMath_Lerp(color3.g, texelFractionalX, color4.g);
					color34.r = DqnMath_Lerp(color3.r, texelFractionalX, color4.r);

					DqnV4 blend;
					blend.a = DqnMath_Lerp(color12.a, texelFractionalY, color34.a);
					blend.b = DqnMath_Lerp(color12.b, texelFractionalY, color34.b);
					blend.g = DqnMath_Lerp(color12.g, texelFractionalY, color34.g);
					blend.r = DqnMath_Lerp(color12.r, texelFractionalY, color34.r);

					DQN_ASSERT(blend.a >= 0 && blend.a <= 1.0f);
					DQN_ASSERT(blend.r >= 0 && blend.r <= 1.0f);
					DQN_ASSERT(blend.g >= 0 && blend.g <= 1.0f);
					DQN_ASSERT(blend.b >= 0 && blend.b <= 1.0f);

					// TODO(doyle): Color modulation does not work!!! By supplying
					// colors [0->1] it'll reduce some of the coverage of a channel
					// and once alpha blending is applied that reduced coverage will
					// blend with the background and cause the bitmap to go
					// transparent when it shouldn't.
					blend.a *= color.a;
					blend.r *= color.r;
					blend.g *= color.g;
					blend.b *= color.b;

#if 0
					blend.a = DqnMath_Clampf(blend.a, 0.0f, 1.0f);
					blend.r = DqnMath_Clampf(blend.r, 0.0f, 1.0f);
					blend.g = DqnMath_Clampf(blend.g, 0.0f, 1.0f);
					blend.b = DqnMath_Clampf(blend.b, 0.0f, 1.0f);
#endif

					SetPixel(renderBuffer, bufferX, bufferY, blend, ColorSpace_Linear);
				}
			}
		}
	}

	if (DTR_DEBUG_RENDER)
	{
		// Draw Bounding box
		{
			DqnV4 yellow = DqnV4_4f(1, 1, 0, 1);
			DTRRender_Line(renderBuffer, DqnV2i_2f(min.x, min.y), DqnV2i_2f(min.x, max.y), yellow);
			DTRRender_Line(renderBuffer, DqnV2i_2f(min.x, max.y), DqnV2i_2f(max.x, max.y), yellow);
			DTRRender_Line(renderBuffer, DqnV2i_2f(max.x, max.y), DqnV2i_2f(max.x, min.y), yellow);
			DTRRender_Line(renderBuffer, DqnV2i_2f(max.x, min.y), DqnV2i_2f(min.x, min.y), yellow);
		}

		// Draw rotating outline
		if (transform.rotation > 0)
		{
			DqnV4 green = DqnV4_4f(0, 1, 0, 1);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[0]), DqnV2i_V2(pList[1]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[1]), DqnV2i_V2(pList[2]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[2]), DqnV2i_V2(pList[3]), green);
			DTRRender_Line(renderBuffer, DqnV2i_V2(pList[3]), DqnV2i_V2(pList[0]), green);
		}

		// Draw axis point
		{
			DqnV4 red    = DqnV4_4f(1, 0, 0, 1);
			DqnV4 green  = DqnV4_4f(0, 1, 0, 1);
			DqnV4 blue   = DqnV4_4f(0, 0, 1, 1);
			DqnV4 purple = DqnV4_4f(1, 0, 1, 1);

			DqnV2 p1 = pList[0];
			DqnV2 p2 = pList[1];
			DqnV2 p3 = pList[2];
			DqnV2 p4 = pList[3];
			DTRRender_Rectangle(renderBuffer, p1 - DqnV2_1f(5), p1 + DqnV2_1f(5), green);
			DTRRender_Rectangle(renderBuffer, p2 - DqnV2_1f(5), p2 + DqnV2_1f(5), blue);
			DTRRender_Rectangle(renderBuffer, p3 - DqnV2_1f(5), p3 + DqnV2_1f(5), purple);
			DTRRender_Rectangle(renderBuffer, p4 - DqnV2_1f(5), p4 + DqnV2_1f(5), red);
		}
	}
}

void DTRRender_Clear(DTRRenderBuffer *const renderBuffer,
                     DqnV3 color)
{
	if (!renderBuffer) return;

	DQN_ASSERT(color.r >= 0.0f && color.r <= 1.0f);
	DQN_ASSERT(color.g >= 0.0f && color.g <= 1.0f);
	DQN_ASSERT(color.b >= 0.0f && color.b <= 1.0f);
	color *= 255.0f;

	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	for (i32 y = 0; y < renderBuffer->height; y++)
	{
		for (i32 x = 0; x < renderBuffer->width; x++)
		{
			u32 pixel = ((i32)0       << 24) |
			            ((i32)color.r << 16) |
			            ((i32)color.g << 8)  |
			            ((i32)color.b << 0);
			bitmapPtr[x + (y * renderBuffer->width)] = pixel;
		}
	}
}
