![Blend4Real](Resources/Blend4RealLogo.png)

# Blend4Real - Blender style controls in Unreal Editor 

If, like me, you do a lot of back and forth between Blender and Unreal, and you never could wrap your head around Unreal's viewport controls, this plugin is for you.

## Disclaimer

This project started as a fork of [b3d-nav](https://github.com/ppmpreetham/b3d-nav) by [ppmpreetham](https://github.com/ppmpreetham). It has since been significantly expanded to support the minimum set of features from Blender that I needed to feel comfortable working in Unreal.\
This project is under MIT license, and is freely available on fab.com. It's given as is, you can use it, modify it for any purpose, without even mentionning me.\
In return, don't expect me to take in any feature requests. I will eventually fix bugs, and probably enhance it as I use it myself.
So if you want an extra feature, you'll have to implement it yourself, and optionnally do a Pull Request on this repo, and I'll consider merging it. 


## Blender-Style Features

### Object Manipulation
| Action | Key | Description |
|--------|-----|-------------|
| **Grab (Translate)** | `G` | Begin moving selected objects |
| **Rotate** | `R` | Begin rotating selected objects |
| **Scale** | `S` | Begin scaling selected objects |
| **Axis Constraint** | `X` / `Y` / `Z` | Constrain transform to world axis (press twice for local axis) |
| **Confirm Transform** | `Left Click` or `Enter` | Apply the transformation |
| **Cancel Transform** | `Right Click` or `Escape` | Cancel and revert to original transform |
| **Numeric Input** | `0-9`, `.`, `-` | Type exact values during transform |

### Transform Reset
| Action | Key | Description |
|--------|-----|-------------|
| **Reset Translation** | `Alt + G` | Reset selected objects position to origin |
| **Reset Rotation** | `Alt + R` | Reset selected objects rotation |
| **Reset Scale** | `Alt + S` | Reset selected objects scale to (1,1,1) |

### Object Actions
| Action | Key | Description |
|--------|-----|-------------|
| **Duplicate** | `Shift + D` | Duplicate selected objects and immediately grab |
| **Delete** | `X` | Delete selected objects |

### Camera Navigation
| Action | Key | Description |
|--------|-----|-------------|
| **Orbit** | `Middle Mouse Button` | Orbit camera around pivot point |
| **Pan** | `Shift + Middle Mouse Button` | Pan the camera |

### Orbit Modes
Configure how the camera orbits in **Edit > Project Settings > Plugins > Blend4Real**:
- **Default**: Use Unreal's default orbit behavior (Similar to Blender, orbits around the camera look at point)
- **Orbit Around Mouse Cursor Projection** (Default): Orbit around the point where the mouse cursor projects onto the scene
- **Orbit Around Selection**: Orbit around the center of selected actors

### Snapping
- Hold `Ctrl` during transform to toggle snapping (uses Unreal's grid settings)
- Surface snapping supported during translation (when enabled in viewport settings)

### Undo / Redo
- Every operation on actors can be Undone or Redone, using the Unreal Engine editor system.

## Extra Features

These features go beyond standard Blender controls:

| Action | Key                              | Description                                                                                                                              |
|--------|----------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| **Focus on Hit** | `Alt + Middle Mouse Button`      | Focus camera on the point under cursor (Slightly different as it Zooms on the focal point instead of just looking at it like in blender) |
| **Focus on Hit (Double Click)** | `Double Click Left Mouse Button` | Focus camera on the point under cursor (Sketchfab-style)                                                                                 |

## Non Intrusive
Can be toggled on/off anytime via a button in the viewport toolbar.

## Configurable Keybindings
You might be used to blender, but maybe you are using specific bindigns in blender itself.\
If That's the case you can configure them the same in the plugin settings\
All keybindings can be customized in **Edit > Project Settings > Plugins > Blend4Real** under the Keybindings categories:
- **Transform**: Begin Translation, Rotation, Scale
- **Transform Reset**: Reset Translation, Rotation, Scale
- **Objects**: Duplicate, Delete
- **Camera**: Orbit, Pan, Focus on Hit
- **Confirmation**: Apply Transform, Cancel Transform

The plugin will warn you if you assign conflicting keybindings.

## Compatibility
This plugin compiles against Unreal Engine 5.7.1, but it should be possible to retro compile it to 5.6 if you import it in your project's Plugin folder.

## Feature I'm contemplating implementing, or not. 
- **Restrict tansform on Plane, with shift + axis key**: It's a feature I never use in blender, and that I actually discovered while developing this plugin. I can see how it can be useful, but I'll wait to actually need it to implement it.
- **Change pivot point for transform**: Having a 3D cursor and being allowed to use median/individual origins or cursor as the center of the transformation. It's useful mostly in mesh eit mode to me, so I'm not sure it would be useful in Unreal.
- **Status bar at the bottom displaying contextual shortcuts**: It's something quite useful in Blender, but mostly if you're not used to it. if you're there I guess you already know the shortcuts.
- ~~box selection with LMB+drag~~ :  I decided to not do this one. In unreal it's hardcoded to LMB+Ctrl+Shift+Alt, unfortunately, just remapping it in not possible.\
  To have this it basicallt needs to be redone from scartch. The change is quite involved and would add a big amount of code for what would just be a key rebind. I figured it was not worth it.\
  Unreal has an experimental InteractiveToolFramework system, that makes me think that all this will drastically change in the future version, and will be a lot more modular than today. I'll revisit then.

