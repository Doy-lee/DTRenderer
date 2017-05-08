#include "DRenderer.h"
#include "DRendererPlatform.h"

#define DQN_IMPLEMENTATION
#define DQN_WIN32_IMPLEMENTATION
#include "dqn.h"

#define UNICODE
#define _UNICODE

#include <Pathcch.h>
#include <Windows.h>
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
FILE_SCOPE bool              globalRunning;

typedef struct Win32ExternalCode
{
	HMODULE            dll;
	FILETIME           lastWriteTime;

	DR_UpdateFunction *DR_Update;
} Win32ExternalCode;

enum Win32Menu
{
	Win32Menu_FileOpen = 4,
	Win32Menu_FileExit,
};

FILE_SCOPE void Win32DisplayRenderBitmap(Win32RenderBitmap renderBitmap,
                                         HDC deviceContext, LONG width,
                                         LONG height)
{
	HDC stretchDC = CreateCompatibleDC(deviceContext);
	SelectObject(stretchDC, renderBitmap.handle);
    // DQN_ASSERT(renderBitmap.width  == width);
    // DQN_ASSERT(renderBitmap.height == height);
	StretchBlt(deviceContext, 0, 0, width, height, stretchDC, 0, 0,
	           renderBitmap.width, renderBitmap.height, SRCCOPY);
	DeleteDC(stretchDC);
}

FILETIME Win32GetLastWriteTime(const char *const srcName)
{
	WIN32_FIND_DATA findData = {};
	FILETIME lastWriteTime   = {};
	HANDLE findHandle        = FindFirstFileA(srcName, &findData);
	if (findHandle != INVALID_HANDLE_VALUE)
	{
		lastWriteTime = findData.ftLastWriteTime;
		FindClose(findHandle);
	}

	return lastWriteTime;
}

FILE_SCOPE Win32ExternalCode Win32LoadExternalDLL(const char *const srcPath,
                                                  const char *const tmpPath,
                                                  const FILETIME lastWriteTime)
{
	Win32ExternalCode result = {};
	result.lastWriteTime     = lastWriteTime;
	CopyFile(srcPath, tmpPath, false);

	DR_UpdateFunction *updateFunction = NULL;
	result.dll = LoadLibraryA(tmpPath);
	if (result.dll)
	{
		updateFunction =
		    (DR_UpdateFunction *)GetProcAddress(result.dll, "DR_Update");
		if (updateFunction) result.DR_Update = updateFunction;
	}


	return result;
}

FILE_SCOPE void Win32UnloadExternalDLL(Win32ExternalCode *externalCode)
{

	if (externalCode->dll) FreeLibrary(externalCode->dll);
	externalCode->dll       = NULL;
	externalCode->DR_Update = NULL;
}

