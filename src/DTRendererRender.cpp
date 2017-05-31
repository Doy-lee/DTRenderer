#include "DTRendererRender.h"
#include "DTRendererDebug.h"
#include "DTRendererPlatform.h"

#define STB_RECT_PACK_IMPLEMENTATION
#define STB_TRUETYPE_IMPLEMENTATION
#include "external/stb_rect_pack.h"
#include "external/stb_truetype.h"

#include <intrin.h>

FILE_SCOPE const f32 COLOR_EPSILON = 0.9f;

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
#if 0
	result.r     = DTRRender_SRGB1ToLinearSpacef(color.r);
	result.g     = DTRRender_SRGB1ToLinearSpacef(color.g);
	result.b     = DTRRender_SRGB1ToLinearSpacef(color.b);
#else
	__m128 simdColor  = _mm_set_ps(color.r, color.g, color.b, 0);
	__m128 simdResult = _mm_mul_ps(simdColor, simdColor);
	result.r = ((f32 *)&simdResult)[3];
	result.g = ((f32 *)&simdResult)[2];
	result.b = ((f32 *)&simdResult)[1];
#endif
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
#if 0
	result.r     = DTRRender_LinearToSRGB1Spacef(color.r);
	result.g     = DTRRender_LinearToSRGB1Spacef(color.g);
	result.b     = DTRRender_LinearToSRGB1Spacef(color.b);
#else
	__m128 simdColor  = _mm_set_ps(color.r, color.g, color.b, 0);
	__m128 simdResult = _mm_sqrt_ps(simdColor);
	result.r = ((f32 *)&simdResult)[3];
	result.g = ((f32 *)&simdResult)[2];
	result.b = ((f32 *)&simdResult)[1];
#endif
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
#if 0
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

#else
	__m128 simdSrc = _mm_set_ps(0.0f,
	                            (f32)((src >> 16) & 0xFF),
	                            (f32)((src >> 8) & 0xFF),
	                            (f32)((src >> 0) & 0xFF));

	__m128 inv255_4x = _mm_set_ps1(DTRRENDER_INV_255);
	simdSrc          = _mm_mul_ps(simdSrc, inv255_4x);
	simdSrc          = _mm_mul_ps(simdSrc, simdSrc); // to linear

	f32 invANorm       = 1 - color.a;
	__m128 invANorm_4x = _mm_set_ps1(invANorm);
	__m128 const255_4x = _mm_set_ps1(255.0f);

	__m128 simdColor = _mm_set_ps(0, color.r, color.g, color.b);
	__m128 dest      = _mm_add_ps(simdColor, _mm_mul_ps(simdSrc, invANorm_4x)); // to 0->1 range
	dest             = _mm_sqrt_ps(dest);                                   // to srgb
	dest             = _mm_mul_ps(dest, const255_4x);                       // to 0->255 range

	DQN_ASSERT(((f32 *)&dest)[2] >= 0);
	DQN_ASSERT(((f32 *)&dest)[1] >= 0);
	DQN_ASSERT(((f32 *)&dest)[0] >= 0);

	if (DTR_DEBUG)
	{
		DQN_ASSERT((((f32 *)&dest)[2] - 255.0f) < COLOR_EPSILON);
		DQN_ASSERT((((f32 *)&dest)[1] - 255.0f) < COLOR_EPSILON);
		DQN_ASSERT((((f32 *)&dest)[0] - 255.0f) < COLOR_EPSILON);
	}

	dest = _mm_min_ps(dest, const255_4x);

	f32 destR = ((f32 *)&dest)[2];
	f32 destG = ((f32 *)&dest)[1];
	f32 destB = ((f32 *)&dest)[0];

#endif

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

