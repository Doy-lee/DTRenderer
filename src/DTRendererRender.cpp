#include "DTRendererRender.h"
#include "DTRendererDebug.h"
#include "DTRendererPlatform.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb_rect_pack.h"
#include "external/stb_truetype.h"

#include <intrin.h>

FILE_SCOPE const f32 COLOR_EPSILON = 0.9f;

typedef struct RenderLightInternal
{
	enum DTRRenderShadingMode mode;
	DqnV3 vector;

	DqnV3 normals[4];
	u32 numNormals;
} RenderLightInternal;


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
FILE_SCOPE inline void SetPixel(DTRRenderContext context, const i32 x, const i32 y,
                                DqnV4 color, const enum ColorSpace colorSpace = ColorSpace_SRGB)
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;
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

void DTRRender_Text(DTRRenderContext context,
                    const DTRFont font, DqnV2 pos, const char *const text,
                    DqnV4 color, i32 len)
{
	if (!text) return;

	DTRRenderBuffer *renderBuffer = context.renderBuffer;
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
				SetPixel(context, actualX, actualY, resultColor, ColorSpace_Linear);
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

void DTRRender_Line(DTRRenderContext context, DqnV2i a,
                    DqnV2i b, DqnV4 color)
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;
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
		SetPixel(context, *plotX, *plotY, color, ColorSpace_Linear);

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

	TransformPoints(origin, result.pList, DQN_ARRAY_COUNT(result.pList), transform.scale.xy, transform.rotation);

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

void DTRRender_Rectangle(DTRRenderContext context, DqnV2 min, DqnV2 max,
                         DqnV4 color, const DTRRenderTransform transform)
{
	DTR_DEBUG_EP_TIMED_FUNCTION();
	DTRRenderBuffer *renderBuffer = context.renderBuffer;
	if (!renderBuffer) return;

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

				if (pIsInside) SetPixel(context, bufferX, bufferY, color, ColorSpace_Linear);
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
				SetPixel(context, bufferX, bufferY, color, ColorSpace_Linear);
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
			DTRRender_Line(context, DqnV2i_2f(min.x, min.y), DqnV2i_2f(min.x, max.y), color);
			DTRRender_Line(context, DqnV2i_2f(min.x, max.y), DqnV2i_2f(max.x, max.y), color);
			DTRRender_Line(context, DqnV2i_2f(max.x, max.y), DqnV2i_2f(max.x, min.y), color);
			DTRRender_Line(context, DqnV2i_2f(max.x, min.y), DqnV2i_2f(min.x, min.y), color);
		}

		// Draw rotating outline
		if (transform.rotation > 0)
		{
			DqnV4 green = DqnV4_4f(0, 1, 0, 1);
			DTRRender_Line(context, DqnV2i_V2(pList[0]), DqnV2i_V2(pList[1]), green);
			DTRRender_Line(context, DqnV2i_V2(pList[1]), DqnV2i_V2(pList[2]), green);
			DTRRender_Line(context, DqnV2i_V2(pList[2]), DqnV2i_V2(pList[3]), green);
			DTRRender_Line(context, DqnV2i_V2(pList[3]), DqnV2i_V2(pList[0]), green);
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

FILE_SCOPE inline f32 Triangle2TimesSignedArea(const DqnV2 a, const DqnV2 b, const DqnV2 c)
{
	f32 result = ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// SIMD
////////////////////////////////////////////////////////////////////////////////
// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline void DebugSIMDAssertColorInRange(__m128 color, f32 min, f32 max)
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

// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline __m128 SIMDSRGB1ToLinearSpace(__m128 color)
{
	DebugSIMDAssertColorInRange(color, 0.0f, 1.0f);

	f32 preserveAlpha   = ((f32 *)&color)[3];
	__m128 result       = _mm_mul_ps(color, color);
	((f32 *)&result)[3] = preserveAlpha;

	return result;
}

// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline __m128 SIMDSRGB255ToLinearSpace1(__m128 color)
{
	LOCAL_PERSIST const __m128 INV255_4X = _mm_set_ps1(DTRRENDER_INV_255);
	color                                = _mm_mul_ps(color, INV255_4X);

	f32 preserveAlpha   = ((f32 *)&color)[3];
	__m128 result       = _mm_mul_ps(color, color);
	((f32 *)&result)[3] = preserveAlpha;

	return result;
}

// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline __m128 SIMDLinearSpace1ToSRGB1(__m128 color)
{
	DebugSIMDAssertColorInRange(color, 0.0f, 1.0f);

	f32 preserveAlpha   = ((f32 *)&color)[3];
	__m128 result       = _mm_sqrt_ps(color);
	((f32 *)&result)[3] = preserveAlpha;

	return result;
}


// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline __m128 SIMDPreMultiplyAlpha1(__m128 color)
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

	DqnV2 p1p2Anchored = p1p2 * transform.anchor.xy;
	DqnV2 p1p3Anchored = p1p3 * transform.anchor.xy;
	DqnV2 origin       = p1 + p1p2Anchored + p1p3Anchored;

	return origin;
}

// color: _mm_set_ps(a, b, g, r) ie. 0=r, 1=g, 2=b, 3=a
FILE_SCOPE inline void SIMDSetPixel(DTRRenderContext context, const i32 x, const i32 y,
                                     __m128 color,
                                     const enum ColorSpace colorSpace = ColorSpace_SRGB)
{

	DTRRenderBuffer *renderBuffer = context.renderBuffer;
	if (!renderBuffer) return;
	if (x < 0 || x > (renderBuffer->width - 1)) return;
	if (y < 0 || y > (renderBuffer->height - 1)) return;

	DTR_DEBUG_EP_TIMED_FUNCTION();
	DebugSIMDAssertColorInRange(color, 0.0f, 1.0f);

	// If some alpha is involved, we need to apply gamma correction, but if the
	// new pixel is totally opaque or invisible then we're just flat out
	// overwriting/keeping the state of the pixel so we can save cycles by skipping.
	f32 alpha = ((f32 *)&color)[3];
	bool needGammaFix =
	    (alpha > 0.0f || alpha < (1.0f + COLOR_EPSILON)) && (colorSpace == ColorSpace_SRGB);
	if (needGammaFix) color = SIMDSRGB1ToLinearSpace(color);

	// Format: u32 == (XX, RR, GG, BB)
	u32 *const bitmapPtr = (u32 *)renderBuffer->memory;
	const u32 pitchInU32 = (renderBuffer->width * renderBuffer->bytesPerPixel) / 4;

	u32 srcPixel = bitmapPtr[x + (y * pitchInU32)];
	__m128 src   = _mm_set_ps(0, (f32)((srcPixel >> 0) & 0xFF), (f32)((srcPixel >> 8) & 0xFF),
	                        (f32)((srcPixel >> 16) & 0xFF));
	src = SIMDSRGB255ToLinearSpace1(src);

	f32 invA       = 1 - alpha;
	__m128 invA_4x = _mm_set_ps1(invA);

	// PreAlphaMulColor + (1 - Alpha) * Src
	__m128 oneMinusAlphaSrc = _mm_mul_ps(invA_4x, src);
	__m128 dest             = _mm_add_ps(color, oneMinusAlphaSrc);
	dest                    = SIMDLinearSpace1ToSRGB1(dest);
	dest                    = _mm_mul_ps(dest, _mm_set_ps1(255.0f)); // to 0->255 range

	DebugSIMDAssertColorInRange(dest, 0.0f, 255.0f);

	f32 destR = ((f32 *)&dest)[0];
	f32 destG = ((f32 *)&dest)[1];
	f32 destB = ((f32 *)&dest)[2];

	u32 pixel = // ((u32)(destA) << 24 |
	    (u32)(destR) << 16 | (u32)(destG) << 8 | (u32)(destB) << 0;
	bitmapPtr[x + (y * pitchInU32)] = pixel;
}

// colorModulate: _mm_set_ps(a, b, g, r)     ie. 0=r, 1=g, 2=b, 3=a
// barycentric:   _mm_set_ps(xx, p3, p2, p1) ie. 0=p1, 1=p2, 2=p3, 3=a
FILE_SCOPE __m128 SIMDSampleTextureForTriangle(const DTRBitmap *const texture, const DqnV2 uv1,
                                               const DqnV2 uv2SubUv1, const DqnV2 uv3SubUv1,
                                               const __m128 barycentric)
{
	DTRDebug_BeginCycleCount("SIMDTexturedTriangle_SampleTexture",
	                         DTRDebugCycleCount_SIMDTexturedTriangle_SampleTexture);

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

	color = SIMDSRGB255ToLinearSpace1(color);
	DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMDTexturedTriangle_SampleTexture);
	return color;
}

// IMPORTANT: Debug Markers can _NOT_ be used in primitive rendering functions,
// ie. any render function that is used in this call because it'll call into
// itself infinitely.
FILE_SCOPE void DebugRenderMarkers(DTRRenderContext context, const DqnV2 *const pList,
                                   const i32 pListSize, const DTRRenderTransform transform,
                                   bool drawBoundingBox, bool drawBasis, bool drawVertexMarkers)
{
	if (!DTR_DEBUG) return;
	if (!DTR_DEBUG_RENDER) return;

	DqnV4 green  = DqnV4_4f(0, 1, 0, 1);
	DqnV4 blue   = DqnV4_4f(0, 0, 1, 1);
	DqnV4 purple = DqnV4_4f(1, 0, 1, 1);
	DqnV4 red    = DqnV4_4f(1, 0, 0, 1);

	// Draw Bounding box
	if (drawBoundingBox)
	{
		DqnRect bounds = GetBoundingBox(pList, pListSize);

		DTRRender_Line(context, DqnV2i_2f(bounds.min.x, bounds.min.y), DqnV2i_2f(bounds.min.x, bounds.max.y), red);
		DTRRender_Line(context, DqnV2i_2f(bounds.min.x, bounds.max.y), DqnV2i_2f(bounds.max.x, bounds.max.y), red);
		DTRRender_Line(context, DqnV2i_2f(bounds.max.x, bounds.max.y), DqnV2i_2f(bounds.max.x, bounds.min.y), red);
		DTRRender_Line(context, DqnV2i_2f(bounds.max.x, bounds.min.y), DqnV2i_2f(bounds.min.x, bounds.min.y), red);
	}

	// Draw Coordinate Basis
	if (drawBasis)
	{
		// TODO(doyle): Fixme
		if (pListSize == 3)
		{
			DqnV2 origin = Get2DOriginFromTransformAnchor(pList[0], pList[1], pList[2], transform);
			const f32 rotation  = transform.rotation;
			DqnV2 xAxis         = DqnV2_2f(cosf(rotation), sinf(rotation)) * transform.scale.x;
			DqnV2 yAxis         = DqnV2_2f(-xAxis.y, xAxis.x) * transform.scale.y;
			DqnV4 coordSysColor = DqnV4_4f(0, 1, 1, 1);
			i32 axisLen         = 50;
			DTRRender_Line(context, DqnV2i_V2(origin),
			               DqnV2i_V2(origin) + DqnV2i_V2(xAxis * axisLen), coordSysColor);
			DTRRender_Line(context, DqnV2i_V2(origin),
			               DqnV2i_V2(origin) + DqnV2i_V2(yAxis * axisLen), coordSysColor);
		}
	}

	// Draw axis point
	if (drawVertexMarkers)
	{
		DqnV4 colorList[] = {green, blue, purple, red};
		for (i32 i = 0; i < pListSize; i++)
		{
			DqnV2 p = pList[i];
			DTRRender_Rectangle(context, p - DqnV2_1f(5), p + DqnV2_1f(5), colorList[i]);
		}
	}
}

FILE_SCOPE inline f32 GetCurrZDepth(DTRRenderContext context, i32 posX, i32 posY)
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;
	DQN_ASSERT(renderBuffer);
	const u32 zBufferPitch        = renderBuffer->width;

	i32 zBufferIndex = posX + (posY * zBufferPitch);
	DQN_ASSERT(zBufferIndex < (renderBuffer->width * renderBuffer->height));

	context.api->LockAcquire(renderBuffer->renderLock);
	f32 currZDepth = renderBuffer->zBuffer[zBufferIndex];
	context.api->LockRelease(renderBuffer->renderLock);
	return currZDepth;
}

