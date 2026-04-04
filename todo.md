# Radiary Enhancement Todos

**Reminder:** Make new additions modular if possible to maintain code organization and ease future maintenance.

- **Themed Generators like JWildfire (Toggable in Settings)**  
  Implement specialized random scene generators inspired by JWildfire's themed classes (e.g., Gnarl, Bubbles, Spirals). Add a settings toggle to enable/disable themed randomization, ensuring flames/paths are generated within curated aesthetic bounds for better variety and quality.

- **New "Generation" Panel with Batch Random Scene Generations like JWildfire (with Preview)**  
  Create a dedicated UI panel for batch-generating random scenes, similar to JWildfire's random flame batch feature. Include live previews of generated scenes, seed control for reproducibility, and options to save/select favorites during batch generation.

- **Add More Flame Variations (from flam3)**  
  Expand the variation library by porting additional variations from the flam3 specification (e.g., missing ones like Synth, Patterns, or advanced 3D variations). Update the `VariationType` enum and `ApplyVariation` function to support them, ensuring compatibility with existing flame rendering.

- **Better Gradient Picker**  
  Enhance the gradient editing interface with a more intuitive color picker, support for gradient stops manipulation (drag, add/remove), and previews. Allow importing/exporting gradients in formats compatible with JWildfire (.flame) for better usability and cross-tool workflows.

- **Better Path Editor, Maybe Make Points Editable through Viewport Preview (May be Hard, Please Help Check)**  
  Improve path editing with viewport-based point manipulation (e.g., click/drag control points directly in the preview). This could involve integrating with ImGui's drawing or adding custom input handling. (Note: This may be complex due to viewport interaction; requires checking ImGui capabilities and potential conflicts with existing camera controls.)

- **Search Filter for Presets**
  When the preset dropdown is open, change the dropdown header to an inline search box where the user can type to filter and select presets. Implement fuzzy matching or substring search for quick navigation through large preset lists.

- **File Picker Defaults for Saving .radiary Files**
  When saving or saving as a .radiary file, open the file picker at the Radiary presets folder by default. Remember the last saved location (e.g., via a config file) and use it for subsequent file picker openings to improve workflow efficiency.

- **Drag and Drop File Loading**
  Enable drag-and-drop support for loading .radiary or other supported file types (e.g., .flame) directly onto the application window. Hook into Windows messages (e.g., WM_DROPFILES) to handle file paths and trigger load operations seamlessly.

- **More Keyboard Shortcuts**
  Add additional keyboard shortcuts for common actions, such as Ctrl+S (save), Ctrl+R (random scene), Ctrl+O (open), Ctrl+N (new scene), and F5 (render). Integrate them into the existing `HandleShortcuts` function for improved workflow efficiency.

- **Confirmation Dialog Boxes for Destructive/Big Actions**
  Add confirmation dialogs for significant actions that could lose unsaved changes, including quitting with unsaved changes, clearing scenes with unsaved changes, and randomizing the scene with unsaved changes. Use ImGui modals to prevent accidental data loss and improve user safety.