FILE_SCOPE void Win32CreateMenu(HWND window)
{
	HMENU menuBar  = CreateMenu();
	{ // File Menu
		HMENU menu = CreatePopupMenu();
		AppendMenu(menuBar, MF_STRING | MF_POPUP, (UINT_PTR)menu, "File");
		AppendMenu(menu, MF_STRING, Win32Menu_FileOpen, "Open");
		AppendMenu(menu, MF_STRING, Win32Menu_FileExit, "Exit");
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

// Return the index of the last slash
i32 Win32GetModuleDirectory(char *const buf, const u32 bufLen)
{
	if (!buf || bufLen == 0) return 0;
	u32 copiedLen = GetModuleFileName(NULL, buf, bufLen);
	if (copiedLen == bufLen)
	{
		DQN_WIN32_ERROR_BOX(
		    "GetModuleFileName() buffer maxed: Len of copied text is len "
		    "of supplied buffer.",
		    NULL);
		DQN_ASSERT(DQN_INVALID_CODE_PATH);
	}

	// NOTE: Should always work if GetModuleFileName works and we're running an
	// executable.
	i32 lastSlashIndex = 0;
	for (i32 i = copiedLen; i > 0; i--)
	{
		if (buf[i] == '\\')
		{
			lastSlashIndex = i;
			break;
		}
	}

	return lastSlashIndex;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nShowCmd)
{
	////////////////////////////////////////////////////////////////////////////
	// Initialise Win32 Window
	////////////////////////////////////////////////////////////////////////////
	WNDCLASSEXW wc =
	{
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
	const u32 MIN_HEIGHT = 600;
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
		DQN_ASSERT(globalRenderBitmap.bytesPerPixel >= 1);

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
		i32 lastSlashIndex =
		    Win32GetModuleDirectory(exeDir, DQN_ARRAY_COUNT(exeDir));
		DQN_ASSERT(lastSlashIndex + 1 < DQN_ARRAY_COUNT(exeDir));

		exeDir[lastSlashIndex + 1] = 0;
		u32 numCopied = Dqn_sprintf(dllPath, "%s%s", exeDir, "drenderer.dll");
		DQN_ASSERT(numCopied < DQN_ARRAY_COUNT(dllPath));

		numCopied =
		    Dqn_sprintf(dllTmpPath, "%s%s", exeDir, "drenderer_temp.dll");
		DQN_ASSERT(numCopied < DQN_ARRAY_COUNT(dllPath));
	}

	////////////////////////////////////////////////////////////////////////////
	// Update Loop
	////////////////////////////////////////////////////////////////////////////
	PlatformMemory platformMemory = {};
	DQN_ASSERT(DqnMemBuffer_Init(&platformMemory.permanentBuffer, DQN_MEGABYTE(1), true, 4) &&
	           DqnMemBuffer_Init(&platformMemory.transientBuffer, DQN_MEGABYTE(1), true, 4));

	const f32 TARGET_FRAMES_PER_S = 60.0f;
	f32 targetSecondsPerFrame     = 1 / TARGET_FRAMES_PER_S;
	f64 frameTimeInS              = 0.0f;
	globalRunning                 = true;

	while (globalRunning)
	{
		////////////////////////////////////////////////////////////////////////
		// Update State
		////////////////////////////////////////////////////////////////////////
		f64 startFrameTimeInS = DqnTime_NowInS();

		FILETIME lastWriteTime = Win32GetLastWriteTime(dllPath);
		if (CompareFileTime(&lastWriteTime, &dllCode.lastWriteTime) != 0)
		{
			Win32UnloadExternalDLL(&dllCode);
			dllCode = Win32LoadExternalDLL(dllPath, dllTmpPath, lastWriteTime);
		}

		{
			PlatformInput platformInput = {};
			platformInput.deltaForFrame = (f32)frameTimeInS;
			Win32ProcessMessages(mainWindow, &platformInput);

			PlatformRenderBuffer platformBuffer = {};
			platformBuffer.memory               = globalRenderBitmap.memory;
			platformBuffer.height               = globalRenderBitmap.height;
			platformBuffer.width                = globalRenderBitmap.width;
			platformBuffer.bytesPerPixel = globalRenderBitmap.bytesPerPixel;

			if (dllCode.DR_Update)
			{
				dllCode.DR_Update(&platformBuffer, &platformInput,
				                  &platformMemory);
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
			f64 workTimeInS = DqnTime_NowInS() - startFrameTimeInS;
			if (workTimeInS < targetSecondsPerFrame)
			{
				DWORD remainingTimeInMs =
				    (DWORD)((targetSecondsPerFrame - workTimeInS) * 1000);
				Sleep(remainingTimeInMs);
			}
		}

		frameTimeInS   = DqnTime_NowInS() - startFrameTimeInS;
		f32 msPerFrame = 1000.0f * (f32)frameTimeInS;

		char windowTitleBuffer[128] = {};
		Dqn_sprintf(windowTitleBuffer, "drenderer - dev - %5.2f ms/f",
		            msPerFrame);
		SetWindowTextA(mainWindow, windowTitleBuffer);
	}

	return 0;
}