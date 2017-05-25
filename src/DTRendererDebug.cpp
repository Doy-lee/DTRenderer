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
void DTRDebug_TestWavefFaceAndVertexParser(DTRWavefModel *const obj)
{
	if (DTR_DEBUG)
	{
		if (!obj) DQN_ASSERT(DQN_INVALID_CODE_PATH);
		Model model = Model("african_head.obj");

		DQN_ASSERT(obj->faces.count == model.nfaces());
		for (i32 i = 0; i < model.nfaces(); i++)
		{
			std::vector<i32> correctFace = model.face(i);
			DTRWavefModelFace *myFace    = &obj->faces.data[i];

			DQN_ASSERT(myFace->vertexIndexArray.count == correctFace.size());

			for (i32 j = 0; j < myFace->vertexIndexArray.count; j++)
			{
				// Ensure the vertex index references are correct per face
				DQN_ASSERT(myFace->vertexIndexArray.data[j] == correctFace[j]);

				Vec3f tmp           = model.vert(correctFace[j]);
				DqnV3 correctVertex = DqnV3_3f(tmp[0], tmp[1], tmp[2]);
				DqnV3 myVertex = (obj->geometryArray.data[myFace->vertexIndexArray.data[j]]).xyz;

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

void DTRDebug_DumpZBuffer(DTRRenderBuffer *const renderBuffer, DqnMemStack *const transMemStack)
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

		DqnTempMemStack tmpMemRegion = DqnMemStack_BeginTempRegion(transMemStack);

		size_t bufSize = DQN_MEGABYTE(16);
		char *bufString = (char *)DqnMemStack_Push(transMemStack, bufSize);
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

void inline DTRDebug_BeginCycleCount(enum DTRDebugCycleCount tag)
{
	if (DTR_DEBUG_PROFILING)
	{
		if (globalDebug.input && globalDebug.input->canUseRdtsc)
		{
			globalDebug.cycleCount[tag] = __rdtsc();
		}
	}
}

void inline DTRDebug_EndCycleCount(enum DTRDebugCycleCount tag)
{
	if (DTR_DEBUG_PROFILING)
	{
		if (globalDebug.input && globalDebug.input->canUseRdtsc)
		{
			globalDebug.cycleCount[tag] = __rdtsc() - globalDebug.cycleCount[tag];
		}
	}
}

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
		while (blockPtr)
		{
			totalUsed += blockPtr->used;
			totalSize += blockPtr->size;
			blockPtr = blockPtr->prevBlock;
			numBlocks++;
		}

		size_t totalUsedKb   = totalUsed / 1024;
		size_t totalSizeKb   = totalSize / 1024;
		size_t totalWastedKb = totalWasted / 1024;

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
			PushMemStackText("PermBuffer", &memory->permMemStack);
			PushMemStackText("TransBuffer", &memory->transMemStack);
		}

		DTRDebug_PushText("Mouse: %d, %d", input->mouse.x, input->mouse.y);
		DTRDebug_PushText("MouseLBtn: %s", (input->mouse.leftBtn.endedDown) ? "true" : "false");
		DTRDebug_PushText("MouseRBtn: %s", (input->mouse.rightBtn.endedDown) ? "true" : "false");
		DTRDebug_PushText("");

		DTRDebug_PushText("SSE2Support: %s", (input->canUseSSE2) ? "true" : "false");
		DTRDebug_PushText("RDTSCSupport: %s", (input->canUseRdtsc) ? "true" : "false");
		DTRDebug_PushText("");

		DTRDebug_PushText("TotalSetPixels: %'lld",    debug->totalSetPixels);
		DTRDebug_PushText("SetPixelsPerFrame: %'lld", debug->counter[DTRDebugCounter_SetPixels]);
		DTRDebug_PushText("TrianglesRendered: %'lld", debug->counter[DTRDebugCounter_RenderTriangle]);
		DTRDebug_PushText("");

		for (i32 i = 0; i < DQN_ARRAY_COUNT(debug->cycleCount); i++)
		{
			DTRDebug_PushText("%d: %'lld cycles", i, debug->cycleCount[i]);
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

