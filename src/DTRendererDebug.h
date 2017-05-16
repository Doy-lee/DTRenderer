#ifndef DTRENDERER_DEBUG_H
#define DTRENDERER_DEBUG_H

#include "dqn.h"

#define DTR_DEBUG 1

typedef struct PlatformRenderBuffer PlatformRenderBuffer;
typedef struct DTRFont              DTRFont;
typedef struct DTRState             DTRState;
typedef struct PlatformInput        PlatformInput;
typedef struct PlatformMemory       PlatformMemory;

typedef struct DTRDebug
{
	DTRFont              *font;
	PlatformRenderBuffer *renderBuffer;

	DqnV2 displayP;
	i32   displayYOffset;

	u64 setPixelsPerFrame;
	u64 totalSetPixels;
} DTRDebug;

extern DTRDebug globalDebug;

void DTRDebug_PushText(const char *const formatStr, ...);
void DTRDebug_Update(DTRState *const state,
                     PlatformRenderBuffer *const renderBuffer,
                     PlatformInput *const input, PlatformMemory *const memory);
#endif
