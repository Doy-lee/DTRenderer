#include "DTRendererDebug.h"
#include "DTRenderer.h"
#include "DTRendererAsset.h"
#include "DTRendererPlatform.h"
#include "DTRendererRender.h"

#include "dqn.h"

#include "external/tests/tinyrenderer/geometry.h"
#include "external/tests/tinyrenderer/model.cpp"
#include <vector>

DTRDebug globalDebug;
void DTRDebug_TestMeshFaceAndVertexParser(DTRMesh *const mesh)
{
	if (DTR_DEBUG)
	{
		if (!mesh) DQN_ASSERT(DQN_INVALID_CODE_PATH);
		Model model = Model("african_head.obj");

		DQN_ASSERT((i64)mesh->numFaces == model.nfaces());
		for (i32 i = 0; i < model.nfaces(); i++)
		{
			std::vector<i32> correctFace = model.face(i);
			DTRMeshFace *myFace          = &mesh->faces[i];

			DQN_ASSERT(myFace->numVertexIndex == correctFace.size());

			for (i32 j = 0; j < (i32)myFace->numVertexIndex; j++)
			{
				// Ensure the vertex index references are correct per face
				DQN_ASSERT(myFace->vertexIndex[j] == correctFace[j]);

				Vec3f tmp           = model.vert(correctFace[j]);
				DqnV3 correctVertex = DqnV3_3f(tmp[0], tmp[1], tmp[2]);
				DqnV3 myVertex      = (mesh->vertexes[myFace->vertexIndex[j]]).xyz;

				// Ensure the vertex values read are correct
				for (i32 k = 0; k < DQN_ARRAY_COUNT(correctVertex.e); k++)
				{
					f32 delta = DQN_ABS(correctVertex.e[k] - myVertex.e[k]);
					DQN_ASSERT(delta < 0.1f);
				}
			}
		}
	}
}

void DTRDebug_DumpZBuffer(DTRRenderBuffer *const renderBuffer, DqnMemStack *const tempStack)
{
	if (DTR_DEBUG)
	{
		PlatformAPI *const api = &globalDebug.input->api;
		PlatformFile file      = {};

		u32 permissions   = (PlatformFilePermissionFlag_Read | PlatformFilePermissionFlag_Write);
		if (!api->FileOpen("zBufferDump.txt", &file, permissions,
		                   PlatformFileAction_CreateIfNotExist))
		{
			if (!api->FileOpen("zBufferDump.txt", &file, permissions,
			                   PlatformFileAction_ClearIfExist))
			{
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}
		}

		DqnTempMemStack tmpMemRegion = DqnMemStack_BeginTempRegion(tempStack);

		size_t bufSize = DQN_MEGABYTE(16);
		char *bufString = (char *)DqnMemStack_Push(tempStack, bufSize);
		char *bufPtr    = bufString;
		for (i32 i = 0; i < renderBuffer->width * renderBuffer->height; i++)
		{

			f32 zValue = renderBuffer->zBuffer[i];
			if (zValue == DQN_F32_MIN) continue;
			i32 chWritten = Dqn_sprintf(bufPtr, "index %06d: %05.5f\n", i, zValue);
			if ((bufPtr + chWritten) > (bufString + bufSize))
			{
				size_t bufPtrAddr    = (size_t)(bufPtr + chWritten);
				size_t bufStringAddr = (size_t)(bufString + bufSize);
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}
			bufPtr += chWritten;
		}

		size_t writeSize    = (size_t)bufPtr - (size_t)bufString;
		size_t bytesWritten = api->FileWrite(&file, (u8 *)bufString, writeSize);
		api->FileClose(&file);

		DqnMemStack_EndTempRegion(tmpMemRegion);
	}
}

void DTRDebug_PushText(const char *const formatStr, ...)
{
	if (DTR_DEBUG)
	{
		DTRDebug *const debug = &globalDebug;
		if (!debug->renderBuffer) return;

		char str[1024] = {};

		va_list argList;
		va_start(argList, formatStr);
		{
			i32 numCopied = Dqn_vsprintf(str, formatStr, argList);
			DQN_ASSERT(numCopied < DQN_ARRAY_COUNT(str));
		}
		va_end(argList);

		DTRRender_Text(debug->renderBuffer, *debug->font, debug->displayP, str,
		               debug->displayColor);
		debug->displayP.y += globalDebug.displayYOffset;
	}
}

#pragma warning(push)
#pragma warning(disable: 4127)
void inline DTRDebug_BeginCycleCount(char *title, enum DTRDebugCycleCount tag)
{
	if (DTR_DEBUG && DTR_DEBUG_PROFILING)
	{
		if (globalDTRPlatformFlags.canUseRdtsc)
		{
			DTRDebugCycles *const cycles = &globalDebug.cycles[tag];
			cycles->tmpStartCycles       = __rdtsc();
			cycles->numInvokes++;

			if (!cycles->name)
			{
				cycles->name = (char *)DqnMemStack_Push(&globalDebug.memStack, 128 * sizeof(char));
				if (cycles->name) Dqn_sprintf(cycles->name, "%s", title);
			}
		}
	}
}

