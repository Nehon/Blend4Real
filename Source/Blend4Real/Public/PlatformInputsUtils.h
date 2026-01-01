#pragma once
namespace PlatformInputs
{
	/** Transforms KeyEvent into real character to be able to use 0-9 input keys on AZERTY keyboards with no num pad*/
	TCHAR TranslateKeyWithModifiers(const FKeyEvent& KeyEvent);

	/** Initialize keyboard layout cache - call from module startup (main thread) */
	void InitializeKeyboardLayoutCache();

	/** Clean up keyboard layout cache - call from module shutdown */
	void ShutdownKeyboardLayoutCache();
}
