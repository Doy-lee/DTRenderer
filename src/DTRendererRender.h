#ifndef DTRENDERER_RENDER_H
#define DTRENDERER_RENDER_H

#include "dqn.h"

typedef struct PlatformRenderBuffer PlatformRenderBuffer;
typedef struct DTRBitmap DTRBitmap;

typedef struct DTRRenderTransform
{
	f32 rotation = 0;
	DqnV2 anchor = DqnV2_1f(0.5f);
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

inline DqnV4 DTRRender_PreMultiplyAlpha(const DqnV4 color);

void DTRRender_Text     (PlatformRenderBuffer *const renderBuffer, const DTRFont font, DqnV2 pos,
                         const char *const text, DqnV4 color = DqnV4_4f(255, 255, 255, 255), i32 len = -1);
void DTRRender_Line     (PlatformRenderBuffer *const renderBuffer, DqnV2i a, DqnV2i b, DqnV4 color);
void DTRRender_Rectangle(PlatformRenderBuffer *const renderBuffer, DqnV2 min, DqnV2 max,
                         DqnV4 color, const DqnV2 scale = DqnV2_1f(1.0f), const f32 rotation = 0, const DqnV2 anchor = DqnV2_1f(0.5f));
void DTRRender_Triangle (PlatformRenderBuffer *const renderBuffer, DqnV2 p1, DqnV2 p2, DqnV2 p3,
                         DqnV4 color, const DqnV2 scale = DqnV2_1f(1.0f), const f32 rotation = 0, const DqnV2 anchor = DqnV2_1f(0.33f));
void DTRRender_Bitmap   (PlatformRenderBuffer *const renderBuffer, DTRBitmap *const bitmap, DqnV2i pos,
                         DTRRenderTransform transform = DTRRender_DefaultTransform());
void DTRRender_Clear    (PlatformRenderBuffer *const renderBuffer, const DqnV3 color);

#endif