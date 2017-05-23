#ifndef DTRENDERER_H
#define DTRENDERER_H

#include "dqn.h"
#include "external/stb_truetype.h"

typedef void DTR_UpdateFunction(struct PlatformRenderBuffer *const renderBuffer,
                                struct PlatformInput        *const input,
                                struct PlatformMemory       *const memory);

typedef struct WavefrontModelFace
{
	DqnArray<i32> vertexArray;
	DqnArray<i32> textureArray;
	DqnArray<i32> normalArray;
} WavefrontModelFace;

typedef struct WavefrontModel
{
	// TODO(doyle): Fixed size
	char *groupName[16];
	i32 groupNameIndex;
	i32 groupSmoothing;

	DqnArray<WavefrontModelFace> faces;
} WavefrontModel;

typedef struct WavefrontObj
{
	DqnArray<DqnV4> geometryArray;
	DqnArray<DqnV3> texUVArray;
	DqnArray<DqnV3> normalArray;

	WavefrontModel model;
} WavefrontObj;

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
	DTRFont           font;
	DTRBitmap         bitmap;
	WavefrontObj obj;
} DTRState;
#endif
