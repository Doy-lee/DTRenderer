#include "DTRendererAsset.h"
#include "DTRendererDebug.h"
#include "DTRendererRender.h"

#include "external/stb_image.h"

// #define DTR_DEBUG_RENDER_FONT_BITMAP
#ifdef DTR_DEBUG_RENDER_FONT_BITMAP
	#define STB_IMAGE_WRITE_IMPLEMENTATION
	#include "external/tests/stb_image_write.h"
#endif

void DTRAsset_InitGlobalState()
{
	// NOTE(doyle): Do premultiply ourselves
	stbi_set_unpremultiply_on_load(true);
	stbi_set_flip_vertically_on_load(true);
}

FILE_SCOPE void MemcopyInternal(u8 *const dest, u8 *const src, size_t numBytes)
{
	if (!dest || !src || numBytes == 0) return;
	for (size_t i = 0; i < numBytes; i++)
		dest[i] = src[i];
}

// NOTE: Dynamic array allocations just requests space at the first option it
// can take. Realloc will reallocate in place if there's space. Otherwise
// it'll create a new block and reallocate there by copying the old data over.

// So this does waste space. But is a quick way to reroute allocations into
// a MemStack. It's main intended purpose is for one-shot loading data that you
// don't know how much space you need in your DArray. After filling out
// the dynamic array you then compact the data manually using memcopys into
// a new block and discard the old data.
FILE_SCOPE void DumbDynamicArrayMemAPICallback(DqnMemAPICallbackInfo info, DqnMemAPICallbackResult *result)
{
	DQN_ASSERT(info.type != DqnMemAPICallbackType_Invalid);
	DqnMemStack *stack = static_cast<DqnMemStack *>(info.userContext);
	switch (info.type)
	{
		case DqnMemAPICallbackType_Alloc:
		{
			void *ptr         = DqnMemStack_Push(stack, info.requestSize);
			result->newMemPtr = ptr;
			result->type      = DqnMemAPICallbackType_Alloc;
		}
		break;

		case DqnMemAPICallbackType_Free:
		{
			DQN_ASSERT(DQN_INVALID_CODE_PATH);
		}
		break;

		case DqnMemAPICallbackType_Realloc:
		{
			result->type = DqnMemAPICallbackType_Realloc;

			// Check if the ptr is the last thing that was allocated. If so we
			// can check if there's enough space in place for realloc and give
			// them that.
			u8 *currMemPtr = (u8 *)(stack->block->memory + stack->block->used);
			u8 *checkPtr   = currMemPtr - info.oldSize;
			if (checkPtr == info.oldMemPtr)
			{
				if ((stack->block->used + info.newRequestSize) < stack->block->size)
				{
					stack->block->used += info.newRequestSize;
					result->newMemPtr = info.oldMemPtr;
					return;
				}

				// The allocation was the last one allocated, but there's not
				// enough space to realloc in the block. For book-keeping,
				// "deallocate" the old mem ptr by reverting the usage of the
				// memory stack.
				stack->block->used -= info.oldSize;
			}

			// Otherwise, not enough space or, allocation is not the last
			// allocated, so can't expand inplace.
			DqnMemStackBlock *newBlock =
			    DqnMemStack_AllocateCompatibleBlock(stack, info.newRequestSize);
			if (DqnMemStack_AttachBlock(stack, newBlock))
			{
				// NOTE(doyle): This leaves chunks of memory dead!!! But, we use
				// this in our custom allocator, which its strategy is to load all
				// the data, using as much re-allocations as required then after the
				// fact, recompact the data by Memcopying the data together and free
				// the extraneous blocks we've made.

				void *newPtr = DqnMemStack_Push(stack, info.newRequestSize);
				MemcopyInternal((u8 *)newPtr, (u8 *)info.oldMemPtr, info.oldSize);

				result->newMemPtr = newPtr;
			}
			else
			{
				// TODO(doyle): Die out of memory
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}
		}
		break;
	}
}

typedef struct WavefModelFace
{
	DqnArray<i32> vertexIndexArray;
	DqnArray<i32> textureIndexArray;
	DqnArray<i32> normalIndexArray;
} WavefModelFace;

typedef struct WavefModel
{
	DqnArray<DqnV4> geometryArray;
	DqnArray<DqnV3> textureArray;
	DqnArray<DqnV3> normalArray;

	// TODO(doyle): Fixed size
	char *groupName[16];
	i32   groupNameIndex;
	i32   groupSmoothing;

	DqnArray<WavefModelFace> faces;
} WavefModel;

