#include "DTRendererDebug.h"
#include "DTRenderer.h"
#include "DTRendererPlatform.h"
#include "DTRendererRender.h"

DTRDebug globalDebug;

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

FILE_SCOPE void PushMemStackText(const char *const name,
                                  const DqnMemStack *const stack)
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
		debug->font         = &state->font;
		debug->displayColor = DqnV4_4f(1, 1, 1, 1);
		if (debug->font->bitmap && debug->renderBuffer)
		{
			debug->displayYOffset = -(i32)(state->font.sizeInPt + 0.5f);
			DQN_ASSERT(globalDebug.displayYOffset < 0);
		}

		debug->totalSetPixels += debug->counter[DTRDebugCounter_SetPixels];
		debug->totalSetPixels = DQN_MAX(0, debug->totalSetPixels);

		DTRDebug_PushText("TotalSetPixels: %'lld",    debug->totalSetPixels);
		DTRDebug_PushText("SetPixelsPerFrame: %'lld", debug->counter[DTRDebugCounter_SetPixels]);
		DTRDebug_PushText("TrianglesRendered: %'lld", debug->counter[DTRDebugCounter_RenderTriangle]);
		DTRDebug_PushText("");

		// memory
		{
			PushMemStackText("PermBuffer", &memory->permMemStack);
			PushMemStackText("TransBuffer", &memory->transMemStack);
		}

		DTRDebug_PushText("SSE2Support: %s", (input->canUseSSE2) ? "true" : "false");

		debug->displayP =
			DqnV2_2i(0, debug->renderBuffer->height + globalDebug.displayYOffset);

		for (i32 i = 0; i < DQN_ARRAY_COUNT(debug->counter); i++)
			debug->counter[i] = 0;
	}
}


