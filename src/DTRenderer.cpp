#include "DTRenderer.h"
#include "DTRendererDebug.h"
#include "DTRendererPlatform.h"
#include "DTRendererRender.h"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#define DQN_IMPLEMENTATION
#include "dqn.h"

#include <math.h>

FILE_SCOPE bool BitmapFontCreate(const PlatformAPI api,
                                 PlatformMemory *const memory,
                                 DTRFont *const font, const char *const path,
                                 const DqnV2i bitmapDim,
                                 const DqnV2i codepointRange,
                                 const f32 sizeInPt)
{
	if (!memory || !font || !path) return false;

	DTRFont loadedFont = {};
	loadedFont.bitmapDim      = bitmapDim;
	loadedFont.codepointRange = codepointRange;
	loadedFont.sizeInPt       = sizeInPt;

	////////////////////////////////////////////////////////////////////////////
	// Load font data
	////////////////////////////////////////////////////////////////////////////
	PlatformFile file = {};
	if (!api.FileOpen(path, &file, PlatformFilePermissionFlag_Read))
		return false; // TODO(doyle): Logging

	DqnTempBuffer tmpMemRegion = DqnMemBuffer_BeginTempRegion(&memory->transientBuffer);
	u8 *fontBuf                = (u8 *)DqnMemBuffer_Allocate(&memory->transientBuffer, file.size);
	size_t bytesRead           = api.FileRead(&file, fontBuf, file.size);
	api.FileClose(&file);
	if (bytesRead != file.size)
	{
		// TODO(doyle): Logging
		DqnMemBuffer_EndTempRegion(tmpMemRegion);
		return false;
	}

	stbtt_fontinfo fontInfo = {};
	if (stbtt_InitFont(&fontInfo, fontBuf, 0) == 0)
	{
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
		return false;
	}

	if (DTR_DEBUG) DQN_ASSERT(stbtt_GetNumberOfFonts(fontBuf) == 1);
	////////////////////////////////////////////////////////////////////////////
	// Pack font data to bitmap
	////////////////////////////////////////////////////////////////////////////
	loadedFont.bitmap = (u8 *)DqnMemBuffer_Allocate(
	    &memory->permanentBuffer,
	    (size_t)(loadedFont.bitmapDim.w * loadedFont.bitmapDim.h));

	stbtt_pack_context fontPackContext = {};
	if (stbtt_PackBegin(&fontPackContext, loadedFont.bitmap, bitmapDim.w,
	                    bitmapDim.h, 0, 1, NULL) == 1)
	{
		// stbtt_PackSetOversampling(&fontPackContext, 2, 2);

		i32 numCodepoints =
		    (i32)((codepointRange.max + 1) - codepointRange.min);

		loadedFont.atlas = (stbtt_packedchar *)DqnMemBuffer_Allocate(
		    &memory->permanentBuffer, numCodepoints * sizeof(stbtt_packedchar));
		stbtt_PackFontRange(&fontPackContext, fontBuf, 0,
		                    STBTT_POINT_SIZE(sizeInPt), (i32)codepointRange.min,
		                    numCodepoints, loadedFont.atlas);
		stbtt_PackEnd(&fontPackContext);
	}
	else
	{
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
		return false;
	}

	////////////////////////////////////////////////////////////////////////////
	// Premultiply Alpha of Bitmap
	////////////////////////////////////////////////////////////////////////////
	for (i32 y = 0; y < bitmapDim.h; y++)
	{
		for (i32 x = 0; x < bitmapDim.w; x++)
		{
			// NOTE: Bitmap from stb_truetype is 1BPP. So the actual color
			// value represents its' alpha value but also its' color.
			u32 index            = x + (y * bitmapDim.w);
			f32 alpha            = (f32)(loadedFont.bitmap[index]) / 255.0f;
			f32 color            = alpha;
			f32 preMulAlphaColor = color * alpha;
			DQN_ASSERT(preMulAlphaColor >= 0.0f && preMulAlphaColor <= 255.0f);

			loadedFont.bitmap[index] = (u8)(preMulAlphaColor * 255.0f);
		}
	}

#ifdef DTR_DEBUG_RENDER_FONT_BITMAP
	stbi_write_bmp("test.bmp", bitmapDim.w, bitmapDim.h, 1, loadedFont.bitmap);
#endif

	*font = loadedFont;
	return true;
}

