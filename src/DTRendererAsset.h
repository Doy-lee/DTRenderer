#ifndef DTRENDERER_ASSET_H
#define DTRENDERER_ASSET_H

#include "DTRendererPlatform.h"

#include "dqn.h"
#include "external/stb_truetype.h"

typedef struct DTRBitmap
{
	u8    *memory;
	DqnV2i dim;
	i32    bytesPerPixel;
} DTRBitmap;

typedef struct DTRMeshFace
{
	i32 *vertexIndex;
	u32 numVertexIndex;

	i32 *texIndex;
	u32 numTexIndex;

	i32 *normalIndex;
	u32  numNormalIndex;
} DTRMeshFace;

typedef struct DTRMesh
{
	DqnV4 *vertexes;
	u32    numVertexes;

	DqnV3 *texUV;
	u32    numTexUV;

	DqnV3 *normals;
	u32    numNormals;

	DTRMeshFace *faces;
	u32          numFaces;
	DTRBitmap    tex;
} DTRMesh;

typedef struct DTRFont
{
	u8    *bitmap;
	DqnV2i bitmapDim;
	DqnV2i codepointRange;
	f32    sizeInPt;

	stbtt_packedchar *atlas;
} DTRFont;

void DTRAsset_InitGlobalState ();
bool DTRAsset_LoadWavefrontObj(const PlatformAPI api, DqnMemStack *const memStack, DTRMesh *const mesh, const char *const path);
bool DTRAsset_LoadFontToBitmap(const PlatformAPI api, DqnMemStack *const memStack, DqnMemStack *const tmpMemStack, DTRFont *const font, const char *const path, const DqnV2i bitmapDim, const DqnV2i codepointRange, const f32 sizeInPt);
bool DTRAsset_LoadBitmap      (const PlatformAPI api, DqnMemStack *const memStack, DqnMemStack *const transMemStack, DTRBitmap *bitmap, const char *const path);
#endif
