#include "Blend4Real.h"
#include "Blend4RealCommands.h"
#include "Blend4RealStyle.h"
#include "Blend4RealInputProcessor.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FBlend4RealModule"

void FBlend4RealModule::StartupModule()
{
	// Register styles and commands
	FBlend4RealStyle::Initialize();
	FBlend4RealStyle::ReloadTextures();
	FBlend4RealCommands::Register();

	// Registering Blender Controls handler (kinda weird)
	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FBlend4RealCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FBlend4RealModule::PluginButtonClicked),
		FCanExecuteAction(),
		FIsActionChecked::CreateRaw(this, &FBlend4RealModule::IsBlend4RealEnabled));

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FBlend4RealModule::RegisterMenus));

	//  Extension point for input handler
	if (GEditor)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}

	BlenderInputHandler = MakeShareable(new FBlend4RealInputProcessor());

	// Subscribe to PIE events to disable the input processor during gameplay
	BeginPIEDelegateHandle = FEditorDelegates::BeginPIE.AddRaw(this, &FBlend4RealModule::OnBeginPIE);
	EndPIEDelegateHandle = FEditorDelegates::EndPIE.AddRaw(this, &FBlend4RealModule::OnEndPIE);
}

void FBlend4RealModule::ShutdownModule()
{
	// Unsubscribe from PIE events
	FEditorDelegates::BeginPIE.Remove(BeginPIEDelegateHandle);
	FEditorDelegates::EndPIE.Remove(EndPIEDelegateHandle);

	// Unregister the input handler
	if (BlenderInputHandler.IsValid())
	{
		BlenderInputHandler.Reset();
	}

	// Unregister UI elements
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FBlend4RealStyle::Shutdown();
	FBlend4RealCommands::Unregister();
}

void FBlend4RealModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Add to the viewport toolbar (near snapping controls)
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.ViewportToolbar");
	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Left");

	// Add as a toolbar button
	FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		FBlend4RealCommands::Get().PluginAction,
		TAttribute<FText>(),
		TAttribute<FText>(),
		FSlateIcon(FBlend4RealStyle::GetStyleSetName(), "Blend4Real.PluginAction")
	));
	Entry.SetCommandList(PluginCommands);
}

bool FBlend4RealModule::IsBlend4RealEnabled() const
{
	return BlenderInputHandler.IsValid() && BlenderInputHandler->IsEnabled();
}

void FBlend4RealModule::PluginButtonClicked()
{
	// Toggle the Blender Controls
	if (BlenderInputHandler.IsValid())
	{
		BlenderInputHandler->ToggleEnabled();
	}
}

void FBlend4RealModule::OnBeginPIE(bool bIsSimulating)
{
	// Store current enabled state and disable if enabled
	if (BlenderInputHandler.IsValid() && BlenderInputHandler->IsEnabled())
	{
		bWasEnabledBeforePIE = true;
		BlenderInputHandler->ToggleEnabled();
	}
	else
	{
		bWasEnabledBeforePIE = false;
	}
}

void FBlend4RealModule::OnEndPIE(bool bIsSimulating)
{
	// Re-enable if it was enabled before PIE started
	if (bWasEnabledBeforePIE && BlenderInputHandler.IsValid() && !BlenderInputHandler->IsEnabled())
	{
		BlenderInputHandler->ToggleEnabled();		
	}
	bWasEnabledBeforePIE = false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlend4RealModule, Blend4Real)