void inline DTRDebug_EndCycleCount(enum DTRDebugCycleCount tag)
{
	if (DTR_DEBUG && DTR_DEBUG_PROFILING)
	{
		if (globalDTRPlatformFlags.canUseRdtsc)
		{
			DTRDebugCycles *const cycles = &globalDebug.cycles[tag];
			cycles->totalCycles += __rdtsc() - cycles->tmpStartCycles;
		}
	}
}
#pragma warning(pop)

FILE_SCOPE void PushMemStackText(const char *const name, const DqnMemStack *const stack)
{
	if (DTR_DEBUG)
	{
		if (!stack) return;

		size_t totalUsed   = 0;
		size_t totalSize   = 0;
		size_t totalWasted = 0;
		i32 numBlocks      = 0;

		DqnMemStackBlock *blockPtr = stack->block;
		size_t freeSizeOfCurrBlock  = (blockPtr) ? blockPtr->size - blockPtr->used : 0;
		while (blockPtr)
		{
			totalUsed += blockPtr->used;
			totalSize += blockPtr->size;
			blockPtr = blockPtr->prevBlock;
			numBlocks++;
		}

		size_t totalUsedKb   = totalUsed / 1024;
		size_t totalSizeKb   = totalSize / 1024;
		size_t totalWastedKb = (totalSize - totalUsed - freeSizeOfCurrBlock) / 1024;

		char str[128] = {};
		Dqn_sprintf(str, "%s: %d block(s): %_$lld/%_$lld: wasted: %_$lld", name, numBlocks,
		            totalUsed, totalSize, totalWastedKb);

		DTRRender_Text(globalDebug.renderBuffer, *globalDebug.font,
		               globalDebug.displayP, str, globalDebug.displayColor);
		globalDebug.displayP.y += globalDebug.displayYOffset;
	}
}

void DTRDebug_Update(DTRState *const state,
                     DTRRenderBuffer *const renderBuffer,
                     PlatformInput *const input, PlatformMemory *const memory)
{
	if (DTR_DEBUG)
	{
		DTRDebug *const debug = &globalDebug;

		debug->renderBuffer = renderBuffer;
		debug->input        = input;
		debug->font         = &state->font;
		debug->displayColor = DqnV4_4f(1, 1, 1, 1);
		if (debug->font->bitmap && debug->renderBuffer)
		{
			debug->displayYOffset = -(i32)(state->font.sizeInPt + 0.5f);
			DQN_ASSERT(globalDebug.displayYOffset < 0);
		}

		debug->totalSetPixels += debug->counter[DTRDebugCounter_SetPixels];
		debug->totalSetPixels = DQN_MAX(0, debug->totalSetPixels);

		// memory
		{
			PushMemStackText("MainStack", &memory->mainStack);
			PushMemStackText("TempStack", &memory->tempStack);
			PushMemStackText("AssetStack", &memory->assetStack);
			PushMemStackText("DebugStack", &debug->memStack);
		}

		DTRDebug_PushText("Mouse: %d, %d", input->mouse.x, input->mouse.y);
		DTRDebug_PushText("MouseLBtn: %s", (input->mouse.leftBtn.endedDown) ? "true" : "false");
		DTRDebug_PushText("MouseRBtn: %s", (input->mouse.rightBtn.endedDown) ? "true" : "false");
		DTRDebug_PushText("");

		DTRDebug_PushText("SSE2Support: %s", (globalDTRPlatformFlags.canUseSSE2) ? "true" : "false");
		DTRDebug_PushText("RDTSCSupport: %s", (globalDTRPlatformFlags.canUseRdtsc) ? "true" : "false");
		DTRDebug_PushText("");

		DTRDebug_PushText("TotalSetPixels: %'lld",    debug->totalSetPixels);
		DTRDebug_PushText("SetPixelsPerFrame: %'lld", debug->counter[DTRDebugCounter_SetPixels]);
		DTRDebug_PushText("TrianglesRendered: %'lld", debug->counter[DTRDebugCounter_RenderTriangle]);
		DTRDebug_PushText("");

		DTRDebugCycles emptyDebugCycles = {};
		for (i32 i = 0; i < DQN_ARRAY_COUNT(debug->cycles); i++)
		{
			DTRDebugCycles *const cycles = &globalDebug.cycles[i];

			u64 invocations = (cycles->numInvokes == 0) ? 1 : cycles->numInvokes;
			u64 avgCycles   = cycles->totalCycles / invocations;

			if (avgCycles > 0)
			{
				DTRDebug_PushText("%d:%s: %'lld avg cycles", i, cycles->name, avgCycles);
			}
			cycles->name = NULL;

			// *cycles = emptyDebugCycles;
		}
		DTRDebug_PushText("");


		////////////////////////////////////////////////////////////////////////
		// End Debug Update
		////////////////////////////////////////////////////////////////////////
		debug->displayP =
			DqnV2_2i(0, debug->renderBuffer->height + globalDebug.displayYOffset);

		for (i32 i = 0; i < DQN_ARRAY_COUNT(debug->counter); i++)
			debug->counter[i] = 0;
	}
}

void inline DTRDebug_CounterIncrement(enum DTRDebugCounter tag)
{
	if (DTR_DEBUG)
	{
		DQN_ASSERT(tag >= 0 && tag < DTRDebugCounter_Count);
		globalDebug.counter[tag]++;
	}
}

