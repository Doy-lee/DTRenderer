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

		DTRRender_Text(debug->renderBuffer, *debug->font, debug->displayP, str);
		debug->displayP.y += globalDebug.displayYOffset;
	}
}

FILE_SCOPE void PushMemBufferText(const char *const name,
                                  const DqnMemBuffer *const buffer)
{
	if (DTR_DEBUG)
	{
		if (!buffer) return;

		size_t totalUsed   = 0;
		size_t totalSize   = 0;
		size_t totalWasted = 0;
		i32 numBlocks      = 0;

		DqnMemBufferBlock *blockPtr = buffer->block;
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
		Dqn_sprintf(str, "%s: %d block(s): %_$lld/%_$lld", name, numBlocks,
		            totalUsed, totalSize);

		DTRRender_Text(globalDebug.renderBuffer, *globalDebug.font,
		               globalDebug.displayP, str);
		globalDebug.displayP.y += globalDebug.displayYOffset;
	}
}

void DTRDebug_Update(DTRState *const state,
                     PlatformRenderBuffer *const renderBuffer,
                     PlatformInput *const input, PlatformMemory *const memory)
{
	if (DTR_DEBUG)
	{
		DTRDebug *const debug = &globalDebug;

		debug->renderBuffer = renderBuffer;
		debug->font         = &state->font;
		if (debug->font->bitmap && debug->renderBuffer)
		{
			debug->displayYOffset = -(i32)(state->font.sizeInPt + 0.5f);
			DQN_ASSERT(globalDebug.displayYOffset < 0);
		}

		debug->totalSetPixels += debug->setPixelsPerFrame;
		debug->totalSetPixels = DQN_MAX(0, debug->totalSetPixels);

		// totalSetPixels
		{
			char str[128] = {};
			Dqn_sprintf(str, "%s: %'lld", "TotalSetPixels", debug->totalSetPixels);
			DTRRender_Text(debug->renderBuffer, *debug->font, debug->displayP, str);
			debug->displayP.y += globalDebug.displayYOffset;
		}

		// setPixelsPerFrame
		{
			char str[128] = {};
			Dqn_sprintf(str, "%s: %'lld", "SetPixelsPerFrame", debug->setPixelsPerFrame);
			DTRRender_Text(debug->renderBuffer, *debug->font, debug->displayP, str);
			debug->displayP.y += globalDebug.displayYOffset;
		}

		// memory
		{
			PushMemBufferText("PermBuffer", &memory->permanentBuffer);
			PushMemBufferText("TransBuffer", &memory->transientBuffer);
		}

		debug->setPixelsPerFrame = 0;
		debug->displayP =
			DqnV2_2i(0, debug->renderBuffer->height + globalDebug.displayYOffset);
	}
}