FILE_SCOPE inline void SetCurrZDepth(DTRRenderContext context, i32 posX, i32 posY, f32 newZDepth)
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;
	DQN_ASSERT(renderBuffer);
	const u32 zBufferPitch        = renderBuffer->width;

	i32 zBufferIndex = posX + (posY * zBufferPitch);
	DQN_ASSERT(zBufferIndex < (renderBuffer->width * renderBuffer->height));

	context.api->LockAcquire(renderBuffer->renderLock);
	renderBuffer->zBuffer[zBufferIndex] = newZDepth;
	context.api->LockRelease(renderBuffer->renderLock);
}

#define DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(type)                                             \
	do                                                                                             \
	{                                                                                              \
		if (texture)                                                                               \
			DTRDebug_BeginCycleCount("SIMDTextured" #type, DTRDebugCycleCount_SIMDTextured##type); \
		else                                                                                       \
			DTRDebug_BeginCycleCount("SIMD" #type, DTRDebugCycleCount_SIMD##type);                 \
	} while (0)

#define DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(type)                                               \
	do                                                                                             \
	{                                                                                              \
		if (texture)                                                                               \
			DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMDTextured##type);                                 \
		else                                                                                       \
			DTRDebug_EndCycleCount(DTRDebugCycleCount_SIMD##type);                                 \
	} while (0)

FILE_SCOPE void SIMDRasteriseTrianglePixel(DTRRenderContext context,
                                           const DTRBitmap *const texture, const i32 posX,
                                           const i32 posY, const i32 maxX, const DqnV2 uv1,
                                           const DqnV2 uv2SubUv1, const DqnV2 uv3SubUv1,
                                           const __m128 simdColor, const __m128 triangleZ,
                                           const __m128 signedArea,
                                           const __m128 invSignedAreaParallelogram_4x)
{
	const __m128 ZERO_4X      = _mm_set_ps1(0.0f);
	const u32 IS_GREATER_MASK = 0xF;

	DTRRenderBuffer *renderBuffer = context.renderBuffer;

	// TODO(doyle): Copy lighting work over. But not important since using this
	// function causes performance problems.

	// Rasterise buffer(X, Y) pixel
	{
		__m128 isGreater    = _mm_cmpge_ps(signedArea, ZERO_4X);
		i32 isGreaterResult = _mm_movemask_ps(isGreater);

		if ((isGreaterResult & IS_GREATER_MASK) == IS_GREATER_MASK && posX < maxX)
		{
			DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_RasterisePixel);
			__m128 barycentric  = _mm_mul_ps(signedArea, invSignedAreaParallelogram_4x);
			__m128 barycentricZ = _mm_mul_ps(triangleZ, barycentric);

			f32 pixelZDepth =
			    ((f32 *)&barycentricZ)[0] + ((f32 *)&barycentricZ)[1] + ((f32 *)&barycentricZ)[2];
			f32 currZDepth = GetCurrZDepth(context, posX, posY);
			if (pixelZDepth > currZDepth)
			{
				SetCurrZDepth(context, posX, posY, pixelZDepth);

				__m128 finalColor = simdColor;
				if (texture)
				{
					__m128 texSampledColor = SIMDSampleTextureForTriangle(texture, uv1, uv2SubUv1,
					                                                      uv3SubUv1, barycentric);
					finalColor = _mm_mul_ps(texSampledColor, simdColor);
				}
				SIMDSetPixel(context, posX, posY, finalColor, ColorSpace_Linear);
			}
			DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_RasterisePixel);
		}
	}
}

FILE_SCOPE void SIMDTriangle(DTRRenderContext context,
                             const DqnV3 p1, const DqnV3 p2, const DqnV3 p3, const DqnV2 uv1,
                             const DqnV2 uv2, const DqnV2 uv3, const f32 lightIntensity1,
                             const f32 lightIntensity2, const f32 lightIntensity3,
                             const bool ignoreLight, DTRBitmap *const texture, DqnV4 color,
                             const DqnV2i min, const DqnV2i max)

{
	DTR_DEBUG_EP_TIMED_FUNCTION();
	DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle);

	DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Preamble);

	DTRRenderBuffer *const renderBuffer = context.renderBuffer;
	////////////////////////////////////////////////////////////////////////////
	// Convert color
	////////////////////////////////////////////////////////////////////////////
	__m128 simdColor  = _mm_set_ps(color.a, color.b, color.g, color.r);
	simdColor         = SIMDSRGB1ToLinearSpace(simdColor);
	simdColor         = SIMDPreMultiplyAlpha1(simdColor);
	f32 preserveAlpha = ((f32 *)&simdColor)[3];

	const __m128 ZERO_4X       = _mm_set_ps1(0.0f);
	__m128 simdLightIntensity1 = _mm_set_ps1(lightIntensity1);
	__m128 simdLightIntensity2 = _mm_set_ps1(lightIntensity2);
	__m128 simdLightIntensity3 = _mm_set_ps1(lightIntensity3);

	simdLightIntensity1 = _mm_max_ps(simdLightIntensity1, ZERO_4X);
	simdLightIntensity2 = _mm_max_ps(simdLightIntensity2, ZERO_4X);
	simdLightIntensity3 = _mm_max_ps(simdLightIntensity3, ZERO_4X);

	__m128 p1Light = _mm_mul_ps(simdColor, simdLightIntensity1);
	__m128 p2Light = _mm_mul_ps(simdColor, simdLightIntensity2);
	__m128 p3Light = _mm_mul_ps(simdColor, simdLightIntensity3);

	////////////////////////////////////////////////////////////////////////////
	// Setup SIMD data
	////////////////////////////////////////////////////////////////////////////
	const u32 NUM_X_PIXELS_TO_SIMD = 2;
	const u32 NUM_Y_PIXELS_TO_SIMD = 1;

	// SignedArea: _mm_set_ps(unused, p3, p2, p1) ie 0=p1, 1=p1, 2=p3, 3=unused
	__m128 signedAreaPixel1 = _mm_set_ps1(0);
	__m128 signedAreaPixel2 = _mm_set_ps1(0);

	__m128 signedAreaPixelDeltaX         = _mm_set_ps1(0);
	__m128 signedAreaPixelDeltaY         = _mm_set_ps1(0);
	__m128 invSignedAreaParallelogram_4x = _mm_set_ps1(0);

	__m128 triangleZ = _mm_set_ps(0, p3.z, p2.z, p1.z);
	{
		DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Preamble_SArea);
		DTR_DEBUG_EP_TIMED_BLOCK("SIMDTriangle_Preamble_SArea");
		DqnV2 startP          = DqnV2_V2i(min);
		f32 signedArea1Start  = Triangle2TimesSignedArea(p2.xy, p3.xy, startP);
		f32 signedArea1DeltaX = p2.y - p3.y;
		f32 signedArea1DeltaY = p3.x - p2.x;

		f32 signedArea2Start  = Triangle2TimesSignedArea(p3.xy, p1.xy, startP);
		f32 signedArea2DeltaX = p3.y - p1.y;
		f32 signedArea2DeltaY = p1.x - p3.x;

		f32 signedArea3Start  = Triangle2TimesSignedArea(p1.xy, p2.xy, startP);
		f32 signedArea3DeltaX = p1.y - p2.y;
		f32 signedArea3DeltaY = p2.x - p1.x;
		DTR_DEBUG_EP_TIMED_END_BLOCK();
		DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Preamble_SArea);

		DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Preamble_SIMDStep);
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
		DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Preamble_SIMDStep);
	}

	const DqnV2 uv2SubUv1 = uv2 - uv1;
	const DqnV2 uv3SubUv1 = uv3 - uv1;