inline void SIMDRasteriseTexturedTriangle(DTRRenderBuffer *const renderBuffer, const DqnV3 p1,
                                          const DqnV3 p2, const DqnV3 p3, const DqnV2 uv1,
                                          const DqnV2 uv2, const DqnV2 uv3,
                                          DTRBitmap *const texture, const DqnV4 color)
{
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

	const u32 zBufferPitch = renderBuffer->width;
	const DqnV3 a          = p1;
	const DqnV3 b          = p2;
	const DqnV3 c          = p3;

	DqnV2i startP         = min;
	f32 signedAreaC       = ((b.x - a.x) * (startP.y - a.y)) - ((b.y - a.y) * (startP.x - a.x));
	f32 signedAreaCDeltaX = a.y - b.y;
	f32 signedAreaCDeltaY = b.x - a.x;

	f32 signedAreaA       = ((c.x - b.x) * (startP.y - b.y)) - ((c.y - b.y) * (startP.x - b.x));
	f32 signedAreaADeltaX = b.y - c.y;
	f32 signedAreaADeltaY = c.x - b.x;

	f32 signedAreaB       = ((a.x - c.x) * (startP.y - c.y)) - ((a.y - c.y) * (startP.x - c.x));
	f32 signedAreaBDeltaX = c.y - a.y;
	f32 signedAreaBDeltaY = a.x - c.x;

	f32 signedAreaParallelogram = signedAreaC + signedAreaA + signedAreaB;
	if (signedAreaParallelogram == 0) return;
	f32 invSignedAreaParallelogram = 1.0f / signedAreaParallelogram;

	__m128 invSignedAreaParallelogram_4x = _mm_set_ps1(invSignedAreaParallelogram);

	// NOTE: Order is important here!
	__m128 triangleZ             = _mm_set_ps(0, b.z, a.z, c.z);
	__m128 signedAreaPixelDeltaX = _mm_set_ps(0, signedAreaBDeltaX, signedAreaADeltaX, signedAreaCDeltaX);
	__m128 signedAreaPixelDeltaY = _mm_set_ps(0, signedAreaBDeltaY, signedAreaADeltaY, signedAreaCDeltaY);
	__m128 signedAreaPixel1      = _mm_set_ps(0, signedAreaB, signedAreaA, signedAreaC);
	__m128 signedAreaPixel2      = _mm_add_ps(signedAreaPixel1, signedAreaPixelDeltaX);

	const __m128 INV255_4X    = _mm_set_ps1(1.0f / 255.0f);
	const __m128 ZERO_4X      = _mm_set_ps1(0.0f);
	const __m128 TWO_4X       = _mm_set_ps1(2.0f);
	const u32 IS_GREATER_MASK = 0xF;

	// NOTE: Step size of 2 pixels across X
	signedAreaPixelDeltaX = _mm_mul_ps(signedAreaPixelDeltaX, TWO_4X);

	const DqnV2 uv2SubUv1      = uv2 - uv1;
	const DqnV2 uv3SubUv1      = uv3 - uv1;
	const __m128 colorModulate = _mm_set_ps(color.a, color.b, color.g, color.r);

	for (i32 bufferY = min.y; bufferY < max.y; bufferY++)
	{
		__m128 signedArea1 = signedAreaPixel1;
		__m128 signedArea2 = signedAreaPixel2;

#define PROCESS_COLOR_NO_SIMD 0
		for (i32 bufferX = min.x; bufferX < max.x; bufferX += 2)
		{
			__m128 isGreater1    = _mm_cmpge_ps(signedArea1, ZERO_4X);
			i32 isGreaterResult1 = _mm_movemask_ps(isGreater1);
			if ((isGreaterResult1 & IS_GREATER_MASK) == IS_GREATER_MASK)
			{
				__m128 barycentric  = _mm_mul_ps(signedArea1, invSignedAreaParallelogram_4x);
				__m128 barycentricZ = _mm_mul_ps(triangleZ, barycentric);

				i32 zBufferIndex = bufferX + (bufferY * zBufferPitch);
				f32 pixelZValue  = ((f32 *)&barycentricZ)[0] +
				                   ((f32 *)&barycentricZ)[1] +
				                   ((f32 *)&barycentricZ)[2];
				f32 currZValue = renderBuffer->zBuffer[zBufferIndex];
				if (pixelZValue > currZValue)
				{
					renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
					u8 *texturePtr                      = texture->memory;
					const u32 texturePitch              = texture->bytesPerPixel * texture->dim.w;

					f32 barycentricB = ((f32 *)&barycentric)[2];
					f32 barycentricC = ((f32 *)&barycentric)[0];
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

#if PROCESS_COLOR_NO_SIMD
					DqnV4 color1;
					color1.a = (f32)(texel1 >> 24);
					color1.b = (f32)((texel1 >> 16) & 0xFF);
					color1.g = (f32)((texel1 >> 8) & 0xFF);
					color1.r = (f32)((texel1 >> 0) & 0xFF);
					color1 *= DTRRENDER_INV_255;
					color1      = DTRRender_SRGB1ToLinearSpaceV4(color1);
					DqnV4 blend = color * color1;
#else
					__m128 color1 =
					    _mm_set_ps((f32)(texel1 >> 24), (f32)((texel1 >> 16) & 0xFF),
					               (f32)((texel1 >> 8) & 0xFF), (f32)((texel1 >> 0) & 0xFF));
					color1 = _mm_mul_ps(color1, INV255_4X);
					color1 = _mm_mul_ps(color1, color1); // to linear space
					color1 = _mm_mul_ps(color1, colorModulate);

					DqnV4 blend = {};
					blend.r     = ((f32 *)&color1)[0];
					blend.g     = ((f32 *)&color1)[1];
					blend.b     = ((f32 *)&color1)[2];
					blend.a     = ((f32 *)&color1)[3];
#endif
					SetPixel(renderBuffer, bufferX, bufferY, blend, ColorSpace_Linear);
				}
			}

			__m128 isGreater2    = _mm_cmpge_ps(signedArea2, ZERO_4X);
			i32 isGreaterResult2 = _mm_movemask_ps(isGreater2);
			i32 bufferX1         = bufferX + 1;
			if ((isGreaterResult2 & IS_GREATER_MASK) == IS_GREATER_MASK && bufferX1 < max.x)
			{
				__m128 barycentric  = _mm_mul_ps(signedArea2, invSignedAreaParallelogram_4x);
				__m128 barycentricZ = _mm_mul_ps(triangleZ, barycentric);

				i32 zBufferIndex = bufferX1 + (bufferY * zBufferPitch);
				f32 pixelZValue  = ((f32 *)&barycentricZ)[0] + ((f32 *)&barycentricZ)[1] +
				                  ((f32 *)&barycentricZ)[2];
				f32 currZValue = renderBuffer->zBuffer[zBufferIndex];
				if (pixelZValue > currZValue)
				{
					renderBuffer->zBuffer[zBufferIndex] = pixelZValue;
					u8 *texturePtr                      = texture->memory;
					const u32 texturePitch              = texture->bytesPerPixel * texture->dim.w;

					f32 barycentricB = ((f32 *)&barycentric)[2];
					f32 barycentricC = ((f32 *)&barycentric)[0];
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

#if PROCESS_COLOR_NO_SIMD
					DqnV4 color1;
					color1.a = (f32)(texel1 >> 24);
					color1.b = (f32)((texel1 >> 16) & 0xFF);
					color1.g = (f32)((texel1 >> 8) & 0xFF);
					color1.r = (f32)((texel1 >> 0) & 0xFF);
					color1 *= DTRRENDER_INV_255;
					color1      = DTRRender_SRGB1ToLinearSpaceV4(color1);
					DqnV4 blend = color * color1;
#else
					__m128 color1 =
					    _mm_set_ps((f32)(texel1 >> 24), (f32)((texel1 >> 16) & 0xFF),
					               (f32)((texel1 >> 8) & 0xFF), (f32)((texel1 >> 0) & 0xFF));
					color1 = _mm_mul_ps(color1, INV255_4X);
					color1 = _mm_mul_ps(color1, color1); // to linear space
					color1 = _mm_mul_ps(color1, colorModulate);

					DqnV4 blend = {};
					blend.r     = ((f32 *)&color1)[0];
					blend.g     = ((f32 *)&color1)[1];
					blend.b     = ((f32 *)&color1)[2];
					blend.a     = ((f32 *)&color1)[3];
#endif
					SetPixel(renderBuffer, bufferX1, bufferY, blend, ColorSpace_Linear);
				}
			}

			signedArea1 = _mm_add_ps(signedArea1, signedAreaPixelDeltaX);
			signedArea2 = _mm_add_ps(signedArea2, signedAreaPixelDeltaX);
		}

		signedAreaPixel1 = _mm_add_ps(signedAreaPixel1, signedAreaPixelDeltaY);
		signedAreaPixel2 = _mm_add_ps(signedAreaPixel2, signedAreaPixelDeltaY);
	}
}

