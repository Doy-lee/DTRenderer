#ifndef DTRENDERER_RENDER_H
#define DTRENDERER_RENDER_H

#include "dqn.h"
#include "DTRendererPlatform.h"

#define DTRRENDER_INV_255 1.0f/255.0f

typedef struct DTRBitmap DTRBitmap;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct DTRRenderBuffer
{
	i32 width;
	i32 height;
	i32 bytesPerPixel;
	PlatformLock *renderLock;
	volatile u8  *memory;     // Format: XX RR GG BB, and has (width * height * bytesPerPixels) elements
	volatile f32 *zBuffer;    // zBuffer has (width * height) elements

	volatile bool *pixelLockTable; // has (width * height) elements

} DTRRenderBuffer;

// Using transforms for 2D ignores the 'z' element.
typedef struct DTRRenderTransform
{
	f32 rotation = 0;               // Rotation in degrees
	DqnV3 anchor = DqnV3_1f(0.5f);  // Anchor has expected range of [0->1]
	DqnV3 scale  = DqnV3_1f(1.0f);
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
	result.anchor             = DqnV3_1f(0.33f);
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
enum DTRRenderShadingMode
{
	DTRRenderShadingMode_FullBright,
	DTRRenderShadingMode_Flat,
	DTRRenderShadingMode_Gouraud,
};

// DTRRenderLight is used for specifying an individual light for illuminating meshes.
// vector: The light direction, it does NOT need to be normalised. Will be normalise for you.
typedef struct DTRRenderLight
{
	enum DTRRenderShadingMode mode;
	DqnV3 vector;
	DqnV4 color;
} DTRRenderLight;

typedef struct DTRRenderContext
{
	DTRRenderBuffer  *renderBuffer;
	DqnMemStack      *tempStack;
	PlatformAPI      *api;
	PlatformJobQueue *jobQueue;
} DTRRenderContext;

// NOTE: All colors should be in the range of [0->1] where DqnV4 is a struct with 4 floats, rgba
// Leaving len = -1 for text will make the system use strlen to determine len.
void DTRRender_Text            (DTRRenderContext context, const DTRFont font, DqnV2 pos, const char *const text, DqnV4 color = DqnV4_1f(1), i32 len = -1);
void DTRRender_Line            (DTRRenderContext context, DqnV2i a, DqnV2i b, DqnV4 color);
void DTRRender_Rectangle       (DTRRenderContext context, DqnV2 min, DqnV2 max, DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTransform());
void DTRRender_Mesh            (DTRRenderContext context, PlatformJobQueue *const jobQueue, DTRMesh *const mesh, DTRRenderLight lighting, const DqnV3 pos, const DTRRenderTransform transform);
void DTRRender_Triangle        (DTRRenderContext context, DqnV3 p1, DqnV3 p2, DqnV3 p3, DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTriangleTransform());
void DTRRender_TexturedTriangle(DTRRenderContext context, DqnV3 p1, DqnV3 p2, DqnV3 p3, DqnV2 uv1, DqnV2 uv2, DqnV2 uv3, DTRBitmap *const texture, DqnV4 color, const DTRRenderTransform transform = DTRRender_DefaultTriangleTransform());
void DTRRender_Bitmap          (DTRRenderContext context, DTRBitmap *const bitmap, DqnV2 pos, const DTRRenderTransform transform = DTRRender_DefaultTransform(), DqnV4 color = DqnV4_4f(1, 1, 1, 1));
void DTRRender_Clear           (DTRRenderContext context, DqnV3 color);

#endif
