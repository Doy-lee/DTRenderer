#ifndef DTRENDERER_DEBUG_H
#define DTRENDERER_DEBUG_H

#include "dqn.h"

// NOTE: When DTR_DEBUG is 0, _ALL_ debug code is compiled out.
#define DTR_DEBUG 1
#define DTR_DEBUG_RENDER 1

// For inbuilt profiling DTRDebug_BeginCycleCount .. etc
#define DTR_DEBUG_PROFILING 1

#define DTR_DEBUG_PROFILING_EASY_PROFILER 0
#if DTR_DEBUG_PROFILING_EASY_PROFILER
	#define BUILD_WITH_EASY_PROFILER 0
	#include "external/easy/profiler.h"
	
	#define DTR_DEBUG_EP_PROFILE_START() profiler::startListen()
	#define DTR_DEBUG_EP_PROFILE_END() profiler::stopListen()
	
	#define DTR_DEBUG_EP_TIMED_BLOCK(name) EASY_BLOCK(name)
	#define DTR_DEBUG_EP_TIMED_NONSCOPED_BLOCK(name) EASY_NONSCOPED_BLOCK(name)
	#define DTR_DEBUG_EP_TIMED_END_BLOCK() EASY_END_BLOCK
	#define DTR_DEBUG_EP_TIMED_FUNCTION() EASY_FUNCTION()
#else
	#define DTR_DEBUG_EP_PROFILE_START()
	#define DTR_DEBUG_EP_PROFILE_END()
	
	#define DTR_DEBUG_EP_TIMED_BLOCK(name)
	#define DTR_DEBUG_EP_TIMED_NONSCOPED_BLOCK(name)
	#define DTR_DEBUG_EP_TIMED_END_BLOCK()
	#define DTR_DEBUG_EP_TIMED_FUNCTION()
#endif

enum DTRDebugCounter
{
	DTRDebugCounter_SetPixels,
	DTRDebugCounter_RenderTriangle,
	DTRDebugCounter_Count,
};

enum DTRDebugCycleCount
{
	DTRDebugCycleCount_DTR_Update,
	DTRDebugCycleCount_DTR_Update_RenderModel,
	DTRDebugCycleCount_DTR_Update_RenderPrimitiveTriangles,

	DTRDebugCycleCount_SIMDTexturedTriangle,
	DTRDebugCycleCount_SIMDTexturedTriangle_Preamble,
	DTRDebugCycleCount_SIMDTexturedTriangle_Preamble_SArea,
	DTRDebugCycleCount_SIMDTexturedTriangle_Preamble_SIMDStep,
	DTRDebugCycleCount_SIMDTexturedTriangle_Rasterise,
	DTRDebugCycleCount_SIMDTexturedTriangle_RasterisePixel,
	DTRDebugCycleCount_SIMDTexturedTriangle_SampleTexture,

	DTRDebugCycleCount_SIMDTriangle,
	DTRDebugCycleCount_SIMDTriangle_Preamble,
	DTRDebugCycleCount_SIMDTriangle_Preamble_SArea,
	DTRDebugCycleCount_SIMDTriangle_Preamble_SIMDStep,
	DTRDebugCycleCount_SIMDTriangle_Rasterise,
	DTRDebugCycleCount_SIMDTriangle_RasterisePixel,

	DTRDebugCycleCount_SlowTexturedTriangle,
	DTRDebugCycleCount_SlowTexturedTriangle_Preamble,
	DTRDebugCycleCount_SlowTexturedTriangle_Preamble_SArea,
	DTRDebugCycleCount_SlowTexturedTriangle_Preamble_SIMDStep,
	DTRDebugCycleCount_SlowTexturedTriangle_Rasterise,
	DTRDebugCycleCount_SlowTexturedTriangle_RasterisePixel,
	DTRDebugCycleCount_SlowTexturedTriangle_SampleTexture,

	DTRDebugCycleCount_SlowTriangle,
	DTRDebugCycleCount_SlowTriangle_Preamble,
	DTRDebugCycleCount_SlowTriangle_Preamble_SArea,
	DTRDebugCycleCount_SlowTriangle_Preamble_SIMDStep,
	DTRDebugCycleCount_SlowTriangle_Rasterise,
	DTRDebugCycleCount_SlowTriangle_RasterisePixel,
	DTRDebugCycleCount_Count,
};

typedef struct DTRDebugCycles
{
	char *name;
	u64 totalCycles;
	u64 numInvokes;

	u64 tmpStartCycles; // Used to calculate the number of cycles elapsed
} DTRDebugCycles;

typedef struct DTRDebug
{
	struct DTRFont          *font;
	struct DTRRenderContext *renderContext;
	struct PlatformInput    *input;
	DqnMemStack              memStack;

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
void        DTRDebug_RunTinyRenderer            ();
void        DTRDebug_PushText                   (const char *const formatStr, ...);
void        DTRDebug_Update                     (struct DTRState *const state, struct DTRRenderBuffer *const renderBuffer, struct PlatformInput *const input, struct PlatformMemory *const memory);
void inline DTRDebug_BeginCycleCount            (char *title, enum DTRDebugCycleCount tag);
void inline DTRDebug_EndCycleCount              (enum DTRDebugCycleCount tag);
void inline DTRDebug_CounterIncrement           (enum DTRDebugCounter tag);

#endif
