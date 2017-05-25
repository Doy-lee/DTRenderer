#ifndef DTRENDERER_ASSET_H
#define DTRENDERER_ASSET_H

#include "DTRendererPlatform.h"

#include "dqn.h"

#include "external/stb_truetype.h"

typedef struct DTRWavefModelFace
{
	DqnArray<i32> vertexIndexArray;
	DqnArray<i32> textureIndexArray;
	DqnArray<i32> normalIndexArray;
} DTRWavefModelFace;

typedef struct DTRWavefModel
{
	DqnArray<DqnV4> geometryArray;
	DqnArray<DqnV3> textureArray;
	DqnArray<DqnV3> normalArray;

	// TODO(doyle): Fixed size
	char *groupName[16];
	i32   groupNameIndex;
	i32   groupSmoothing;

	DqnArray<DTRWavefModelFace> faces;
} DTRWavefModel;

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

void DTRAsset_InitGlobalState ();
bool DTRAsset_WavefModelLoad  (const PlatformAPI api, PlatformMemory *const memory, const char *const path, DTRWavefModel *const obj);
bool DTRAsset_FontToBitmapLoad(const PlatformAPI api, PlatformMemory *const memory, DTRFont *const font, const char *const path, const DqnV2i bitmapDim, const DqnV2i codepointRange, const f32 sizeInPt);
bool DTRAsset_BitmapLoad      (const PlatformAPI api, DTRBitmap *bitmap, const char *const path, DqnMemStack *const transMemStack);
#endif
