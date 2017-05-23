#ifndef DTRENDERER_RENDER_H
#define DTRENDERER_RENDER_H

#include "dqn.h"

#define DTRRENDER_INV_255 1.0f/255.0f

typedef struct DTRRenderBuffer DTRRenderBuffer;
typedef struct DTRBitmap DTRBitmap;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct DTRRenderBuffer
{
	i32 width;
	i32 height;
	i32 bytesPerPixel;

	u8  *memory;   // Format: XX RR GG BB, and has (width * height * bytesPerPixels) elements
	f32 *zBuffer;  // zBuffer has (width * height) elements

} DTRRenderBuffer;

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

// NOTE(doyle): 1 suffix represents the range the color is being sent in which
// means all the colors should be in the range of [0-1]
inline f32   DTRRender_SRGB1ToLinearSpacef (f32 val);
inline DqnV4 DTRRender_SRGB1ToLinearSpaceV4(DqnV4 color);
inline f32   DTRRender_LinearToSRGB1Spacef (f32 val);
inline DqnV4 DTRRender_LinearToSRGB1SpaceV4(DqnV4 color);

// Takes SRGB in [0->1], converts to linear space, premultiplies alpha and returns
// color back in SRGB space.
inline DqnV4 DTRRender_PreMultiplyAlphaSRGB1WithLinearConversion(DqnV4 color);

////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering
////////////////////////////////////////////////////////////////////////////////////////////////////
// NOTE: All colors should be in the range of [0->1] where DqnV4 is a struct with 4 floats, rgba
// Leaving len = -1 for text will make the system use strlen to determine len.
void DTRRender_Text     (DTRRenderBuffer *const renderBuffer, const DTRFont font, DqnV2 pos, const char *const text, DqnV4 color = DqnV4_1f(1), i32 len = -1);
void DTRRender_Line     (DTRRenderBuffer *const renderBuffer, DqnV2i a, DqnV2i b, DqnV4 color);
void DTRRender_Rectangle(DTRRenderBuffer *const renderBuffer, DqnV2 min, DqnV2 max, DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTransform());
void DTRRender_Triangle (DTRRenderBuffer *const renderBuffer, DqnV3 p1, DqnV3 p2, DqnV3 p3, DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTriangleTransform());
void DTRRender_Bitmap   (DTRRenderBuffer *const renderBuffer, DTRBitmap *const bitmap, DqnV2 pos, const DTRRenderTransform transform = DTRRender_DefaultTransform(), DqnV4 color = DqnV4_4f(1, 1, 1, 1));
void DTRRender_Clear    (DTRRenderBuffer *const renderBuffer, DqnV3 color);

#endif