#define UNROLL_LOOP 1
	DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Preamble);

#if UNROLL_LOOP
	const u32 IS_GREATER_MASK = 0xF;
	const u32 zBufferPitch    = renderBuffer->width;
#endif

	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Rasterise);
	for (i32 bufferY = min.y; bufferY < max.y; bufferY += NUM_Y_PIXELS_TO_SIMD)
	{
		__m128 signedArea1 = signedAreaPixel1;
		__m128 signedArea2 = signedAreaPixel2;

		for (i32 bufferX = min.x; bufferX < max.x; bufferX += NUM_X_PIXELS_TO_SIMD)
		{
#if UNROLL_LOOP
			// Rasterise buffer(X, Y) pixel
			{
				__m128 checkArea    = signedArea1;
				__m128 isGreater    = _mm_cmpge_ps(checkArea, ZERO_4X);
				i32 isGreaterResult = _mm_movemask_ps(isGreater);
				i32 posX            = bufferX;
				i32 posY            = bufferY;

				if ((isGreaterResult & IS_GREATER_MASK) == IS_GREATER_MASK)
				{
					DEBUG_SIMD_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_RasterisePixel);
					__m128 barycentric  = _mm_mul_ps(checkArea, invSignedAreaParallelogram_4x);
					__m128 barycentricZ = _mm_mul_ps(triangleZ, barycentric);

					f32 pixelZDepth  = ((f32 *)&barycentricZ)[0] +
					                   ((f32 *)&barycentricZ)[1] +
					                   ((f32 *)&barycentricZ)[2];

					i32 zBufferIndex = posX + (posY * zBufferPitch);
					context.api->LockAcquire(renderBuffer->renderLock);
					if (pixelZDepth > renderBuffer->zBuffer[zBufferIndex])
					{
						renderBuffer->zBuffer[zBufferIndex] = pixelZDepth;

						__m128 finalColor = simdColor;
						if (!ignoreLight)
						{
							__m128 barycentricA_4x = _mm_set_ps1(((f32 *)&barycentric)[0]);
							__m128 barycentricB_4x = _mm_set_ps1(((f32 *)&barycentric)[1]);
							__m128 barycentricC_4x = _mm_set_ps1(((f32 *)&barycentric)[2]);

							__m128 barycentricLight1 = _mm_mul_ps(p1Light, barycentricA_4x);
							__m128 barycentricLight2 = _mm_mul_ps(p2Light, barycentricB_4x);
							__m128 barycentricLight3 = _mm_mul_ps(p3Light, barycentricC_4x);

							__m128 light =
							    _mm_add_ps(barycentricLight3,
							               _mm_add_ps(barycentricLight1, barycentricLight2));

							finalColor = _mm_mul_ps(finalColor, light);
							((f32 *)&finalColor)[3] = preserveAlpha;
						}

						if (texture)
						{
							__m128 texSampledColor = SIMDSampleTextureForTriangle(texture, uv1, uv2SubUv1, uv3SubUv1, barycentric);
							finalColor             = _mm_mul_ps(texSampledColor, finalColor);
						}
						SIMDSetPixel(context, posX, posY, finalColor, ColorSpace_Linear);
					}
					context.api->LockRelease(renderBuffer->renderLock);
					DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_RasterisePixel);
				}
				signedArea1 = _mm_add_ps(signedArea1, signedAreaPixelDeltaX);
			}

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

					f32 pixelZDepth  = ((f32 *)&barycentricZ)[0] +
					                    ((f32 *)&barycentricZ)[1] +
					                    ((f32 *)&barycentricZ)[2];
					i32 zBufferIndex = posX + (posY * zBufferPitch);
					context.api->LockAcquire(renderBuffer->renderLock);
					if (pixelZDepth > renderBuffer->zBuffer[zBufferIndex])
					{
						renderBuffer->zBuffer[zBufferIndex] = pixelZDepth;

						__m128 finalColor = simdColor;
						if (!ignoreLight)
						{
							__m128 barycentricA_4x   = _mm_set_ps1(((f32 *)&barycentric)[0]);
							__m128 barycentricB_4x   = _mm_set_ps1(((f32 *)&barycentric)[1]);
							__m128 barycentricC_4x   = _mm_set_ps1(((f32 *)&barycentric)[2]);

							__m128 barycentricLight1 = _mm_mul_ps(p1Light, barycentricA_4x);
							__m128 barycentricLight2 = _mm_mul_ps(p2Light, barycentricB_4x);
							__m128 barycentricLight3 = _mm_mul_ps(p3Light, barycentricC_4x);

							__m128 light =
							    _mm_add_ps(barycentricLight3,
							               _mm_add_ps(barycentricLight1, barycentricLight2));

							finalColor = _mm_mul_ps(finalColor, light);
							((f32 *)&finalColor)[3] = preserveAlpha;
						}

						if (texture)
						{
							__m128 texSampledColor = SIMDSampleTextureForTriangle(texture, uv1, uv2SubUv1, uv3SubUv1, barycentric);
							finalColor             = _mm_mul_ps(texSampledColor, finalColor);
						}
						SIMDSetPixel(context, posX, posY, finalColor, ColorSpace_Linear);
					}
					context.api->LockRelease(renderBuffer->renderLock);
				}
				signedArea2 = _mm_add_ps(signedArea2, signedAreaPixelDeltaX);
			}
