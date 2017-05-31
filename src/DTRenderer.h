#ifndef DTRENDERER_H
#define DTRENDERER_H

#include "DTRendererAsset.h"
#include "dqn.h"

typedef void DTR_UpdateFunction(struct PlatformRenderBuffer *const renderBuffer,
                                struct PlatformInput        *const input,
                                struct PlatformMemory       *const memory);

typedef struct DTRState
{
	DTRFont   font;
	DTRBitmap bitmap;
	DTRMesh   mesh;
} DTRState;

extern PlatformFlags globalDTRPlatformFlags;
#endif
