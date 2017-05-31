#ifndef DTRENDERER_DEBUG_H
#define DTRENDERER_DEBUG_H

#include "dqn.h"

// NOTE: When DTR_DEBUG is 0, _ALL_ debug code is compiled out.
#define DTR_DEBUG 1

#if DTR_DEBUG
	#define DTR_DEBUG_RENDER 1

	#define DTR_DEBUG_PROFILING_EASY_PROFILER 0
	#if DTR_DEBUG_PROFILING_EASY_PROFILER
		#define BUILD_WITH_EASY_PROFILER 1
		#include "external/easy/profiler.h"

		#define DTR_DEBUG_EP_PROFILE_START() profiler::startListen()
		#define DTR_DEBUG_EP_PROFILE_END()   profiler::stopListen()

		#define DTR_DEBUG_EP_TIMED_BLOCK(name)           EASY_BLOCK(name)
		#define DTR_DEBUG_EP_TIMED_NONSCOPED_BLOCK(name) EASY_NONSCOPED_BLOCK(name)
		#define DTR_DEBUG_EP_TIMED_END_BLOCK()           EASY_END_BLOCK()
		#define DTR_DEBUG_EP_TIMED_FUNCTION()            EASY_FUNCTION()
	#else
		#define DTR_DEBUG_EP_PROFILE_START()
		#define DTR_DEBUG_EP_PROFILE_END()

		#define DTR_DEBUG_EP_TIMED_BLOCK(name)
		#define DTR_DEBUG_EP_TIMED_NONSCOPED_BLOCK(name)
		#define DTR_DEBUG_EP_TIMED_END_BLOCK()
		#define DTR_DEBUG_EP_TIMED_FUNCTION()
	#endif

	#define DTR_DEBUG_PROFILING 1
#endif

enum DTRDebugCounter
{
	DTRDebugCounter_SetPixels,
	DTRDebugCounter_RenderTriangle,
	DTRDebugCounter_Count,
};

enum DTRDebugCycleCount
{
	DTRDebugCycleCount_RenderTexturedTriangle_Rasterise,
	DTRDebugCycleCount_RenderTriangle_Rasterise,
	DTRDebugCycleCount_Count,
};

typedef struct DTRDebugCycles
{
	u64 totalCycles;
	u64 numInvokes;

	u64 tmpStartCycles; // Used to calculate the number of cycles elapsed
} DTRDebugCycles;

typedef struct DTRDebug
{
	struct DTRFont         *font;
	struct DTRRenderBuffer *renderBuffer;
	struct PlatformInput   *input;

	DqnV4 displayColor;
	DqnV2 displayP;
	i32   displayYOffset;

	DTRDebugCycles cycles [DTRDebugCycleCount_Count];
	u64            counter[DTRDebugCounter_Count];
	u64            totalSetPixels;
} DTRDebug;

extern DTRDebug globalDebug;

void        DTRDebug_TestMeshFaceAndVertexParser(struct DTRMesh *const mesh);
void        DTRDebug_DumpZBuffer                (struct DTRRenderBuffer *const renderBuffer, struct DqnMemStack *const transMemStack);
void        DTRDebug_PushText                   (const char *const formatStr, ...);
void        DTRDebug_Update                     (struct DTRState *const state, struct DTRRenderBuffer *const renderBuffer, struct PlatformInput *const input, struct PlatformMemory *const memory);
void inline DTRDebug_BeginCycleCount            (enum DTRDebugCycleCount tag);
void inline DTRDebug_EndCycleCount              (enum DTRDebugCycleCount tag);
void inline DTRDebug_CounterIncrement           (enum DTRDebugCounter tag);

#endif
