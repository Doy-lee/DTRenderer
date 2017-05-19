#ifndef DRENDERER_PLATFORM_H
#define DRENDERER_PLATFORM_H

#include "dqn.h"

enum PlatformFilePermissionFlag
{
	PlatformFilePermissionFlag_Read  = (1 << 0),
	PlatformFilePermissionFlag_Write = (1 << 1),
};

typedef struct PlatformFile
{
	void   *handle;
	size_t  size;
	u32     permissionFlags;
} PlatformFile;

typedef bool   PlatformAPI_FileOpen (const char *const path, PlatformFile *const file,
                                     const u32 permissionFlags);
typedef size_t PlatformAPI_FileRead (PlatformFile *const file, u8 *const buf,
                                     const size_t bytesToRead); // Return bytes read
typedef void   PlatformAPI_FileClose(PlatformFile *const file);
typedef void   PlatformAPI_Print    (const char *const string);
typedef struct PlatformAPI
{
	PlatformAPI_FileOpen  *FileOpen;
	PlatformAPI_FileRead  *FileRead;
	PlatformAPI_FileClose *FileClose;
	PlatformAPI_Print     *Print;
} PlatformAPI;

enum Key
{
	key_up,
	key_down,
	key_left,
	key_right,
	key_escape,

	key_1,
	key_2,
	key_3,
	key_4,

	key_q,
	key_w,
	key_e,
	key_r,

	key_a,
	key_s,
	key_d,
	key_f,

	key_z,
	key_x,
	key_c,
	key_v,

	key_count,
};

typedef struct KeyState
{
	bool endedDown;
	u32 halfTransitionCount;
} KeyState;

typedef struct PlatformInput
{
	f32  deltaForFrame;
	f64  timeNowInS;
	bool executableReloaded;
	bool canUseSSE2;

	PlatformAPI api;
	union {
		KeyState key[key_count];
		struct
		{
			KeyState up;
			KeyState down;
			KeyState left;
			KeyState right;
			KeyState escape;

			KeyState key_1;
			KeyState key_2;
			KeyState key_3;
			KeyState key_4;

			KeyState key_q;
			KeyState key_w;
			KeyState key_e;
			KeyState key_r;

			KeyState key_a;
			KeyState key_s;
			KeyState key_d;
			KeyState key_f;

			KeyState key_z;
			KeyState key_x;
			KeyState key_c;
			KeyState key_v;
		};
	};
} PlatformInput;

typedef struct PlatformMemory
{
	DqnMemBuffer permanentBuffer;
	DqnMemBuffer transientBuffer;
	bool  isInit;
	void *context;
} PlatformMemory;

typedef struct PlatformRenderBuffer
{
	i32   width;
	i32   height;
	i32   bytesPerPixel;

	//  Pixel Format: XX RR GG BB
	void *memory;
} PlatformRenderBuffer;

#endif
