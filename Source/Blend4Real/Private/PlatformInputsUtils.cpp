#include "PlatformInputsUtils.h"
#if PLATFORM_MAC
#include <Carbon/Carbon.h>  // For UCKeyTranslate, TIS functions
#include "Mac/CocoaThread.h" // For run in main thread
#elif PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

// this utils file allows to detect numeric input on non QWERTY keyboards that don't have numpads.
// where numbers has to be typed with shift pressed down. In that case we need to know the actual character typed once
// it's been transformed by the platform (Windows and mac are supported)
namespace PlatformInputs
{
#if PLATFORM_MAC
	// Cached keyboard layout data (populated on main thread at startup)
	static CFDataRef CachedKeyboardLayoutData = nullptr;

	void InitKeyboardInMainThread()
	{
		TISInputSourceRef CurrentKeyboard = TISCopyCurrentKeyboardLayoutInputSource();
		if (CurrentKeyboard)
		{
			CFDataRef LayoutData = static_cast<CFDataRef>(TISGetInputSourceProperty(
				CurrentKeyboard,
				kTISPropertyUnicodeKeyLayoutData
			));
			if (LayoutData) { CachedKeyboardLayoutData = static_cast<CFDataRef>(CFRetain(LayoutData)); }
			CFRelease(CurrentKeyboard);
		}
	}


	void InitializeKeyboardLayoutCache()
	{
		// Rider disable All
		MainThreadCall(^{InitKeyboardInMainThread();}, true);
		// Rider restore All
	}

	void ShutdownKeyboardLayoutCache()
	{
		if (CachedKeyboardLayoutData)
		{
			CFRelease(CachedKeyboardLayoutData);
			CachedKeyboardLayoutData = nullptr;
		}
	}


	TCHAR TranslateKeyWithModifiers(const FKeyEvent& KeyEvent)
	{
		if (!CachedKeyboardLayoutData)
		{
			// Fallback to raw character if cache not ready
			return static_cast<TCHAR>(KeyEvent.GetCharacter());
		}

		// Build Mac modifier flags
		uint32 MacModifiers = 0;
		if (KeyEvent.IsShiftDown())
		{
			MacModifiers |= (shiftKey >> 8);
		}
		if (KeyEvent.IsControlDown())
		{
			MacModifiers |= (controlKey >> 8);
		}
		if (KeyEvent.IsAltDown())
		{
			MacModifiers |= (optionKey >> 8);
		}
		if (KeyEvent.IsCommandDown())
		{
			MacModifiers |= (cmdKey >> 8);
		}

		const UCKeyboardLayout* KeyboardLayout = (const UCKeyboardLayout*)(CFDataGetBytePtr(CachedKeyboardLayoutData));
		if (KeyboardLayout)
		{
			UniChar Buffer[4] = {0};
			UniCharCount BufferLength = 4;
			UInt32 DeadKeyState = 0;

			OSStatus Status = UCKeyTranslate(
				KeyboardLayout,
				static_cast<UInt16>(KeyEvent.GetKeyCode()),
				kUCKeyActionDown,
				MacModifiers,
				LMGetKbdType(),
				kUCKeyTranslateNoDeadKeysMask,
				&DeadKeyState,
				BufferLength,
				&BufferLength,
				Buffer
			);

			if (Status == noErr && BufferLength > 0)
			{
				return static_cast<TCHAR>(Buffer[0]);
			}
		}
		return 0;
	}
#elif PLATFORM_WINDOWS

	TCHAR TranslateKeyWithModifiers(const FKeyEvent& KeyEvent)
	{
		// Use ToUnicode Win32 API
		BYTE KeyboardState[256];
		::GetKeyboardState(KeyboardState);

		WCHAR Buffer[8] = {0};
		int Result = ::ToUnicode(
			KeyEvent.GetKeyCode(),
			::MapVirtualKey(KeyEvent.GetKeyCode(), MAPVK_VK_TO_VSC),
			KeyboardState,
			Buffer,
			8,
			0
		);

		return (Result > 0) ? Buffer[0] : 0;
	}
	void InitializeKeyboardLayoutCache()
	{
	}
	void ShutdownKeyboardLayoutCache()
	{
	}
#else

	TCHAR TranslateKeyWithModifiers(const FKeyEvent& KeyEvent)
	{
		// Fallback: return the raw character code
		return static_cast<TCHAR>(KeyEvent.GetCharacter());
	}
	void InitializeKeyboardLayoutCache()
	{
	}
	void ShutdownKeyboardLayoutCache()
	{
	}
#endif
}
