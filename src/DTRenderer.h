#ifndef DTRENDERER_H
#define DTRENDERER_H

#include "dqn.h"
#include "external/stb_truetype.h"

typedef void DTR_UpdateFunction(struct PlatformRenderBuffer *const renderBuffer,
                                struct PlatformInput        *const input,
                                struct PlatformMemory       *const memory);

typedef struct DTRFont
{
	u8    *bitmap;
	DqnV2i bitmapDim;
	DqnV2i codepointRange;
	f32    sizeInPt;

	stbtt_packedchar *atlas;
} DTRFont;

typedef struct DTRBitmap
{
	u8    *memory;
	DqnV2i dim;
	i32    bytesPerPixel;
} DTRBitmap;

typedef struct DTRState
{
	DTRFont   font;
	DTRBitmap bitmap;
} DTRState;
#endif