void DTRRender_TexturedTriangle(DTRRenderBuffer *const renderBuffer, DqnV3 p1, DqnV3 p2, DqnV3 p3,
                                DqnV2 uv1, DqnV2 uv2, DqnV2 uv3, DTRBitmap *const texture,
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

	f32 area2Times = ((p2.x - p1.x) * (p2.y + p1.y)) +
	                 ((p3.x - p2.x) * (p3.y + p2.y)) +
	                 ((p1.x - p3.x) * (p1.y + p3.y));
	if (area2Times > 0)
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV3, p2, p3);
	}

	DTRDebug_BeginCycleCount(DTRDebugCycleCount_RenderTriangle_Rasterise);
	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	if (globalDTRPlatformFlags.canUseSSE2)
	{
		SIMDRasteriseTexturedTriangle(renderBuffer, p1, p2, p3, uv1, uv2, uv3, texture, color);
	}
	else
	{
		RasteriseTexturedTriangle(renderBuffer, p1, p2, p3, uv1, uv2, uv3, texture, color);
	}
	DTRDebug_EndCycleCount(DTRDebugCycleCount_RenderTriangle_Rasterise);

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
		screenVA.x = (f32)(i32)(screenVA.x + 0.5f);
		screenVA.y = (f32)(i32)(screenVA.y + 0.5f);
		screenVB.x = (f32)(i32)(screenVB.x + 0.5f);
		screenVB.y = (f32)(i32)(screenVB.y + 0.5f);
		screenVC.x = (f32)(i32)(screenVC.x + 0.5f);
		screenVC.y = (f32)(i32)(screenVC.y + 0.5f);

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

	f32 area2Times = ((p2.x - p1.x) * (p2.y + p1.y)) +
	                 ((p3.x - p2.x) * (p3.y + p2.y)) +
	                 ((p1.x - p3.x) * (p1.y + p3.y));
	if (area2Times > 0)
	{
		// Clockwise swap any point to make it clockwise
		DQN_SWAP(DqnV3, p2, p3);
	}

	const DqnV3 a = p1;
	const DqnV3 b = p2;
	const DqnV3 c = p3;

	DqnV2i startP = min;
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

	////////////////////////////////////////////////////////////////////////////
	// Scan and Render
	////////////////////////////////////////////////////////////////////////////
	const u32 zBufferPitch = renderBuffer->width;
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
				f32 pixelZValue = a.z + (barycentricB * (b.z - a.z)) + (barycentricC * (c.z - a.z));
				f32 currZValue  = renderBuffer->zBuffer[zBufferIndex];
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

	////////////////////////////////////////////////////////////////////////////
	// Debug
	////////////////////////////////////////////////////////////////////////////
	DTRDebug_CounterIncrement(DTRDebugCounter_RenderTriangle);
	if (DTR_DEBUG_RENDER)
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