#else
			SIMDRasteriseTrianglePixel(renderBuffer, texture, bufferX, bufferY, max.x, uv1, uv2SubUv1,
			                           uv3SubUv1, simdColor, triangleZ, signedArea1,
			                           invSignedAreaParallelogram_4x);
			SIMDRasteriseTrianglePixel(renderBuffer, texture, bufferX + 1, bufferY, max.x, uv1, uv2SubUv1,
			                           uv3SubUv1, simdColor, triangleZ, signedArea2,
			                           invSignedAreaParallelogram_4x);
			signedArea1 = _mm_add_ps(signedArea1, signedAreaPixelDeltaX);
			signedArea2 = _mm_add_ps(signedArea2, signedAreaPixelDeltaX);
#endif
		}
		signedAreaPixel1 = _mm_add_ps(signedAreaPixel1, signedAreaPixelDeltaY);
		signedAreaPixel2 = _mm_add_ps(signedAreaPixel2, signedAreaPixelDeltaY);
	}
	DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Rasterise);
	DEBUG_SIMD_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle);
}

FILE_SCOPE void SlowTriangle(DTRRenderContext context, const DqnV3 p1, const DqnV3 p2,
                             const DqnV3 p3, const DqnV2 uv1, const DqnV2 uv2, const DqnV2 uv3,
                             const f32 lightIntensity1, const f32 lightIntensity2,
                             const f32 lightIntensity3, const bool ignoreLight,
                             DTRBitmap *const texture, DqnV4 color, const DqnV2i min,
                             const DqnV2i max)
{
	DTR_DEBUG_EP_TIMED_FUNCTION();
#define DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(type)                                             \
	do                                                                                             \
	{                                                                                              \
		if (texture)                                                                               \
			DTRDebug_BeginCycleCount("SlowTextured" #type, DTRDebugCycleCount_SlowTextured##type); \
		else                                                                                       \
			DTRDebug_BeginCycleCount("Slow" #type, DTRDebugCycleCount_Slow##type);                 \
	} while (0)

#define DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(type)                                               \
	do                                                                                             \
	{                                                                                              \
		if (texture)                                                                               \
			DTRDebug_EndCycleCount(DTRDebugCycleCount_SlowTextured##type);                                 \
		else                                                                                       \
			DTRDebug_EndCycleCount(DTRDebugCycleCount_Slow##type);                                 \
	} while (0)

	DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle);
	DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Preamble);

	DTRRenderBuffer *renderBuffer = context.renderBuffer;
	////////////////////////////////////////////////////////////////////////////
	// Convert Color
	////////////////////////////////////////////////////////////////////////////
	color = DTRRender_SRGB1ToLinearSpaceV4(color);
	color = PreMultiplyAlpha1(color);

	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Preamble_SArea);
	DqnV2i startP         = min;
	f32 signedArea1Pixel  = Triangle2TimesSignedArea(p2.xy, p3.xy, DqnV2_V2i(startP));
	f32 signedArea1DeltaX = p2.y - p3.y;
	f32 signedArea1DeltaY = p3.x - p2.x;

	f32 signedArea2Pixel  = Triangle2TimesSignedArea(p3.xy, p1.xy, DqnV2_V2i(startP));
	f32 signedArea2DeltaX = p3.y - p1.y;
	f32 signedArea2DeltaY = p1.x - p3.x;

	f32 signedArea3Pixel  = Triangle2TimesSignedArea(p1.xy, p2.xy, DqnV2_V2i(startP));
	f32 signedArea3DeltaX = p1.y - p2.y;
	f32 signedArea3DeltaY = p2.x - p1.x;
	DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Preamble_SArea);
	DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Preamble_SIMDStep);

	f32 signedAreaParallelogram = signedArea1Pixel + signedArea2Pixel + signedArea3Pixel;
	if (signedAreaParallelogram == 0) return;

	f32 invSignedAreaParallelogram = 1.0f / signedAreaParallelogram;
	DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Preamble_SIMDStep);

	const DqnV3 p2SubP1        = p2 - p1;
	const DqnV3 p3SubP1        = p3 - p1;
	const DqnV2 uv2SubUv1      = uv2 - uv1;
	const DqnV2 uv3SubUv1      = uv3 - uv1;
	const u32 texturePitch     = (texture) ? (texture->bytesPerPixel * texture->dim.w) : 0;
	const u8 *const texturePtr = (texture) ? (texture->memory) : NULL;
	const u32 zBufferPitch     = renderBuffer->width;
	const f32 INV_255          = 1 / 255.0f;
	DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Preamble);
	DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_Rasterise);

	DqnV3 p1Light = color.rgb * DQN_MAX(0, lightIntensity1);
	DqnV3 p2Light = color.rgb * DQN_MAX(0, lightIntensity2);
	DqnV3 p3Light = color.rgb * DQN_MAX(0, lightIntensity3);
	for (i32 bufferY = min.y; bufferY < max.y; bufferY++)
	{
		f32 signedArea1 = signedArea1Pixel;
		f32 signedArea2 = signedArea2Pixel;
		f32 signedArea3 = signedArea3Pixel;

		for (i32 bufferX = min.x; bufferX < max.x; bufferX++)
		{
			if (signedArea1 >= 0 && signedArea2 >= 0 && signedArea3 >= 0)
			{
				DEBUG_SLOW_AUTO_CHOOSE_BEGIN_CYCLE_COUNT(Triangle_RasterisePixel);
				f32 barycentricA = signedArea1 * invSignedAreaParallelogram;
				f32 barycentricB = signedArea2 * invSignedAreaParallelogram;
				f32 barycentricC = signedArea3 * invSignedAreaParallelogram;

				f32 pixelZDepth  = p1.z + (barycentricB * (p2SubP1.z)) + (barycentricC * (p3SubP1.z));
				f32 currZDepth   = GetCurrZDepth(context, bufferX, bufferY);

				if (pixelZDepth > currZDepth)
				{
					SetCurrZDepth(context, bufferX, bufferY, pixelZDepth);
					DqnV4 finalColor                    = color;

					if (!ignoreLight)
					{
						DqnV3 light = (p1Light * barycentricA) + (p2Light * barycentricB) +
						              (p3Light * barycentricC);
						finalColor.rgb *= light;
					}

					if (texture)
					{
						DqnV2 uv = uv1 + (uv2SubUv1 * barycentricB) + (uv3SubUv1 * barycentricC);

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

						color1 *= INV_255;
						color1 = DTRRender_SRGB1ToLinearSpaceV4(color1);
						finalColor *= color1;
					}

					SetPixel(context, bufferX, bufferY, finalColor, ColorSpace_Linear);
				}
				DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_RasterisePixel);
			}

			signedArea1 += signedArea1DeltaX;
			signedArea2 += signedArea2DeltaX;
			signedArea3 += signedArea3DeltaX;
		}

		signedArea1Pixel += signedArea1DeltaY;
		signedArea2Pixel += signedArea2DeltaY;
		signedArea3Pixel += signedArea3DeltaY;
	}
	DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle_Rasterise);
	DEBUG_SLOW_AUTO_CHOOSE_END_CYCLE_COUNT(Triangle);
}

