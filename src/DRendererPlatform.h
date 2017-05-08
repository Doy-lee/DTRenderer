#ifndef DRENDERER_PLATFORM_H
#define DRENDERER_PLATFORM_H

#include "dqn.h"

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
	f32 deltaForFrame;

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
