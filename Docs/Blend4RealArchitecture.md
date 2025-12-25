# Blend4Real Plugin Architecture

Blend4Real is an Unreal Editor plugin that provides Blender-style viewport controls for object manipulation and camera navigation.

## Features

- **Transform Controls**: G (Grab/Move), R (Rotate), S (Scale) with axis constraints (X/Y/Z)
- **Camera Navigation**: MMB orbit, Shift+MMB pan, double-click focus
- **Selection Actions**: Shift+D duplicate with grab, X delete
- **Numeric Input**: Type exact values during transforms
- **Visual Feedback**: Axis constraint lines, transform info display

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                      FBlend4RealModule                          │
│  - Plugin entry point                                           │
│  - Menu/toolbar registration                                    │
│  - PIE session handling (auto-disable during play)              │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                  FBlend4RealInputProcessor                      │
│  - Implements IInputProcessor                                   │
│  - Thin dispatcher - routes input to controllers                │
│  - Manages enabled state                                        │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────────────┐
│ FNavigation     │ │ FTransform      │ │ FSelectionActions       │
│ Controller      │ │ Controller      │ │ Controller              │
├─────────────────┤ ├─────────────────┤ ├─────────────────────────┤
│ - Orbit camera  │ │ - G/R/S modes   │ │ - Duplicate + grab      │
│ - Pan camera    │ │ - Axis constrain│ │ - Delete selected       │
│ - Focus on hit  │ │ - Numeric input │ │                         │
│                 │ │ - Snapping      │ │ Depends on:             │
│                 │ │ - Visualization │ │ TransformController     │
└─────────────────┘ └─────────────────┘ └─────────────────────────┘
          │                   │                   │
          └───────────────────┼───────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Blend4RealUtils                            │
│  - Stateless utility functions                                  │
│  - Scene picking, ray casting                                   │
│  - Key detection (axis, numeric, transform)                     │
│  - Selection helpers                                            │
└─────────────────────────────────────────────────────────────────┘
```

## File Structure

```
Plugins/blend4real/Source/Blend4Real/
├── Public/
│   ├── Blend4Real.h                    # Module interface
│   ├── Blend4RealInputProcessor.h      # Input dispatcher
│   ├── Blend4RealUtils.h               # Stateless utilities
│   ├── FNavigationController.h         # Camera navigation
│   ├── FTransformController.h          # Object transforms
│   ├── FSelectionActionsController.h   # Delete/duplicate
│   ├── Blend4RealSettings.h            # Plugin settings (UObject)
│   ├── Blend4RealCommands.h            # UI commands
│   └── Blend4RealStyle.h               # UI styling
│
├── Private/
│   ├── Blend4Real.cpp                  # Module implementation
│   ├── Blend4RealInputProcessor.cpp    # Input routing
│   ├── Blend4RealUtils.cpp             # Utility implementations
│   ├── FNavigationController.cpp       # Navigation logic
│   ├── FTransformController.cpp        # Transform logic
│   ├── FSelectionActionsController.cpp # Action logic
│   ├── Blend4RealSettings.cpp          # Settings registration
│   ├── Blend4RealCommands.cpp          # Command definitions
│   └── Blend4RealStyle.cpp             # Style definitions
│
└── Blend4Real.Build.cs                 # Build configuration
```

## Component Details

### FBlend4RealModule
Entry point for the plugin. Responsibilities:
- Register/unregister the input processor
- Set up toolbar button and menu entries
- Subscribe to PIE events to disable during gameplay
- Toggle plugin enabled state

### FBlend4RealInputProcessor
Implements `IInputProcessor` to intercept editor input. Acts as a thin dispatcher:
- Routes keyboard events to `FTransformController` or `FSelectionActionsController`
- Routes mouse events to `FNavigationController` or `FTransformController`
- Maintains enabled state and controller references

### FNavigationController
Handles camera movement operations:
- **Orbit**: MMB rotates camera around a pivot point (selection center, mouse hit, or look-at)
- **Pan**: Shift+MMB translates camera parallel to view plane
- **Focus**: Double-click or Alt+MMB frames viewport on surface under cursor

### FTransformController
Handles object manipulation:
- **Modes**: Translation (G), Rotation (R), Scale (S)
- **Axis Constraints**: X/Y/Z keys lock to world axis, press twice for local
- **Numeric Input**: Type values for precise transforms
- **Snapping**: Respects editor grid settings, Ctrl inverts snap state
- **Visualization**: Draws axis lines and info popup during transforms
- **Undo/Redo**: Full transaction support

### FSelectionActionsController
Handles selection-based operations:
- **Duplicate**: Shift+D duplicates and enters grab mode immediately
- **Delete**: X deletes selected actors with undo support

### Blend4RealUtils
Stateless utility functions used across controllers:
- `GetEditorWorld()` / `GetActiveSceneView()` - Viewport access
- `ComputeSelectionPivot()` - Calculate selection center
- `ScenePickAtPosition()` - Raycast from screen to world
- `ProjectToSurface()` - Line trace against scene
- `IsTransformKey()` / `IsAxisKey()` / `IsNumericKey()` - Key detection
- `MarkSelectionModified()` - Undo system integration

## Settings

Plugin settings are exposed in **Project Settings > Plugins > Blend4Real**:
- Keybindings for all operations (transform, navigation, actions)
- Orbit mode (selection center, mouse hit, or viewport look-at)

## PIE Safety

The plugin automatically disables when Play-In-Editor starts:
1. `FEditorDelegates::BeginPIE` fires
2. Module stores current enabled state
3. Input processor is disabled
4. `FEditorDelegates::EndPIE` fires when play stops
5. Input processor is re-enabled if it was enabled before

This prevents crashes from editor-only APIs (viewport clients, level editor module) being called during gameplay.

## Dependencies

- **Core/CoreUObject/Engine**: Base Unreal types
- **Slate/SlateCore**: UI framework for info popup
- **InputCore**: Input types and key definitions
- **UnrealEd**: Editor APIs (GEditor, transactions)
- **LevelEditor**: Viewport access and level editing
- **ToolMenus**: Toolbar integration
- **EditorInteractiveToolsFramework**: Experimental ITF orbit support
