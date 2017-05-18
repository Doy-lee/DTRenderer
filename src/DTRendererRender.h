#ifndef DTRENDERER_RENDER_H
#define DTRENDERER_RENDER_H

#include "dqn.h"

typedef struct PlatformRenderBuffer PlatformRenderBuffer;
typedef struct DTRBitmap DTRBitmap;

typedef struct DTRRenderTransform
{
	f32 rotation = 0;               // Rotation in degrees
	DqnV2 anchor = DqnV2_1f(0.5f);  // Anchor has expected range of [0->1]
	DqnV2 scale  = DqnV2_1f(1.0f);
} DTRRenderTransform;

inline DTRRenderTransform DTRRender_DefaultTransform()
{
	DTRRenderTransform result = {};
	return result;
}

// NOTE: 0.33f is the midpoint of a triangle
inline DTRRenderTransform DTRRender_DefaultTriangleTransform()
{
	DTRRenderTransform result = {};
	result.anchor             = DqnV2_1f(0.33f);
	return result;
}

// NOTE(doyle): 1 & 255 suffix represent the range the color is being sent in.
// 255 means we expect that all the colors are from [0-255] and 1 means all the
// colors should be in the range of [0-1]
inline DqnV4 DTRRender_PreMultiplyAlpha1  (const DqnV4 color);
inline DqnV4 DTRRender_PreMultiplyAlpha255(const DqnV4 color);

// NOTE: All specified colors should be in the range of [0->1], rgba, respectively.
// Leaving len = -1 for text will make the system use strlen to determine len.
void DTRRender_Text     (PlatformRenderBuffer *const renderBuffer, const DTRFont font, DqnV2 pos,
                         const char *const text, DqnV4 color = DqnV4_1f(1), i32 len = -1);
void DTRRender_Line     (PlatformRenderBuffer *const renderBuffer, DqnV2i a, DqnV2i b, DqnV4 color);
void DTRRender_Rectangle(PlatformRenderBuffer *const renderBuffer, DqnV2 min, DqnV2 max,
                         DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTransform());
void DTRRender_Triangle (PlatformRenderBuffer *const renderBuffer, DqnV2 p1, DqnV2 p2, DqnV2 p3,
                         DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTriangleTransform());
void DTRRender_Bitmap   (PlatformRenderBuffer *const renderBuffer, DTRBitmap *const bitmap, DqnV2 pos,
                         const DTRRenderTransform transform = DTRRender_DefaultTransform());
void DTRRender_Clear    (PlatformRenderBuffer *const renderBuffer, const DqnV3 color);

#endif