FILE_SCOPE bool WavefModelInit(WavefModel *const obj,
                               DqnMemAPI memAPI             = DqnMemAPI_DefaultUseCalloc(),
                               const i32 vertexInitCapacity = 1000,
                               const i32 faceInitCapacity   = 1000)
{
	if (!obj) return false;

	bool initialised = false;

	initialised |= DqnArray_Init(&obj->geometryArray, vertexInitCapacity, memAPI);
	initialised |= DqnArray_Init(&obj->textureArray,  vertexInitCapacity, memAPI);
	initialised |= DqnArray_Init(&obj->normalArray,   vertexInitCapacity, memAPI);
	initialised |= DqnArray_Init(&obj->faces,         faceInitCapacity, memAPI);

	if (!initialised)
	{
		DqnArray_Free(&obj->geometryArray);
		DqnArray_Free(&obj->textureArray);
		DqnArray_Free(&obj->normalArray);
		DqnArray_Free(&obj->faces);
	}

	return initialised;
}

FILE_SCOPE inline WavefModelFace
WavefModelFaceInit(i32 capacity = 3, DqnMemAPI memAPI = DqnMemAPI_DefaultUseCalloc())
{
	WavefModelFace result = {};
	DQN_ASSERT(DqnArray_Init(&result.vertexIndexArray,  capacity, memAPI));
	DQN_ASSERT(DqnArray_Init(&result.textureIndexArray, capacity, memAPI));
	DQN_ASSERT(DqnArray_Init(&result.normalIndexArray,  capacity, memAPI));

	return result;
}

