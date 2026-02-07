// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FBlend4RealStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static FName GetStyleSetName();
	static void ReloadTextures();
	static const ISlateStyle& Get();

private:
	static TSharedRef<class FSlateStyleSet> Create();
	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