DqnMat4 GLViewport(f32 x, f32 y, f32 width, f32 height)
{
	// Given a normalised coordinate from [-1, 1] for X, Y and Z, we want to map this
	// back to viewport coordinates, or i.e. screen coordinates.

	// Consider [-1, 1] for X. We want to (1.0f + [-1, 1] == [0, 2]). Then halve
	// it, (0.5f * [0, 2] == [0, 1]) Then shift it to the viewport origin, (x,
	// y). Then multiply this matrix by the normalised coordinate to find the
	// screen space. This information is what this matrix stores.

	DqnMat4 result                          = DqnMat4_Identity();
	f32 halfWidth                           = width * 0.5f;
	f32 halfHeight                          = height * 0.5f;
	const f32 DEPTH_BUFFER_GRANULARITY      = 255.0f;
	const f32 HALF_DEPTH_BUFFER_GRANULARITY = DEPTH_BUFFER_GRANULARITY * 0.5f;

	result.e[0][0] = halfWidth;
	result.e[1][1] = halfHeight;
	result.e[2][2] = HALF_DEPTH_BUFFER_GRANULARITY;

	result.e[3][0] = x + halfWidth;
	result.e[3][1] = y + halfHeight;
	result.e[3][2] = HALF_DEPTH_BUFFER_GRANULARITY;

	return result;
}

