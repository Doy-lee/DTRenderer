#ifndef DRENDERER_PLATFORM_H
#define DRENDERER_PLATFORM_H

#include "dqn.h"
#include <intrin.h>

typedef void PlatformAPI_DieGracefully();

////////////////////////////////////////////////////////////////////////////////
// Platform File I/O
////////////////////////////////////////////////////////////////////////////////
enum PlatformFilePermissionFlag
{
	PlatformFilePermissionFlag_Read  = (1 << 0),
	PlatformFilePermissionFlag_Write = (1 << 1),
};

enum PlatformFileAction
{
	PlatformFileAction_OpenOnly,
	PlatformFileAction_CreateIfNotExist,
	PlatformFileAction_ClearIfExist,
};

typedef struct PlatformFile
{
	void   *handle;
	size_t  size;
	u32     permissionFlags;
} PlatformFile;

// File I/O API
typedef bool   PlatformAPI_FileOpen (const char *const path, PlatformFile *const file, const u32 permissionFlags, const enum PlatformFileAction actionFlags);
typedef size_t PlatformAPI_FileRead (PlatformFile *const file, u8 *const buf, const size_t bytesToRead);     // Return bytes read
typedef size_t PlatformAPI_FileWrite(PlatformFile *const file, u8 *const buf, const size_t numBytesToWrite); // Return bytes read
typedef void   PlatformAPI_FileClose(PlatformFile *const file);
typedef void   PlatformAPI_Print    (const char *const string);

////////////////////////////////////////////////////////////////////////////////
// Platform Multithreading
////////////////////////////////////////////////////////////////////////////////
// PlatformJobQueue must be implemented in platform code. It simply needs to
// fullfill the API and be able to accept PlatformJob structs and execute them.
typedef struct PlatformJobQueue PlatformJobQueue;

typedef void   PlatformJob_Callback(PlatformJobQueue *const queue, void *const userData);
typedef struct PlatformJob
{
	PlatformJob_Callback *callback;
	void                 *userData;
} PlatformJob;

// Multithreading API
typedef bool PlatformAPI_QueueAddJob           (PlatformJobQueue *const queue, const PlatformJob job);
typedef bool PlatformAPI_QueueTryExecuteNextJob(PlatformJobQueue *const queue);
typedef bool PlatformAPI_QueueAllJobsComplete  (PlatformJobQueue *const queue);

typedef u32  PlatformAPI_AtomicCompareSwap(u32 volatile *dest, u32 swapVal, u32 compareVal);

////////////////////////////////////////////////////////////////////////////////
// Platform Locks
////////////////////////////////////////////////////////////////////////////////
typedef struct PlatformLock PlatformLock;

typedef PlatformLock *PlatformAPI_LockInit   (DqnMemStack *const stack);
typedef void          PlatformAPI_LockAcquire(PlatformLock *const lock);
typedef void          PlatformAPI_LockRelease(PlatformLock *const lock);
typedef void          PlatformAPI_LockDelete (PlatformLock *const lock);

////////////////////////////////////////////////////////////////////////////////
// Platform API for Game to Use
////////////////////////////////////////////////////////////////////////////////
typedef struct PlatformAPI
{
	PlatformAPI_FileOpen    *FileOpen;
	PlatformAPI_FileRead    *FileRead;
	PlatformAPI_FileWrite   *FileWrite;
	PlatformAPI_FileClose   *FileClose;
	PlatformAPI_Print       *Print;

	PlatformAPI_QueueAddJob            *QueueAddJob;
	PlatformAPI_QueueTryExecuteNextJob *QueueTryExecuteNextJob;
	PlatformAPI_QueueAllJobsComplete   *QueueAllJobsComplete;
	PlatformAPI_AtomicCompareSwap      *AtomicCompareSwap;

	PlatformAPI_LockInit    *LockInit;
	PlatformAPI_LockAcquire *LockAcquire;
	PlatformAPI_LockRelease *LockRelease;
	PlatformAPI_LockDelete  *LockDelete;

	PlatformAPI_DieGracefully *DieGracefully;
} PlatformAPI;

////////////////////////////////////////////////////////////////////////////////
// Platform Input
////////////////////////////////////////////////////////////////////////////////
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

typedef struct PlatformMouse
{
	i32 x;
	i32 y;
	KeyState leftBtn;
	KeyState rightBtn;
} PlatformMouse;

typedef struct PlatformFlags
{
	bool executableReloaded;
	bool canUseRdtsc;
	bool canUseSSE2;
} PlatformFlags;

typedef struct PlatformInput
{
	f32           deltaForFrame;
	f64           timeNowInS;
	PlatformFlags flags;

	PlatformAPI       api;
	PlatformMouse     mouse;
	PlatformJobQueue *jobQueue;
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

////////////////////////////////////////////////////////////////////////////////
// Platform Memory
////////////////////////////////////////////////////////////////////////////////
typedef struct PlatformMemory
{
	union {
		struct
		{
			DqnMemStack mainStack;
			DqnMemStack tempStack;
			DqnMemStack assetStack;
		};
		DqnMemStack stacks[3];
	};
	bool  isInit;
	void *context;
} PlatformMemory;

////////////////////////////////////////////////////////////////////////////////
// Platform Frame Buffer
////////////////////////////////////////////////////////////////////////////////
typedef struct PlatformRenderBuffer
{
	i32   width;
	i32   height;
	i32   bytesPerPixel;

	//  Pixel Format: XX RR GG BB
	void *memory;
} PlatformRenderBuffer;

#endif
