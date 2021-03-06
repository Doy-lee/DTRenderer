#define UNICODE
#define _UNICODE

#include "DTRenderer.h"
#include "DTRendererPlatform.h"

#define DQN_WIN32_IMPLEMENTATION
#define DQN_IMPLEMENTATION
#include "dqn.h"

#include <Windows.h>
#include <Windowsx.h> // For GET_X|Y_LPARAM(), mouse input
#include <Psapi.h>    // For win32 GetProcessMemoryInfo()

FILE_SCOPE PlatformMemory globalPlatformMemory;
FILE_SCOPE bool           globalRunning;

void Platform_DieGracefully() { globalRunning = false; }

////////////////////////////////////////////////////////////////////////////////
// Platform Atomics
////////////////////////////////////////////////////////////////////////////////
u32 Platform_AtomicCompareSwap(u32 volatile *dest, u32 swapVal, u32 compareVal)
{
	// TODO(doyle): Compile time assert
	DQN_ASSERT(sizeof(LONG) == sizeof(u32));
	u32 result =
	    (u32)InterlockedCompareExchange((LONG volatile *)dest, (LONG)swapVal, (LONG)compareVal);
	return result;
}

////////////////////////////////////////////////////////////////////////////////
// Platform Mutex/Lock
////////////////////////////////////////////////////////////////////////////////
typedef struct PlatformLock
{
	DqnLock dqnLock;
} PlatformLock;

PlatformLock *Platform_LockInit(DqnMemStack *const stack)
{
	PlatformLock *result = (PlatformLock *)DqnMemStack_Push(stack, sizeof(PlatformLock));
	if (result)
	{
		if (!DqnLock_Init(&result->dqnLock))
		{
			DqnMemStack_Pop(stack, result, sizeof(PlatformLock));
			return NULL;
		}
		else
		{
			return result;
		}
	}
	return NULL;
}

void Platform_LockAcquire(PlatformLock *const lock) { DqnLock_Acquire(&lock->dqnLock); }
void Platform_LockRelease(PlatformLock *const lock) { DqnLock_Release(&lock->dqnLock); }
void Platform_LockDelete (PlatformLock *const lock) { DqnLock_Delete(&lock->dqnLock); }

////////////////////////////////////////////////////////////////////////////////
// Platform Multi Threading
////////////////////////////////////////////////////////////////////////////////
struct PlatformJobQueue
{
	PlatformJob *volatile jobList;
	LONG         size;

	// NOTE: Modified by main+worker threads
	LONG   volatile jobToExecuteIndex;
	HANDLE volatile win32Semaphore;
	LONG   volatile numJobsToComplete;

	// NOTE: Modified by main thread ONLY
	LONG volatile jobInsertIndex;

};

bool Platform_QueueAddJob(PlatformJobQueue *const queue, const PlatformJob job)
{
	LONG newJobInsertIndex = (queue->jobInsertIndex + 1) % queue->size;
	if (newJobInsertIndex == queue->jobToExecuteIndex) return false;

	queue->jobList[queue->jobInsertIndex] = job;

	InterlockedIncrement(&queue->numJobsToComplete);
	ReleaseSemaphore(queue->win32Semaphore, 1, NULL);
	queue->jobInsertIndex = newJobInsertIndex;
	return true;
}

bool Platform_QueueTryExecuteNextJob(PlatformJobQueue *const queue)
{
	LONG originalJobToExecute = queue->jobToExecuteIndex;
	if (originalJobToExecute != queue->jobInsertIndex)
	{
		LONG newJobIndexForNextThread = (originalJobToExecute + 1) % queue->size;
		LONG index = InterlockedCompareExchange(&queue->jobToExecuteIndex, newJobIndexForNextThread,
		                                        originalJobToExecute);

		// NOTE: If we weren't successful at the interlock, another thread has
		// taken the work and we can't know if there's more work or not. So
		// irrespective of that result, return true to let the thread check
		// again for more work.
		if (index == originalJobToExecute)
		{
			PlatformJob job = queue->jobList[index];
			job.callback(queue, job.userData);
			InterlockedDecrement(&queue->numJobsToComplete);
		}

		return true;
	}

	return false;
}