FILE_SCOPE void
TexturedTriangleInternal(DTRRenderContext context, RenderLightInternal lighting, DqnV3 p1, DqnV3 p2,
                         DqnV3 p3, DqnV2 uv1, DqnV2 uv2, DqnV2 uv3, DTRBitmap *const texture,
                         DqnV4 color,
                         const DTRRenderTransform transform = DTRRender_DefaultTriangleTransform())
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;

	////////////////////////////////////////////////////////////////////////////
	// Transform vertexes p1, p2, p3 inplace
	////////////////////////////////////////////////////////////////////////////
	Make3PointsClockwise(&p1, &p2, &p3);

	DqnV2 origin  = Get2DOriginFromTransformAnchor(p1.xy, p2.xy, p3.xy, transform);
	DqnV2 pList[] = {p1.xy - origin, p2.xy - origin, p3.xy - origin};
	TransformPoints(origin, pList, DQN_ARRAY_COUNT(pList), transform.scale.xy, transform.rotation);

	p1.xy = pList[0];
	p2.xy = pList[1];
	p3.xy = pList[2];

	DqnRect bounds      = GetBoundingBox(pList, DQN_ARRAY_COUNT(pList));
	DqnRect screenSpace = DqnRect_4i(0, 0, renderBuffer->width - 1, renderBuffer->height - 1);
	bounds              = DqnRect_ClipRect(bounds, screenSpace);
	DqnV2i min          = DqnV2i_V2(bounds.min);
	DqnV2i max          = DqnV2i_V2(bounds.max);

	////////////////////////////////////////////////////////////////////////////
	// Calculate light
	////////////////////////////////////////////////////////////////////////////
	f32 lightIntensity1 = 1, lightIntensity2 = 1, lightIntensity3 = 1;
	bool ignoreLight = false;
	if (lighting.mode == DTRRenderShadingMode_FullBright)
	{
		ignoreLight = true;
	}
	else
	{
		lighting.vector = DqnV3_Normalise(lighting.vector);
		if (lighting.mode == DTRRenderShadingMode_Flat)
		{
			DqnV3 p2SubP1 = p2 - p1;
			DqnV3 p3SubP1 = p3 - p1;

			DqnV3 normal  = DqnV3_Normalise(DqnV3_Cross(p2SubP1, p3SubP1));
			f32 intensity = DqnV3_Dot(normal, lighting.vector);
			intensity     = DQN_MAX(0, intensity);
			color.rgb *= intensity;
		}
		else
		{
			DQN_ASSERT(lighting.numNormals == 3);
			DQN_ASSERT(lighting.mode == DTRRenderShadingMode_Gouraud);
			lightIntensity1 = DqnV3_Dot(DqnV3_Normalise(lighting.normals[0]), lighting.vector);
			lightIntensity2 = DqnV3_Dot(DqnV3_Normalise(lighting.normals[1]), lighting.vector);
			lightIntensity3 = DqnV3_Dot(DqnV3_Normalise(lighting.normals[2]), lighting.vector);
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// SIMD/Slow Path
	////////////////////////////////////////////////////////////////////////////
	if (globalDTRPlatformFlags.canUseSSE2)
	{
		SIMDTriangle(context, p1, p2, p3, uv1, uv2, uv3, lightIntensity1, lightIntensity2,
		             lightIntensity3, ignoreLight, texture, color, min, max);
	}
	else
	{
		SlowTriangle(context, p1, p2, p3, uv1, uv2, uv3, lightIntensity1, lightIntensity2,
		             lightIntensity3, ignoreLight, texture, color, min, max);
	}

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	DTRDebug_CounterIncrement(DTRDebugCounter_RenderTriangle);
	{
		bool drawBoundingBox = false;
		bool drawBasis       = false;
		bool drawVertexMarkers = false;

		DebugRenderMarkers(context, pList, DQN_ARRAY_COUNT(pList), transform, drawBoundingBox,
		                   drawBasis, drawVertexMarkers);
	}
}

FILE_SCOPE RenderLightInternal NullRenderLightInternal()
{
	RenderLightInternal result = {};
	return result;
}

void DTRRender_TexturedTriangle(DTRRenderContext context,
                                DqnV3 p1, DqnV3 p2, DqnV3 p3, DqnV2 uv1, DqnV2 uv2, DqnV2 uv3,
                                DTRBitmap *const texture, DqnV4 color,
                                const DTRRenderTransform transform)
{
	TexturedTriangleInternal(context, NullRenderLightInternal(), p1, p2, p3, uv1, uv2, uv3, texture,
	                         color, transform);
}

typedef struct RenderMeshJob
{
	DTRRenderContext context;
	DTRBitmap *tex;
	RenderLightInternal lighting;

	DqnV3 v1;
	DqnV3 v2;
	DqnV3 v3;
	DqnV2 uv1;
	DqnV2 uv2;
	DqnV2 uv3;
	DqnV4 color;
} RenderMeshJob;

void MultiThreadedRenderMesh(PlatformJobQueue *const queue, void *const userData)
{
	if (!queue || !userData)
	{
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
		return;
	}

	RenderMeshJob *job = (RenderMeshJob *)userData;
	TexturedTriangleInternal(job->context, job->lighting, job->v1, job->v2, job->v3, job->uv1,
	                         job->uv2, job->uv3, job->tex, job->color);
}

void DTRRender_Mesh(DTRRenderContext context, PlatformJobQueue *const jobQueue, DTRMesh *const mesh,
                    DTRRenderLight lighting, const DqnV3 pos, const DTRRenderTransform transform)
{
	DqnMemStack *const tempStack        = context.tempStack;
	DTRRenderBuffer *const renderBuffer = context.renderBuffer;
	PlatformAPI *const api              = context.api;

	if (!mesh || !renderBuffer || !tempStack || !api || !jobQueue) return;

	DqnMat4 viewPModelViewProjection = {};
	{
		// Create model matrix
		DqnMat4 translateMatrix = DqnMat4_Translate(pos.x, pos.y, pos.z);
		DqnMat4 scaleMatrix     = DqnMat4_ScaleV3(transform.scale);
		DqnMat4 rotateMatrix =
		    DqnMat4_Rotate(DQN_DEGREES_TO_RADIANS(transform.rotation), transform.anchor.x,
		                   transform.anchor.y, transform.anchor.z);
		DqnMat4 modelMatrix = DqnMat4_Mul(translateMatrix, DqnMat4_Mul(rotateMatrix, scaleMatrix));

		// Create camera matrix
		DqnV3 eye          = DqnV3_3f(0, 0, 1);
		DqnV3 up           = DqnV3_3f(0, 1, 0);
		DqnV3 center       = DqnV3_3f(0, 0, 0);
		DqnMat4 viewMatrix = DqnMat4_LookAt(eye, center, up);

		// Create projection matrix
		f32 aspectRatio     = (f32)renderBuffer->width / (f32)renderBuffer->height;
		DqnMat4 perspective = DqnMat4_Perspective(80.0f, aspectRatio, 0.5f, 100.0f);
		perspective         = DqnMat4_Identity();
		perspective.e[2][3] = -1.0f / DqnV3_Length(eye, center);

		// Combine matrix + matrix that maps NDC to screen space
		DqnMat4 viewport  = GLViewport(0, 0, (f32)renderBuffer->width, (f32)renderBuffer->height);
		DqnMat4 modelView = DqnMat4_Mul(viewMatrix, modelMatrix);
		DqnMat4 modelViewProjection = DqnMat4_Mul(perspective, modelView);
		viewPModelViewProjection    = DqnMat4_Mul(viewport, modelViewProjection);
	}

	bool RUN_MULTITHREADED = true;
	for (u32 i = 0; i < mesh->numFaces; i++)
	{
		DTRMeshFace face = mesh->faces[i];

		DqnV4 v1, v2, v3;
		DqnV3 norm1, norm2, norm3;
		{
			DQN_ASSERT(face.numVertexIndex == 3);
			DQN_ASSERT(face.numNormalIndex == 3);

			i32 v1Index = face.vertexIndex[0];
			i32 v2Index = face.vertexIndex[1];
			i32 v3Index = face.vertexIndex[2];

			// TODO(doyle): Some models have -ve indexes to refer to relative
			// vertices. We should resolve that to positive indexes at run time.
			DQN_ASSERT(v1Index < (i32)mesh->numVertexes);
			DQN_ASSERT(v2Index < (i32)mesh->numVertexes);
			DQN_ASSERT(v3Index < (i32)mesh->numVertexes);

			v1 = mesh->vertexes[v1Index];
			v2 = mesh->vertexes[v2Index];
			v3 = mesh->vertexes[v3Index];

			DQN_ASSERT(v1.w == 1);
			DQN_ASSERT(v2.w == 1);
			DQN_ASSERT(v3.w == 1);

			i32 norm1Index = face.normalIndex[0];
			i32 norm2Index = face.normalIndex[1];
			i32 norm3Index = face.normalIndex[2];

			DQN_ASSERT(norm1Index < (i32)mesh->numNormals);
			DQN_ASSERT(norm2Index < (i32)mesh->numNormals);
			DQN_ASSERT(norm3Index < (i32)mesh->numNormals);

			norm1 = mesh->normals[norm1Index];
			norm2 = mesh->normals[norm2Index];
			norm3 = mesh->normals[norm3Index];
		}

		v1 = DqnMat4_MulV4(viewPModelViewProjection, v1);
		v2 = DqnMat4_MulV4(viewPModelViewProjection, v2);
		v3 = DqnMat4_MulV4(viewPModelViewProjection, v3);

		// Perspective Divide to Normalise Device Coordinates
		v1.xyz = (v1.xyz / v1.w);
		v2.xyz = (v2.xyz / v2.w);
		v3.xyz = (v3.xyz / v3.w);

		// NOTE: Because we need to draw on pixel boundaries. We need to round
		// up to closest pixel otherwise we will have gaps.
		v1.x = (f32)(i32)(v1.x + 0.5f);
		v1.y = (f32)(i32)(v1.y + 0.5f);
		v2.x = (f32)(i32)(v2.x + 0.5f);
		v2.y = (f32)(i32)(v2.y + 0.5f);
		v3.x = (f32)(i32)(v3.x + 0.5f);
		v3.y = (f32)(i32)(v3.y + 0.5f);

		i32 uv1Index = face.texIndex[0];
		i32 uv2Index = face.texIndex[1];
		i32 uv3Index = face.texIndex[2];

		DQN_ASSERT(uv1Index < (i32)mesh->numTexUV);
		DQN_ASSERT(uv2Index < (i32)mesh->numTexUV);
		DQN_ASSERT(uv3Index < (i32)mesh->numTexUV);

		DqnV2 uv1 = mesh->texUV[uv1Index].xy;
		DqnV2 uv2 = mesh->texUV[uv2Index].xy;
		DqnV2 uv3 = mesh->texUV[uv3Index].xy;

		DqnV4 color = lighting.color;

		RenderLightInternal lightingInternal = {};
		lightingInternal.mode                = lighting.mode;
		lightingInternal.vector              = lighting.vector;
		lightingInternal.normals[0]          = norm1;
		lightingInternal.normals[1]          = norm2;
		lightingInternal.normals[2]          = norm3;
		lightingInternal.numNormals          = 3;

		bool DEBUG_NO_TEX = false;
		if (RUN_MULTITHREADED)
		{
			RenderMeshJob *jobData = (RenderMeshJob *)DqnMemStack_Push(tempStack, sizeof(*jobData));
			if (jobData)
			{
				jobData->v1       = v1.xyz;
				jobData->v2       = v2.xyz;
				jobData->v3       = v3.xyz;
				jobData->uv1      = uv1;
				jobData->uv2      = uv2;
				jobData->uv3      = uv3;
				jobData->color    = color;
				jobData->lighting = lightingInternal;
				jobData->context  = context;

				if (DTR_DEBUG && DEBUG_NO_TEX)
				{
					jobData->tex = NULL;
				}
				else
				{
					jobData->tex = &mesh->tex;
				}

				PlatformJob renderJob = {};
				renderJob.callback    = MultiThreadedRenderMesh;
				renderJob.userData    = jobData;
				while (!api->QueueAddJob(jobQueue, renderJob))
				{
					api->QueueTryExecuteNextJob(jobQueue);
				}
			}
			else
			{
				// TODO(doyle): Allocation error
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}

		}
		else
		{
			if (DTR_DEBUG && DEBUG_NO_TEX)
			{
				TexturedTriangleInternal(context, lightingInternal, v1.xyz, v2.xyz, v3.xyz,
				                         uv1, uv2, uv3, NULL, color);
			}
			else
			{
				TexturedTriangleInternal(context, lightingInternal, v1.xyz, v2.xyz, v3.xyz,
				                         uv1, uv2, uv3, &mesh->tex, color);
			}
		}

		bool DEBUG_WIREFRAME = false;
		if (DTR_DEBUG && DEBUG_WIREFRAME)
		{
			DqnV4 wireColor = DqnV4_4f(1.0f, 1.0f, 1.0f, 0.01f);
			DTRRender_Line(context, DqnV2i_V2(v1.xy), DqnV2i_V2(v2.xy), wireColor);
			DTRRender_Line(context, DqnV2i_V2(v2.xy), DqnV2i_V2(v3.xy), wireColor);
			DTRRender_Line(context, DqnV2i_V2(v3.xy), DqnV2i_V2(v1.xy), wireColor);
		}
	}

	// NOTE(doyle): Complete remaining jobs and wait until all jobs finished
	// before leaving function.
	if (RUN_MULTITHREADED)
	{
		while (api->QueueTryExecuteNextJob(jobQueue) || !api->QueueAllJobsComplete(jobQueue))
			;
	}
}

void DTRRender_Triangle(DTRRenderContext context, DqnV3 p1, DqnV3 p2, DqnV3 p3, DqnV4 color,
                        const DTRRenderTransform transform)
{
	const DqnV2 NO_UV       = {};
	DTRBitmap *const NO_TEX = NULL;
	TexturedTriangleInternal(context, NullRenderLightInternal(), p1, p2, p3, NO_UV, NO_UV,
	                         NO_UV, NO_TEX, color, transform);
}

void DTRRender_Bitmap(DTRRenderContext context, DTRBitmap *const bitmap, DqnV2 pos,
                      const DTRRenderTransform transform, DqnV4 color)
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;

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
					color1.a = (f32)(texel1 >> 24);
					color1.b = (f32)((texel1 >> 16) & 0xFF);
					color1.g = (f32)((texel1 >> 8) & 0xFF);
					color1.r = (f32)((texel1 >> 0) & 0xFF);

					DqnV4 color2;
					color2.a = (f32)(texel2 >> 24);
					color2.b = (f32)((texel2 >> 16) & 0xFF);
					color2.g = (f32)((texel2 >> 8) & 0xFF);
					color2.r = (f32)((texel2 >> 0) & 0xFF);

					DqnV4 color3;
					color3.a = (f32)(texel3 >> 24);
					color3.b = (f32)((texel3 >> 16) & 0xFF);
					color3.g = (f32)((texel3 >> 8) & 0xFF);
					color3.r = (f32)((texel3 >> 0) & 0xFF);

					DqnV4 color4;
					color4.a = (f32)(texel4 >> 24);
					color4.b = (f32)((texel4 >> 16) & 0xFF);
					color4.g = (f32)((texel4 >> 8) & 0xFF);
					color4.r = (f32)((texel4 >> 0) & 0xFF);

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

					SetPixel(context, bufferX, bufferY, blend, ColorSpace_Linear);
				}
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	{
		bool drawBoundingBox   = true;
		bool drawBasis         = true;
		bool drawVertexMarkers = true;

		DebugRenderMarkers(context, pList, RECT_PLIST_SIZE, transform, drawBoundingBox,
		                   drawBasis, drawVertexMarkers);
	}
}

void DTRRender_Clear(DTRRenderContext context, DqnV3 color)
{
	DTRRenderBuffer *renderBuffer = context.renderBuffer;
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

