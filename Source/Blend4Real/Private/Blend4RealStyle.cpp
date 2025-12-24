#include "Blend4RealStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FBlend4RealStyle::StyleInstance = nullptr;

void FBlend4RealStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FBlend4RealStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FBlend4RealStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("Blend4RealStyle"));
	return StyleSetName;
}

void FBlend4RealStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FBlend4RealStyle::Get()
{
	return *StyleInstance;
}

TSharedRef<FSlateStyleSet> FBlend4RealStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("Blend4RealStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("Blend4Real")->GetBaseDir() / TEXT("Resources"));

	// Default icon size is 40x40
	Style->Set("Blend4Real.PluginAction",
	           new FSlateImageBrush(Style->RootToContentDir(TEXT("Blend4RealIcon"), TEXT(".png")),
	                                FVector2D(40.0f, 40.0f)));

	return Style;
}