FILE_SCOPE bool BitmapLoad(const PlatformAPI api, DTRBitmap *bitmap,
                           const char *const path,
                           DqnMemBuffer *const transientBuffer)
{
	if (!bitmap) return false;

	PlatformFile file = {};
	if (!api.FileOpen(path, &file, PlatformFilePermissionFlag_Read))
		return false;

	DqnTempBuffer tempBuffer = DqnMemBuffer_BeginTempRegion(transientBuffer);
	{
		u8 *const rawData =
		    (u8 *)DqnMemBuffer_Allocate(transientBuffer, file.size);
		size_t bytesRead = api.FileRead(&file, rawData, file.size);
		api.FileClose(&file);

		if (bytesRead != file.size)
		{
			DqnMemBuffer_EndTempRegion(tempBuffer);
			return false;
		}

		bitmap->memory =
		    stbi_load_from_memory(rawData, (i32)file.size, &bitmap->dim.w,
		                          &bitmap->dim.h, &bitmap->bytesPerPixel, 4);
	}
	DqnMemBuffer_EndTempRegion(tempBuffer);
	if (!bitmap->memory) return false;

	const i32 pitch = bitmap->dim.w * bitmap->bytesPerPixel;
	for (i32 y = 0; y < bitmap->dim.h; y++)
	{
		u8 *const srcRow = bitmap->memory + (y * pitch);
		for (i32 x = 0; x < bitmap->dim.w; x++)
		{
			u32 *pixelPtr = (u32 *)srcRow;
			u32 pixel     = pixelPtr[x];

			DqnV4 color = {};
			color.a     = (f32)(pixel >> 24);
			color.b     = (f32)((pixel >> 16) & 0xFF);
			color.g     = (f32)((pixel >> 8) & 0xFF);
			color.r     = (f32)((pixel >> 0) & 0xFF);
			color       = DTRRender_PreMultiplyAlpha(color);

			pixel = (((u32)color.a << 24) |
			         ((u32)color.b << 16) |
			         ((u32)color.g << 8) |
			         ((u32)color.r << 0));

			pixelPtr[x] = pixel;
		}
	}

	return true;
}

extern "C" void DTR_Update(PlatformRenderBuffer *const renderBuffer,
                           PlatformInput *const input,
                           PlatformMemory *const memory)
{
	DTRState *state = (DTRState *)memory->context;
	if (!memory->isInit)
	{
		stbi_set_flip_vertically_on_load(true);
		memory->isInit = true;
		memory->context =
		    DqnMemBuffer_Allocate(&memory->permanentBuffer, sizeof(DTRState));
		DQN_ASSERT(memory->context);

		state = (DTRState *)memory->context;
		BitmapFontCreate(input->api, memory, &state->font, "Roboto-bold.ttf",
		                 DqnV2i_2i(256, 256), DqnV2i_2i(' ', '~'), 16);
		BitmapLoad(input->api, &state->bitmap, "lune_logo.png",
		           &memory->transientBuffer);
	}

	DTRRender_Clear(renderBuffer, DqnV3_3f(0, 0, 0));
	DqnV4 colorRed = DqnV4_4i(180, 0, 0, 255);
	DqnV2i bufferMidP =
	    DqnV2i_2f(renderBuffer->width * 0.5f, renderBuffer->height * 0.5f);
	i32 boundsOffset = 100;

	DqnV2 t0[3] = {DqnV2_2i(10, 70), DqnV2_2i(50, 160), DqnV2_2i(70, 80)};
	DqnV2 t1[3] = {DqnV2_2i(180, 50),  DqnV2_2i(150, 1),   DqnV2_2i(70, 180)};
	DqnV2 t2[3] = {DqnV2_2i(180, 150), DqnV2_2i(120, 160), DqnV2_2i(130, 180)};
	LOCAL_PERSIST DqnV2 t3[3] = {
	    DqnV2_2i(boundsOffset, boundsOffset),
	    DqnV2_2i(bufferMidP.w, renderBuffer->height - boundsOffset),
	    DqnV2_2i(renderBuffer->width - boundsOffset, boundsOffset)};

#if 1
	DTRRender_Triangle(renderBuffer, t0[0], t0[1], t0[2], colorRed);
	DTRRender_Triangle(renderBuffer, t1[0], t1[1], t1[2], colorRed);
	DTRRender_Triangle(renderBuffer, t2[0], t2[1], t2[2], colorRed);
#endif

	DqnV4 colorRedHalfA = DqnV4_4i(255, 0, 0, 64);
	LOCAL_PERSIST f32 rotation = 0;
	rotation += input->deltaForFrame * 0.25f;
	DTRRender_Triangle(renderBuffer, t3[0], t3[1], t3[2], colorRedHalfA,
	                   DqnV2_1f(1.0f), rotation, DqnV2_2f(0.33f, 0.33f));

	DTRRender_Rectangle(renderBuffer, DqnV2_1f(300.0f), DqnV2_1f(300 + 20.0f),
	              colorRed, DqnV2_1f(1.0f), 45 + rotation);

	DqnV2 fontP = DqnV2_2i(200, 180);
	DTRRender_Text(renderBuffer, state->font, fontP, "hello world!");

	DTRRenderTransform transform = DTRRender_DefaultTransform();
	transform.rotation           = rotation;
	DTRRender_Bitmap(renderBuffer, &state->bitmap, DqnV2i_2i(200, 300), transform);
	DTRDebug_Update(state, renderBuffer, input, memory);
}