bool Platform_QueueAllJobsComplete(PlatformJobQueue *const queue)
{
	bool result = (queue->numJobsToComplete == 0);
	return result;
}

FILE_SCOPE u32  volatile globalDebugCounter;
FILE_SCOPE bool volatile globalDebugCounterMemoize[2048];
FILE_SCOPE PlatformLock  globalDebugLock;
FILE_SCOPE void DebugWin32IncrementCounter(PlatformJobQueue *const queue, void *const userData)
{
	Platform_LockAcquire(&globalDebugLock);
	DQN_ASSERT(!globalDebugCounterMemoize[globalDebugCounter]);
	globalDebugCounterMemoize[globalDebugCounter] = true;
	globalDebugCounter++;
	u32 number = globalDebugCounter;
	Platform_LockRelease(&globalDebugLock);

	DqnWin32_OutputDebugString("Thread %d: Incrementing Number: %d\n", GetCurrentThreadId(),
	                           number);
}

DWORD WINAPI Win32ThreadCallback(void *lpParameter)
{
	PlatformJobQueue *queue = (PlatformJobQueue *)lpParameter;
	for (;;)
	{
		if (!Platform_QueueTryExecuteNextJob(queue))
		{
			WaitForSingleObjectEx(queue->win32Semaphore, INFINITE, false);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
// Platform I/O
////////////////////////////////////////////////////////////////////////////////
FILE_SCOPE inline PlatformFile DqnFileToPlatformFileInternal(const DqnFile file)
{
	PlatformFile result    = {};
	result.handle          = file.handle;
	result.size            = file.size;
	result.permissionFlags = file.permissionFlags;

	return result;
}

FILE_SCOPE inline DqnFile PlatformFileToDqnFileInternal(const PlatformFile file)
{
	DqnFile result = {};
	result.handle          = file.handle;
	result.size            = file.size;
	result.permissionFlags = file.permissionFlags;

	return result;
}

void Platform_Print(const char *const string)
{
	if (!string) return;
	OutputDebugStringA(string);
}

bool Platform_FileOpen(const char *const path, PlatformFile *const file, const u32 permissionFlags,
                       const enum PlatformFileAction fileAction)
{
	if (!path || !file) return false;
	DQN_ASSERT((permissionFlags &
	            ~(PlatformFilePermissionFlag_Write |
	              PlatformFilePermissionFlag_Read)) == 0);

	DQN_ASSERT((fileAction &
	            ~(PlatformFileAction_OpenOnly | PlatformFileAction_CreateIfNotExist |
	              PlatformFileAction_ClearIfExist)) == 0);

	DqnFile dqnFile = {};
	if (DqnFile_Open(path, &dqnFile, permissionFlags, (enum DqnFileAction)fileAction))
	{
		*file = DqnFileToPlatformFileInternal(dqnFile);
		return true;
	}

	return false;
}

size_t Platform_FileRead(PlatformFile *const file, u8 *const buf,
                         const size_t bytesToRead)
{
	if (!file || !buf) return 0;

	DqnFile dqnFile     = PlatformFileToDqnFileInternal(*file);
	size_t numBytesRead = DqnFile_Read(&dqnFile, buf, bytesToRead);

	return numBytesRead;
}

size_t Platform_FileWrite(PlatformFile *const file, u8 *const buf,
                         const size_t numBytesToWrite)
{
	if (!file || !buf) return 0;

	DqnFile dqnFile     = PlatformFileToDqnFileInternal(*file);
	size_t numBytesRead = DqnFile_Write(&dqnFile, buf, numBytesToWrite, 0);
	return numBytesRead;
}


void Platform_FileClose(PlatformFile *const file)
{
	if (!file) return;

	DqnFile dqnFile = PlatformFileToDqnFileInternal(*file);
	DqnFile_Close(&dqnFile);
}

////////////////////////////////////////////////////////////////////////////////
// Win32 Layer
////////////////////////////////////////////////////////////////////////////////
const char *const DLL_NAME     = "dtrenderer.dll";
const char *const DLL_TMP_NAME = "dtrenderer_temp.dll";

typedef struct Win32RenderBitmap
{
	BITMAPINFO  info;
	HBITMAP     handle;
	i32         width;
	i32         height;
	i32         bytesPerPixel;
	void       *memory;
} Win32RenderBitmap;

FILE_SCOPE Win32RenderBitmap globalRenderBitmap;

typedef struct Win32ExternalCode
{
	HMODULE            dll;
	FILETIME           lastWriteTime;

	DTR_UpdateFunction *DTR_Update;
} Win32ExternalCode;

enum Win32Menu
{
	Win32Menu_FileOpen = 4,
	Win32Menu_FileFlushMemory,
	Win32Menu_FileExit,
};

FILE_SCOPE void Win32DisplayRenderBitmap(Win32RenderBitmap renderBitmap,
                                         HDC deviceContext, LONG width,
                                         LONG height)
{
#if 0
	HDC stretchDC = CreateCompatibleDC(deviceContext);
	SelectObject(stretchDC, renderBitmap.handle);
    // DQN_ASSERT(renderBitmap.width  == width);
    // DQN_ASSERT(renderBitmap.height == height);
	StretchBlt(deviceContext, 0, 0, width, height, stretchDC, 0, 0,
	           renderBitmap.width, renderBitmap.height, SRCCOPY);
	DeleteDC(stretchDC);
#else
	StretchDIBits(deviceContext, 0, 0, width, height, 0, 0, renderBitmap.width,
	              renderBitmap.height, renderBitmap.memory,
	              &renderBitmap.info, DIB_RGB_COLORS, SRCCOPY);
#endif
}

FILETIME Win32GetLastWriteTime(const char *const srcName)
{
	FILETIME lastWriteTime               = {};
	WIN32_FILE_ATTRIBUTE_DATA attribData = {};
	if (GetFileAttributesExA(srcName, GetFileExInfoStandard, &attribData) != 0)
	{
		lastWriteTime = attribData.ftLastWriteTime;
	}

	return lastWriteTime;
}

FILE_SCOPE Win32ExternalCode Win32LoadExternalDLL(const char *const srcPath,
                                                  const char *const tmpPath,
                                                  const FILETIME lastWriteTime)
{
	Win32ExternalCode result = {};
	result.lastWriteTime     = lastWriteTime;
	CopyFileA(srcPath, tmpPath, false);

	DTR_UpdateFunction *updateFunction = NULL;
	result.dll                        = LoadLibraryA(tmpPath);
	if (result.dll)
	{
		updateFunction =
		    (DTR_UpdateFunction *)GetProcAddress(result.dll, "DTR_Update");
		if (updateFunction) result.DTR_Update = updateFunction;
	}
	else
	{
		DqnWin32_DisplayLastError("LoadLibraryA failed");
	}

	return result;
}

FILE_SCOPE void Win32UnloadExternalDLL(Win32ExternalCode *externalCode)
{

	if (externalCode->dll) FreeLibrary(externalCode->dll);
	externalCode->dll       = NULL;
	externalCode->DTR_Update = NULL;
}

FILE_SCOPE void Win32CreateMenu(HWND window)
{
	HMENU menuBar  = CreateMenu();
	{ // File Menu
		HMENU menu = CreatePopupMenu();
		AppendMenuA(menuBar, MF_STRING | MF_POPUP, (UINT_PTR)menu, "File");
		AppendMenuA(menu, MF_STRING, Win32Menu_FileOpen, "Open");
		AppendMenuA(menu, MF_STRING, Win32Menu_FileFlushMemory, "Flush Memory");
		AppendMenuA(menu, MF_STRING, Win32Menu_FileExit, "Exit");
	}
	SetMenu(window, menuBar);
}

FILE_SCOPE LRESULT CALLBACK Win32MainProcCallback(HWND window, UINT msg,
                                                  WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;
	switch (msg)
	{
		case WM_CREATE:
		{
			Win32CreateMenu(window);
		}
		break;

		case WM_CLOSE:
		case WM_DESTROY:
		{
			globalRunning = false;
		}
		break;

		case WM_PAINT:
		{
			PAINTSTRUCT paint;
			HDC deviceContext = BeginPaint(window, &paint);

			LONG renderWidth, renderHeight;
			DqnWin32_GetClientDim(window, &renderWidth, &renderHeight);

			DqnV2 ratio = DqnV2_2i(globalRenderBitmap.width, globalRenderBitmap.height);
			DqnV2 newDim = DqnV2_ConstrainToRatio(DqnV2_2i(renderWidth, renderHeight), ratio);
			renderWidth  = (LONG)newDim.w;
			renderHeight = (LONG)newDim.h;

			Win32DisplayRenderBitmap(globalRenderBitmap, deviceContext,
			                         renderWidth, renderHeight);
			EndPaint(window, &paint);
			break;
		}

		default:
		{
			result = DefWindowProcW(window, msg, wParam, lParam);
		}
		break;
	}

	return result;
}

FILE_SCOPE inline void Win32UpdateKey(KeyState *key, bool isDown)
{
	if (key->endedDown != isDown)
	{
		key->endedDown = isDown;
		key->halfTransitionCount++;
	}
}

FILE_SCOPE void Win32HandleMenuMessages(HWND window, MSG msg,
                                        PlatformInput *input)
{
	switch (LOWORD(msg.wParam))
	{
		case Win32Menu_FileExit:
		{
			globalRunning = false;
		}
		break;

		case Win32Menu_FileFlushMemory:
		{
			DqnMemStack memStacks[DQN_ARRAY_COUNT(globalPlatformMemory.stacks)] = {};
			for (i32 i = 0; i < DQN_ARRAY_COUNT(globalPlatformMemory.stacks); i++)
			{
				DqnMemStack *stack = &globalPlatformMemory.stacks[i];
				while (stack->block->prevBlock)
					DqnMemStack_FreeLastBlock(stack);

				DqnMemStack_ClearCurrBlock(stack, true);
				memStacks[i] = *stack;
			}

			PlatformMemory empty = {};
			globalPlatformMemory = empty;
			for (i32 i = 0; i < DQN_ARRAY_COUNT(globalPlatformMemory.stacks); i++)
				globalPlatformMemory.stacks[i] = memStacks[i];
		}
		break;

		case Win32Menu_FileOpen:
		{
#if 0
			wchar_t fileBuffer[MAX_PATH] = {};
			OPENFILENAME openDialogInfo  = {};
			openDialogInfo.lStructSize   = sizeof(OPENFILENAME);
			openDialogInfo.hwndOwner     = window;
			openDialogInfo.lpstrFile     = fileBuffer;
			openDialogInfo.nMaxFile      = DQN_ARRAY_COUNT(fileBuffer);
			openDialogInfo.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

			if (GetOpenFileName(&openDialogInfo) != 0)
			{
			}
#endif
		}
		break;

		default:
		{
			DQN_ASSERT(DQN_INVALID_CODE_PATH);
		}
		break;
	}
}

FILE_SCOPE void Win32ProcessMessages(HWND window, PlatformInput *input)
{
	MSG msg;
	while (PeekMessage(&msg, window, 0, 0, PM_REMOVE))
	{
		switch (msg.message)
		{
			case WM_COMMAND:
			{
				Win32HandleMenuMessages(window, msg, input);
			}
			break;

			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			{
				bool isDown = (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN);

				if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONUP)
				{
					Win32UpdateKey(&input->mouse.leftBtn, isDown);
				}
				else if (msg.message == WM_RBUTTONDOWN || msg.message == WM_RBUTTONUP)
				{
					Win32UpdateKey(&input->mouse.rightBtn, isDown);
				}
			}
			break;

			case WM_MOUSEMOVE:
			{
				LONG height;
				DqnWin32_GetClientDim(window, NULL, &height);

				input->mouse.x = GET_X_LPARAM(msg.lParam);
				input->mouse.y = height - GET_Y_LPARAM(msg.lParam);
			}
			break;

			case WM_SYSKEYDOWN:
			case WM_SYSKEYUP:
			case WM_KEYDOWN:
			case WM_KEYUP:
			{
				bool isDown = (msg.message == WM_KEYDOWN);
				switch (msg.wParam)
				{
					case VK_UP:    Win32UpdateKey(&input->up, isDown); break;
					case VK_DOWN:  Win32UpdateKey(&input->down, isDown); break;
					case VK_LEFT:  Win32UpdateKey(&input->left, isDown); break;
					case VK_RIGHT: Win32UpdateKey(&input->right, isDown); break;

					case '1': Win32UpdateKey(&input->key_1, isDown); break;
					case '2': Win32UpdateKey(&input->key_2, isDown); break;
					case '3': Win32UpdateKey(&input->key_3, isDown); break;
					case '4': Win32UpdateKey(&input->key_4, isDown); break;

					case 'Q': Win32UpdateKey(&input->key_q, isDown); break;
					case 'W': Win32UpdateKey(&input->key_w, isDown); break;
					case 'E': Win32UpdateKey(&input->key_e, isDown); break;
					case 'R': Win32UpdateKey(&input->key_r, isDown); break;

					case 'A': Win32UpdateKey(&input->key_a, isDown); break;
					case 'S': Win32UpdateKey(&input->key_s, isDown); break;
					case 'D': Win32UpdateKey(&input->key_d, isDown); break;
					case 'F': Win32UpdateKey(&input->key_f, isDown); break;

					case 'Z': Win32UpdateKey(&input->key_z, isDown); break;
					case 'X': Win32UpdateKey(&input->key_x, isDown); break;
					case 'C': Win32UpdateKey(&input->key_c, isDown); break;
					case 'V': Win32UpdateKey(&input->key_v, isDown); break;

					case VK_ESCAPE:
					{
						Win32UpdateKey(&input->escape, isDown);
						if (input->escape.endedDown) globalRunning = false;
					}
					break;

					default: break;
				}
			}
			break;

			default:
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			};
		}
	}
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{

	////////////////////////////////////////////////////////////////////////////
	// Initialise Win32 Window
	////////////////////////////////////////////////////////////////////////////
	WNDCLASSEXW wc = {
	    sizeof(WNDCLASSEX),
	    CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
	    Win32MainProcCallback,
	    0, // int cbClsExtra
	    0, // int cbWndExtra
	    hInstance,
	    LoadIcon(NULL, IDI_APPLICATION),
	    LoadCursor(NULL, IDC_ARROW),
	    GetSysColorBrush(COLOR_3DFACE),
	    L"", // LPCTSTR lpszMenuName
	    L"DRendererClass",
	    NULL, // HICON hIconSm
	};

	if (!RegisterClassExW(&wc))
	{
		DqnWin32_DisplayLastError("RegisterClassEx() failed.");
		return -1;
	}

	// NOTE: Regarding Window Sizes
	// If you specify a window size, e.g. 800x600, Windows regards the 800x600
	// region to be inclusive of the toolbars and side borders. So in actuality,
	// when you blit to the screen blackness, the area that is being blitted to
	// is slightly smaller than 800x600. Windows provides a function to help
	// calculate the size you'd need by accounting for the window style.
	const u32 MIN_WIDTH  = 800;
	const u32 MIN_HEIGHT = 800;
	RECT rect   = {};
	rect.right  = MIN_WIDTH;
	rect.bottom = MIN_HEIGHT;
	DWORD windowStyle    = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRect(&rect, windowStyle, true);

	HWND mainWindow = CreateWindowExW(
	    0, wc.lpszClassName, L"DRenderer", windowStyle, CW_USEDEFAULT,
	    CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr,
	    nullptr, hInstance, nullptr);

	if (!mainWindow)
	{
		DqnWin32_DisplayLastError("CreateWindowEx() failed.");
		return -1;
	}

	{ // Initialise the renderbitmap
		BITMAPINFOHEADER header = {};
		header.biSize           = sizeof(BITMAPINFOHEADER);
		header.biWidth          = MIN_WIDTH;
		header.biHeight         = MIN_HEIGHT;
		header.biPlanes         = 1;
		header.biBitCount       = 32;
		header.biCompression    = BI_RGB; // uncompressed bitmap

		globalRenderBitmap.info.bmiHeader = header;
		globalRenderBitmap.width          = header.biWidth;
		globalRenderBitmap.height         = header.biHeight;
		globalRenderBitmap.bytesPerPixel  = header.biBitCount / 8;
		if (!DQN_ASSERT(globalRenderBitmap.bytesPerPixel >= 1)) return -1;

		HDC deviceContext         = GetDC(mainWindow);
		globalRenderBitmap.handle = CreateDIBSection(
		    deviceContext, &globalRenderBitmap.info, DIB_RGB_COLORS,
		    &globalRenderBitmap.memory, NULL, NULL);
		ReleaseDC(mainWindow, deviceContext);
	}

	if (!globalRenderBitmap.memory)
	{
		DqnWin32_DisplayLastError("CreateDIBSection() failed");
		return -1;
	}

	////////////////////////////////////////////////////////////////////////////
	// Make DLL Path
	////////////////////////////////////////////////////////////////////////////
	Win32ExternalCode dllCode = {};
	char dllPath[MAX_PATH]    = {};
	char dllTmpPath[MAX_PATH] = {};
	{
		char exeDir[MAX_PATH] = {};
		i32 lastSlashIndex    = DqnWin32_GetEXEDirectory(exeDir, DQN_ARRAY_COUNT(exeDir));
		if (DQN_ASSERT_MSG(lastSlashIndex != -1, "Not enough space in buffer for exe path"))
		{
			exeDir[lastSlashIndex + 1] = 0;
			u32 dllNumCopied           = Dqn_sprintf(dllPath, "%s%s", exeDir, DLL_NAME);
			u32 dllTmpNumCopied        = Dqn_sprintf(dllTmpPath, "%s%s", exeDir, DLL_TMP_NAME);

			if (!DQN_ASSERT_MSG((dllNumCopied    < DQN_ARRAY_COUNT(dllPath)) &&
			                    (dllTmpNumCopied < DQN_ARRAY_COUNT(dllPath)),
			                    "Out of space to form DLL path"))
			{
				Platform_DieGracefully();
			}
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Platform Data Pre-amble
	////////////////////////////////////////////////////////////////////////////
	bool memoryInitResult =
	    DqnMemStack_Init(&globalPlatformMemory.mainStack, DQN_MEGABYTE(4), true, 4) |
	    DqnMemStack_Init(&globalPlatformMemory.tempStack, DQN_MEGABYTE(4), true, 4) |
	    DqnMemStack_Init(&globalPlatformMemory.assetStack, DQN_MEGABYTE(4), true, 4);
	if (!DQN_ASSERT_MSG(memoryInitResult, "Unable to allocate DTRenderer globalPlatformMemory stacks"))
	{
		Platform_DieGracefully();
	}

	PlatformAPI platformAPI   = {};
	platformAPI.DieGracefully = Platform_DieGracefully;

	platformAPI.FileOpen  = Platform_FileOpen;
	platformAPI.FileRead  = Platform_FileRead;
	platformAPI.FileWrite = Platform_FileWrite;
	platformAPI.FileClose = Platform_FileClose;
	platformAPI.Print     = Platform_Print;

	platformAPI.QueueAddJob            = Platform_QueueAddJob;
	platformAPI.QueueTryExecuteNextJob = Platform_QueueTryExecuteNextJob;
	platformAPI.QueueAllJobsComplete   = Platform_QueueAllJobsComplete;

	platformAPI.AtomicCompareSwap = Platform_AtomicCompareSwap;

	platformAPI.LockInit    = Platform_LockInit;
	platformAPI.LockAcquire = Platform_LockAcquire;
	platformAPI.LockRelease = Platform_LockRelease;
	platformAPI.LockDelete  = Platform_LockDelete;

	PlatformJobQueue jobQueue = {};

	PlatformInput platformInput     = {};
	platformInput.api               = platformAPI;
	platformInput.jobQueue          = &jobQueue;
	platformInput.flags.canUseSSE2  = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE);
	platformInput.flags.canUseRdtsc = IsProcessorFeaturePresent(PF_RDTSC_INSTRUCTION_AVAILABLE);

	// Threading
	PlatformJob jobQueueMemory[512] = {};
	{
		DqnMemStackTempRegion memRegion;
		if (!DQN_ASSERT(DqnMemStackTempRegion_Begin(&memRegion, &globalPlatformMemory.tempStack)))
			Platform_DieGracefully();

		////////////////////////////////////////////////////////////////////////
		// Query CPU Cores
		////////////////////////////////////////////////////////////////////////
		i32 numCores, numThreadsPerCore;
		DqnWin32_GetNumThreadsAndCores(&numCores, &numThreadsPerCore);

		////////////////////////////////////////////////////////////////////////
		// Threading
		////////////////////////////////////////////////////////////////////////
		jobQueue.jobList = jobQueueMemory;
		jobQueue.size    = DQN_ARRAY_COUNT(jobQueueMemory);

		// NOTE: InterlockedIncrement requires things to be on 32bit boundaries.
		DQN_ASSERT(((size_t)&jobQueue.jobToExecuteIndex) % 4 == 0);

		// NOTE: (numCores - 1), 1 core is already exclusively for main thread
		i32 availableThreads = (numCores - 1) * numThreadsPerCore;
		if (availableThreads <= 0) availableThreads = 1;
		
		jobQueue.win32Semaphore = CreateSemaphore(NULL, 0, availableThreads, NULL);
		if (jobQueue.win32Semaphore)
		{
			// Create threads
			for (i32 i = 0; i < availableThreads; i++)
			{
				const i32 USE_DEFAULT_STACK_SIZE = 0;
				void *threadParam                = &jobQueue;
				HANDLE handle = CreateThread(NULL, USE_DEFAULT_STACK_SIZE, Win32ThreadCallback,
				                             threadParam, 0, NULL);
				CloseHandle(handle);
			}

#if 1
			DQN_ASSERT_HARD(DqnLock_Init(&globalDebugLock.dqnLock));
			for (i32 i = 0; i < DQN_ARRAY_COUNT(globalDebugCounterMemoize); i++)
			{
				PlatformJob job = {};
				job.callback    = DebugWin32IncrementCounter;
				while (!Platform_QueueAddJob(&jobQueue, job))
				{
					Platform_QueueTryExecuteNextJob(&jobQueue);
				}
			}

			while (Platform_QueueTryExecuteNextJob(&jobQueue))
				;

			for (i32 i = 0; i < DQN_ARRAY_COUNT(globalDebugCounterMemoize); i++)
				DQN_ASSERT(globalDebugCounterMemoize[i]);

			DqnWin32_OutputDebugString("\nFinal incremented value: %d\n", globalDebugCounter);
			DQN_ASSERT(globalDebugCounter == DQN_ARRAY_COUNT(globalDebugCounterMemoize));
#endif
		}
		else
		{
			// TODO(doyle): Semaphore failed.
			DqnWin32_DisplayLastError("CreateSemaphore() failed");
		}
	}

	////////////////////////////////////////////////////////////////////////////
	// Update Loop
	////////////////////////////////////////////////////////////////////////////
	const f32 TARGET_FRAMES_PER_S = 60.0f;
	f32 targetSecondsPerFrame     = 1 / TARGET_FRAMES_PER_S;
	f64 frameTimeInS              = 0.0f;
	globalRunning                 = true;

	while (globalRunning)
	{
		////////////////////////////////////////////////////////////////////////
		// Update State
		////////////////////////////////////////////////////////////////////////
		f64 startFrameTimeInS = DqnTimer_NowInS();

		FILETIME lastWriteTime = Win32GetLastWriteTime(dllPath);
		if (CompareFileTime(&lastWriteTime, &dllCode.lastWriteTime) != 0)
		{
			Win32UnloadExternalDLL(&dllCode);
			dllCode = Win32LoadExternalDLL(dllPath, dllTmpPath, lastWriteTime);
			platformInput.flags.executableReloaded = true;
		}

		{
			platformInput.timeNowInS    = DqnTimer_NowInS();
			platformInput.deltaForFrame = (f32)frameTimeInS;
			Win32ProcessMessages(mainWindow, &platformInput);

			PlatformRenderBuffer platformBuffer = {};
			platformBuffer.memory               = globalRenderBitmap.memory;
			platformBuffer.height               = globalRenderBitmap.height;
			platformBuffer.width                = globalRenderBitmap.width;
			platformBuffer.bytesPerPixel = globalRenderBitmap.bytesPerPixel;

			if (dllCode.DTR_Update)
			{
				dllCode.DTR_Update(&platformBuffer, &platformInput, &globalPlatformMemory);
			}
		}

		////////////////////////////////////////////////////////////////////////
		// Rendering
		////////////////////////////////////////////////////////////////////////
		{
			LONG renderWidth, renderHeight;
			DqnWin32_GetClientDim(mainWindow, &renderWidth, &renderHeight);

			DqnV2 ratio  = DqnV2_2i(globalRenderBitmap.width, globalRenderBitmap.height);
			DqnV2 newDim = DqnV2_ConstrainToRatio(DqnV2_2i(renderWidth, renderHeight), ratio);
			renderWidth  = (LONG)newDim.w;
			renderHeight = (LONG)newDim.h;

			HDC deviceContext = GetDC(mainWindow);
			Win32DisplayRenderBitmap(globalRenderBitmap, deviceContext,
			                         renderWidth, renderHeight);
			ReleaseDC(mainWindow, deviceContext);
		}

		////////////////////////////////////////////////////////////////////////
		// Frame Limiting
		////////////////////////////////////////////////////////////////////////
		{
			f64 workTimeInS = DqnTimer_NowInS() - startFrameTimeInS;
			if (workTimeInS < targetSecondsPerFrame)
			{
				DWORD remainingTimeInMs =
				    (DWORD)((targetSecondsPerFrame - workTimeInS) * 1000);
				Sleep(remainingTimeInMs);
			}
		}

		frameTimeInS        = DqnTimer_NowInS() - startFrameTimeInS;
		f32 msPerFrame      = 1000.0f * (f32)frameTimeInS;
		f32 framesPerSecond = 1.0f / (f32)frameTimeInS;

		////////////////////////////////////////////////////////////////////////
		// Misc
		////////////////////////////////////////////////////////////////////////
		// Get Win32 reported mem usage
		PROCESS_MEMORY_COUNTERS memCounter = {};
		GetProcessMemoryInfo(GetCurrentProcess(), &memCounter, sizeof(memCounter));

		// Update title bar
		char windowTitleBuffer[128] = {};
		Dqn_sprintf(windowTitleBuffer, "drenderer - dev - %5.2f ms/f - %5.2f fps - mem %'dkb", msPerFrame, framesPerSecond,
		            (u32)(memCounter.PagefileUsage / 1024.0f));
		SetWindowTextA(mainWindow, windowTitleBuffer);
	}

	return 0;
}