bool DTRAsset_LoadWavefrontObj(const PlatformAPI api, DqnMemStack *const memStack,
                               DTRMesh *const mesh, const char *const path)
{
	if (!memStack || !path || !mesh) return false;

	PlatformFile file = {};
	if (!api.FileOpen(path, &file, PlatformFilePermissionFlag_Read, PlatformFileAction_OpenOnly))
		return false; // TODO(doyle): Logging

	bool result                    = false;
	DqnTempMemStack tmpAssetRegion = DqnMemStack_BeginTempRegion(memStack);
	u8 *rawBytes                   = (u8 *)DqnMemStack_Push(memStack, file.size);
	size_t bytesRead               = api.FileRead(&file, rawBytes, file.size);
	size_t fileSize                = file.size;

	DqnMemAPI memAPI   = {};
	memAPI.callback    = DumbDynamicArrayMemAPICallback;
	memAPI.userContext = memStack;

	enum WavefVertexType {
		WavefVertexType_Invalid,
		WavefVertexType_Geometric,
		WavefVertexType_Texture,
		WavefVertexType_Normal,
	};

	// TODO(doyle): We should profile, reading it out to WavefModel format and
	// then copying it over, versus just reading the file twice. First pass is
	// to count the number of vertexes etc. for each section we need. Then the
	// second pass we can allocate directly the number we need and reparse it.
	// I have a feeling that, in general that's a better idea, atleast it gets
	// rid of alot of stupid copying code and memstack juggling.

	// NOTE(doyle): We pre-process the data into an intermediate format that
	// more accurately represents the file format. Since there's no metadata
	// inside Wavefront objects, we don't know how many vertexes/texUV/normals
	// there are- which makes it hard to allocate "nicely" out of our memory
	// stack.

	// So we preprocess. Then once we know the final amount, copy over the data
	// to a new memstack block such that all the data is compacted together in
	// memory for locality. Then just throw away the intermediate
	// representation.
	WavefModel dummy_ = {};
	WavefModel *obj   = &dummy_;

	if (bytesRead != file.size)       goto cleanup;
	if (!WavefModelInit(obj, memAPI)) goto cleanup;

	for (char *scan = (char *)rawBytes; scan && scan < ((char *)rawBytes + fileSize);)
	{
		switch (DqnChar_ToLower(*scan))
		{
			////////////////////////////////////////////////////////////////////
			// Polygonal Free Form Statement
			////////////////////////////////////////////////////////////////////
			// Vertex Format: v[ |t|n|p] x y z [w]
			case 'v':
			{
				scan++;
				DQN_ASSERT(scan);

				enum WavefVertexType type = WavefVertexType_Invalid;

				char identifier = DqnChar_ToLower(*scan);
				if      (identifier == ' ') type = WavefVertexType_Geometric;
				else if (identifier == 't' || identifier == 'n')
				{
					scan++;
					if (identifier == 't') type = WavefVertexType_Texture;
					else                   type = WavefVertexType_Normal;
				}
				else DQN_ASSERT(DQN_INVALID_CODE_PATH);

				i32 vIndex = 0;
				DqnV4 v4   = {0, 0, 0, 1.0f};

				// Progress to first non space character after vertex identifier
				for (; scan && *scan == ' '; scan++)
					if (!scan) DQN_ASSERT(DQN_INVALID_CODE_PATH);

				for (;;)
				{
					char *f32StartPtr = scan;
					for (; *scan != ' ' && *scan != '\n';)
					{
						DQN_ASSERT(DqnChar_IsDigit(*scan) || (*scan == '.') || (*scan == '-') ||
						           *scan == 'e');
						scan++;
					}

					i32 f32Len     = (i32)((size_t)scan - (size_t)f32StartPtr);
					v4.e[vIndex++] = Dqn_StrToF32(f32StartPtr, f32Len);
					DQN_ASSERT(vIndex < DQN_ARRAY_COUNT(v4.e));

					while (scan && (*scan == ' ' || *scan == '\n')) scan++;

					if (!scan) break;
					if (!(DqnChar_IsDigit(*scan) || *scan == '-')) break;
				}

				DQN_ASSERT(vIndex == 3 || vIndex == 4);
				if (type == WavefVertexType_Geometric)
				{
					DqnArray_Push(&obj->geometryArray, v4);
				}
				else if (type == WavefVertexType_Texture)
				{
					DqnArray_Push(&obj->textureArray, v4.xyz);
				}
				else if (type == WavefVertexType_Normal)
				{
					DqnArray_Push(&obj->normalArray, v4.xyz);
				}
				else
				{
					DQN_ASSERT(DQN_INVALID_CODE_PATH);
				}
			}
			break;

			////////////////////////////////////////////////////////////////////
			// Polygonal Geometry
			////////////////////////////////////////////////////////////////////
			// Vertex numbers can be negative to reference a relative offset to
			// the vertex which means the relative order of the vertices
			// specified in the file, i.e.

			// v 0.000000 2.000000 2.000000
			// v 0.000000 0.000000 2.000000
			// v 2.000000 0.000000 2.000000
			// v 2.000000 2.000000 2.000000
			// f -4 -3 -2 -1

			// Point Format: p v1 v2 v3 ...
			// Each point is one vertex.
			case 'p':
			{
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}
			break;

			// Line Format: l v1/vt1 v2/vt2 v3/vt3 ...
			// Texture vertex is optional. Minimum of two vertex numbers, no
			// limit on maximum.
			case 'l':
			{
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}
			break;

			// Face Format: f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ...
			// Minimum of three vertexes, no limit on maximum. vt, vn are
			// optional. But specification of vt, vn must be consistent if given
			// across all the vertices for the line.

			// For example, to specify only the vertex and vertex normal
			// reference numbers, you would enter:
			
			// f 1//1 2//2 3//3 4//4
			case 'f':
			{
				scan++;
				while (scan && (*scan == ' ' || *scan == '\n')) scan++;
				if (!scan) continue;

				WavefModelFace face   = WavefModelFaceInit(3, memAPI);
				i32 numVertexesParsed    = 0;
				bool moreVertexesToParse = true;
				while (moreVertexesToParse)
				{
					enum WavefVertexType type = WavefVertexType_Geometric;

					// Read a vertexes 3 attributes v, vt, vn
					for (i32 i = 0; i < 3; i++)
					{
						char *numStartPtr = scan;
						while (scan && DqnChar_IsDigit(*scan))
							scan++;

						i32 numLen = (i32)((size_t)scan - (size_t)numStartPtr);
						if (numLen > 0)
						{
							// NOTE(doyle): Obj format starts indexing from 1,
							// so offset by -1 to make it zero-based indexes.
							i32 vertIndex = (i32)Dqn_StrToI64(numStartPtr, numLen) - 1;

							// TODO(doyle): Does not supprot relative vertexes yet
							DQN_ASSERT(vertIndex >= 0);

							if (type == WavefVertexType_Geometric)
							{
								DQN_ASSERT(DqnArray_Push(&face.vertexIndexArray, vertIndex));
							}
							else if (type == WavefVertexType_Texture)
							{
								DQN_ASSERT(DqnArray_Push(&face.textureIndexArray, vertIndex));
							}
							else if (type == WavefVertexType_Normal)
							{
								DQN_ASSERT(DqnArray_Push(&face.normalIndexArray, vertIndex));
							}
						}

						if (scan) scan++;
						type = (enum WavefVertexType)((i32)type + 1);
					}
					numVertexesParsed++;

					if (scan)
					{
						// Move to next "non-empty" character
						while (scan && (*scan == ' ' || *scan == '\n'))
							scan++;

						// If it isn't a digit, then we've read all the
						// vertexes for this face
						if (!scan || (scan && !DqnChar_IsDigit(*scan)))
						{
							moreVertexesToParse = false;
						}
					}
				}
				DQN_ASSERT(numVertexesParsed >= 3);
				DQN_ASSERT(DqnArray_Push(&obj->faces, face));
			}
			break;

			////////////////////////////////////////////////////////////////////
			// Misc
			////////////////////////////////////////////////////////////////////

			// Group Name Format: g group_name1 group_name2
			// This is optional, if multiple groups are specified, then the
			// following elements belong to all groups. The default group name
			// is "default"
			case 'g':
			{
				scan++;
				while (scan && (*scan == ' ' || *scan == '\n')) scan++;

				if (!scan) continue;

				// Iterate to end of the name, i.e. move ptr to first space
				char *namePtr = scan;
				while (scan && (*scan != ' ' && *scan != '\n'))
					scan++;

				if (scan)
				{
					i32 nameLen = (i32)((size_t)scan - (size_t)namePtr);
					DQN_ASSERT(obj->groupNameIndex + 1 < DQN_ARRAY_COUNT(obj->groupName));

					DQN_ASSERT(!obj->groupName[obj->groupNameIndex]);
					// TODO(doyle): Broken since I don't "copy" it over to our
					// final DTRMesh. Below I copy over the data so that all the
					// allocations are compacted together but don't copy this
					// yet. Which means the name gets trashed atm.
#if 0
					obj->groupName[obj->groupNameIndex++] =
					    (char *)DqnMemStack_Push(memStack, (nameLen + 1) * sizeof(char));

					for (i32 i = 0; i < nameLen; i++)
						obj->groupName[obj->groupNameIndex - 1][i] = namePtr[i];
#endif

					while (scan && (*scan == ' ' || *scan == '\n'))
						scan++;
				}
			}
			break;

			// Smoothing Group: s group_number
			// Sets the smoothing group for the elements that follow it. If it's
			// not to be used it can be specified as "off" or a value of 0.
			case 's':
			{
				// Advance to first non space char after identifier
				scan++;
				while (scan && *scan == ' ' || *scan == '\n') scan++;

				if (scan && DqnChar_IsDigit(*scan))
				{
					char *numStartPtr = scan;
					while (scan && (*scan != ' ' && *scan != '\n'))
					{
						DQN_ASSERT(DqnChar_IsDigit(*scan));
						scan++;
					}

					i32 numLen          = (i32)((size_t)scan - (size_t)numStartPtr);
					i32 groupSmoothing  = (i32)Dqn_StrToI64(numStartPtr, numLen);
					obj->groupSmoothing = groupSmoothing;
				}

				while (scan && *scan == ' ' || *scan == '\n') scan++;
			}
			break;

			// Comment
			case '#':
			{
				// Skip comment line until new line
				while (scan && *scan != '\n')
					scan++;

				// Skip new lines and any leading white spaces
				while (scan && (*scan == '\n' || *scan == ' '))
					scan++;
			}
			break;

			default:
			{
				DQN_ASSERT(DQN_INVALID_CODE_PATH);
			}
			break;
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Recompact Allocations
	////////////////////////////////////////////////////////////////////////////
	size_t geometrySize      = sizeof(obj->geometryArray.data[0]) * obj->geometryArray.count;
	size_t textureSize       = sizeof(obj->textureArray.data[0]) * obj->textureArray.count;
	size_t normalSize        = sizeof(obj->normalArray.data[0]) * obj->normalArray.count;
	size_t faceSize          = sizeof(obj->faces.data[0]) * obj->faces.count;
	size_t vertIndexItemSize = sizeof(obj->faces.data[0].vertexIndexArray.data[0]);
	size_t texIndexItemSize  = sizeof(obj->faces.data[0].textureIndexArray.data[0]);
	size_t normIndexItemSize = sizeof(obj->faces.data[0].normalIndexArray.data[0]);

	size_t totalModelSize = geometrySize + textureSize + normalSize + faceSize;
	{
		for (i32 i = 0; i < obj->faces.count; i++)
		{
			WavefModelFace *face = &obj->faces.data[i];

			size_t vertIndexSize    = face->vertexIndexArray.count * vertIndexItemSize;
			size_t texIndexSize     = face->textureIndexArray.count * texIndexItemSize;
			size_t normIndexSize    = face->normalIndexArray.count * normIndexItemSize;

			totalModelSize += (vertIndexSize + texIndexSize + normIndexSize);
		}
	}

	// IMPORTANT(doyle): We always allocate a new block, so each assets owns
	// their own memory block.
	DqnMemStackBlock *modelBlock =
	    DqnMemStack_AllocateCompatibleBlock(memStack, totalModelSize);
	if (modelBlock)
	{
		if (DqnMemStack_AttachBlock(memStack, modelBlock))
		{
			mesh->vertexes = (DqnV4 *)DqnMemStack_Push(memStack, geometrySize);
			mesh->texUV    = (DqnV3 *)DqnMemStack_Push(memStack, textureSize);
			mesh->normals  = (DqnV3 *)DqnMemStack_Push(memStack, normalSize);
			mesh->faces    = (DTRMeshFace *)DqnMemStack_Push(memStack, faceSize);

			mesh->numVertexes = (u32)obj->geometryArray.count;
			mesh->numTexUV    = (u32)obj->textureArray.count;
			mesh->numNormals  = (u32)obj->normalArray.count;
			mesh->numFaces    = (u32)obj->faces.count;

			MemcopyInternal((u8 *)mesh->vertexes, (u8 *)obj->geometryArray.data, geometrySize);
			MemcopyInternal((u8 *)mesh->texUV,    (u8 *)obj->textureArray.data,  textureSize);
			MemcopyInternal((u8 *)mesh->normals,  (u8 *)obj->normalArray.data,   normalSize);
			MemcopyInternal((u8 *)mesh->faces,    (u8 *)obj->faces.data,     faceSize);

			for (i32 i = 0; i < obj->faces.count; i++)
			{
				WavefModelFace *face     = &obj->faces.data[i];
				DTRMeshFace *const meshFace = &mesh->faces[i];

				size_t vertIndexSize = face->vertexIndexArray.count * vertIndexItemSize;
				size_t texIndexSize  = face->textureIndexArray.count * texIndexItemSize;
				size_t normIndexSize = face->normalIndexArray.count * normIndexItemSize;

				meshFace->vertexIndex = (i32 *)DqnMemStack_Push(memStack, vertIndexSize);
				meshFace->texIndex    = (i32 *)DqnMemStack_Push(memStack, texIndexSize);
				meshFace->normalIndex = (i32 *)DqnMemStack_Push(memStack, normIndexSize);

				MemcopyInternal((u8 *)meshFace->vertexIndex, (u8 *)face->vertexIndexArray.data, vertIndexSize);
				MemcopyInternal((u8 *)meshFace->texIndex,    (u8 *)face->textureIndexArray.data, texIndexSize);
				MemcopyInternal((u8 *)meshFace->normalIndex, (u8 *)face->normalIndexArray.data, normIndexSize);

				meshFace->numVertexIndex = (u32)face->vertexIndexArray.count;
				meshFace->numTexIndex    = (u32)face->textureIndexArray.count;
				meshFace->numNormalIndex = (u32)face->normalIndexArray.count;
			}

			// NOTE: Detach the block, because the stack is in a temp region.
			// End the temp region and reattach the compact model block.
			DqnMemStack_DetachBlock(memStack, modelBlock);
		}
		else
		{
			// TODO(doyle): Stack can't attach block, i.e. invalid args or
			// stack is configured to be nonexpandable
			DQN_ASSERT(DQN_INVALID_CODE_PATH);
		}
	}
	else
	{
		// TODO(doyle): Out of memory error
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
	}

	DqnMemStack_EndTempRegion(tmpAssetRegion);
	if (modelBlock)
	{
		result = true;
		DqnMemStackBlock *firstBlock = memStack->block;
		DqnMemStack_DetachBlock(memStack, memStack->block);

		DqnMemStack_AttachBlock(memStack, modelBlock);
		DqnMemStack_AttachBlock(memStack, firstBlock);
	}

cleanup:
	api.FileClose(&file);
	if(!result) DqnMemStack_EndTempRegion(tmpAssetRegion);

	return result;
}

bool DTRAsset_LoadFontToBitmap(const PlatformAPI api, DqnMemStack *const memStack,
                               DqnMemStack *const tmpMemStack, DTRFont *const font,
                               const char *const path, const DqnV2i bitmapDim,
                               const DqnV2i codepointRange, const f32 sizeInPt)
{
	if (!memStack || !font || !path) return false;

	////////////////////////////////////////////////////////////////////////////
	// Load font data
	////////////////////////////////////////////////////////////////////////////
	DTRFont loadedFont = {};
	loadedFont.bitmapDim      = bitmapDim;
	loadedFont.codepointRange = codepointRange;
	loadedFont.sizeInPt       = sizeInPt;

	PlatformFile file = {};
	if (!api.FileOpen(path, &file, PlatformFilePermissionFlag_Read, PlatformFileAction_OpenOnly))
		return false; // TODO(doyle): Logging

	bool result = false;
	DqnTempMemStack tmpMemRegion = DqnMemStack_BeginTempRegion(tmpMemStack);
	u8 *rawBytes                 = (u8 *)DqnMemStack_Push(tmpMemStack, file.size);
	size_t bytesRead             = api.FileRead(&file, rawBytes, file.size);

	stbtt_fontinfo fontInfo            = {};
	stbtt_pack_context fontPackContext = {};

	if (bytesRead != file.size || stbtt_InitFont(&fontInfo, rawBytes, 0) == 0)
	{
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
		goto cleanup;
	}

	if (DTR_DEBUG) DQN_ASSERT(stbtt_GetNumberOfFonts(rawBytes) == 1);
	////////////////////////////////////////////////////////////////////////////
	// Pack font data to bitmap
	////////////////////////////////////////////////////////////////////////////
	loadedFont.bitmap =
	    (u8 *)DqnMemStack_Push(memStack, (size_t)(loadedFont.bitmapDim.w * loadedFont.bitmapDim.h));

	if (stbtt_PackBegin(&fontPackContext, loadedFont.bitmap, bitmapDim.w,
	                    bitmapDim.h, 0, 1, NULL) == 1)
	{
		// stbtt_PackSetOversampling(&fontPackContext, 2, 2);
		result            = true;
		i32 numCodepoints = (i32)((codepointRange.max + 1) - codepointRange.min);
		loadedFont.atlas  = (stbtt_packedchar *)DqnMemStack_Push(
		    memStack, numCodepoints * sizeof(stbtt_packedchar));
		stbtt_PackFontRange(&fontPackContext, rawBytes, 0,
		                    STBTT_POINT_SIZE(sizeInPt), (i32)codepointRange.min,
		                    numCodepoints, loadedFont.atlas);
		stbtt_PackEnd(&fontPackContext);
	}
	else
	{
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
		goto cleanup;
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
			u32 index = x + (y * bitmapDim.w);
			f32 alpha = (f32)(loadedFont.bitmap[index]) / 255.0f;
			f32 color = alpha;

			color = DTRRender_SRGB1ToLinearSpacef(color);
			color = color * alpha;
			color = DTRRender_LinearToSRGB1Spacef(color) * 255.0f;
			DQN_ASSERT(color >= 0.0f && color <= 255.0f);

			loadedFont.bitmap[index] = (u8)color;
		}
	}

#ifdef DTR_DEBUG_RENDER_FONT_BITMAP
	stbi_write_bmp("test.bmp", bitmapDim.w, bitmapDim.h, 1, loadedFont.bitmap);
#endif

	*font = loadedFont;

cleanup:
	DqnMemStack_EndTempRegion(tmpMemRegion);
	api.FileClose(&file);

	return result;
}

////////////////////////////////////////////////////////////////////////////////
// Bitmap Loading Code
////////////////////////////////////////////////////////////////////////////////
// TODO(doyle): Not threadsafe
FILE_SCOPE DqnMemStack *globalSTBImageAllocator;

FILE_SCOPE void *STBImageReallocSized(void *ptr, size_t oldSize, size_t newSize)
{
	// TODO(doyle): Implement when needed. There's no easy way using our stack
	// allocator since freeing arbitrary elements is abit difficult. But the
	// general approach is, if the ptr is the last thing that was allocated,
	// then we can safely push if there's enough space.

	// Othewise eat the cost, we have to actually realloc the old data. Make
	// a new block and memcpy over the old data.
	DQN_ASSERT(DQN_INVALID_CODE_PATH);
	return NULL;
}

FILE_SCOPE void STBImageFree(void *ptr)
{
	// NOTE(doyle): Using our own allocator. Using our MemStack just take note
	// of current usage before we call STB functions. When we receive the final
	// ptr just memmove the ptr data back to where we started and update usage
	// accordingly.
	return;
}

FILE_SCOPE void *STBImageMalloc(size_t size)
{
	DQN_ASSERT(globalSTBImageAllocator);
	if (!globalSTBImageAllocator) return NULL;

	void *result = DqnMemStack_Push(globalSTBImageAllocator, size);
	return result;
}

#define STBI_MALLOC(size)                         STBImageMalloc(size)
#define STBI_REALLOC_SIZED(ptr, oldSize, newSize) STBImageReallocSized(ptr, oldSize, newSize)
#define STBI_FREE(ptr)                            STBImageFree(ptr)

#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

bool DTRAsset_LoadBitmap(const PlatformAPI api, DqnMemStack *const memStack,
                         DqnMemStack *const tempStack, DTRBitmap *bitmap, const char *const path)
{
	if (!bitmap || !memStack || !tempStack) return false;

	bool result       = false;
	PlatformFile file = {};
	if (!api.FileOpen(path, &file, PlatformFilePermissionFlag_Read, PlatformFileAction_OpenOnly))
		return result;

	DqnTempMemStack tmpMemRegion = DqnMemStack_BeginTempRegion(tempStack);
	u8 *const rawData            = (u8 *)DqnMemStack_Push     (tempStack, file.size);
	size_t bytesRead             = api.FileRead               (&file, rawData, file.size);

	if (bytesRead != file.size) goto cleanup;

	// IMPORTANT(doyle): Look at this line!!! To remind you anytime you think of modifying code here
	globalSTBImageAllocator = memStack;

	// TODO(doyle): We don't need this atm since we are still using a temp stack in this code.
	// size_t usageBeforeSTB   = memStack->block->used;

	const u32 FORCE_4_BPP = 4;
	bitmap->bytesPerPixel = FORCE_4_BPP;
	u8 *pixels = stbi_load_from_memory(rawData, (i32)file.size, &bitmap->dim.w, &bitmap->dim.h,
	                                   NULL, FORCE_4_BPP);
	if (!pixels) goto cleanup;
	result         = true;

    // TODO(doyle): See above. Since we use temp stack we can allocate straight into the AssetStack.
#if 0
	size_t pixelsSizeInBytes    = bitmap->dim.w * bitmap->dim.h * FORCE_4_BPP;
	memStack->block->used = usageBeforeSTB;
	if ((usageBeforeSTB + pixelsSizeInBytes) < memStack->block->size)
	{
		u8 *dest = memStack->block->memory + memStack->block->used;
		MemcopyInternal(dest, pixels, pixelsSizeInBytes);
		pixels = dest;
	}
	else
	{
		// TODO(doyle): Check this periodically. We don't like this branch occuring often
		// Otherwise, stbi will call STBImageMalloc which uses our MemStack and
		// MemStack will allocate a new block for us if it can, so it'll already
		// be somewhat "suitably" sitting in our memory system.
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
	}
#endif

	bitmap->memory = pixels;

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

			DqnV4 preMulColor = color;
			preMulColor *= DTRRENDER_INV_255;
			preMulColor = DTRRender_PreMultiplyAlphaSRGB1WithLinearConversion(preMulColor);
			preMulColor *= 255.0f;

			pixel = (((u32)preMulColor.a << 24) |
			         ((u32)preMulColor.b << 16) |
			         ((u32)preMulColor.g << 8) |
			         ((u32)preMulColor.r << 0));

			pixelPtr[x] = pixel;
		}
	}

cleanup:
	globalSTBImageAllocator = NULL;
	DqnMemStack_EndTempRegion(tmpMemRegion);
	api.FileClose(&file);

	return result;
}
