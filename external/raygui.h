/*******************************************************************************************
*
*   raygui v4.5-dev - A simple and easy-to-use immediate-mode gui library
*
*   DESCRIPTION:
*   raygui is a tools-dev-focused immediate-mode-gui library based on raylib but also
*   available as a standalone library, as long as input and drawing functions are provided.
*
*   FEATURES:
*   - Immediate-mode gui, minimal retained data
*   - +25 controls provided (basic and advanced)
*   - Styling system for colors, font and metrics
*   - Icons supported, embedded as a 1-bit icons pack
*   - Standalone mode option (custom input/graphics backend)
*   - Multiple support tools provided for raygui development
*
*   POSSIBLE IMPROVEMENTS:
*   - Better standalone mode API for easy plug of custom backends
*   - Externalize required inputs, allow user easier customization
*
*   LIMITATIONS:
*   - No editable multi-line word-wraped text box supported
*   - No auto-layout mechanism, up to the user to define controls position and size
*   - Standalone mode requires library modification and some user work to plug another backend
*
*   NOTES:
*   - WARNING: GuiLoadStyle() and GuiLoadStyle{Custom}() functions, allocate memory for
*   font atlas recs and glyphs, freeing that memory is (usually) up to the user,
*   no unload function is explicitly provided... but note that GuiLoadStyleDefault() unloads
*   by default any previously loaded font (texture, recs, glyphs).
*   - Global UI alpha (guiAlpha) is applied inside GuiDrawRectangle() and GuiDrawText() functions
*
*   CONTROLS PROVIDED:
*   # Container/separators Controls
*   - WindowBox --> StatusBar, Panel
*   - GroupBox --> Line
*   - Line
*   - Panel --> StatusBar
*   - ScrollPanel --> StatusBar
*   - TabBar --> Button
*
*   # Basic Controls
*   - Label
*   - LabelButton --> Label
*   - Button
*   - Toggle
*   - ToggleGroup --> Toggle
*   - ToggleSlider
*   - CheckBox
*   - ComboBox
*   - DropdownBox
*   - TextBox
*   - ValueBox --> TextBox
*   - Spinner --> Button, ValueBox
*   - Slider
*   - SliderBar --> Slider
*   - ProgressBar
*   - StatusBar
*   - DummyRec
*   - Grid
*
*   # Advance Controls
*   - ListView
*   - ColorPicker --> ColorPanel, ColorBarHue
*   - MessageBox --> Window, Label, Button
*   - TextInputBox --> Window, Label, TextBox, Button
*
*   It also provides a set of functions for styling the controls based on its properties (size, color).
*
*
*   RAYGUI STYLE (guiStyle):
*   raygui uses a global data array for all gui style properties (allocated on data segment by default),
*   when a new style is loaded, it is loaded over the global style... but a default gui style could always be
*   recovered with GuiLoadStyleDefault() function, that overwrites the current style to the default one
*
*   The global style array size is fixed and depends on the number of controls and properties:
*
*   static unsigned int guiStyle[RAYGUI_MAX_CONTROLS*(RAYGUI_MAX_PROPS_BASE + RAYGUI_MAX_PROPS_EXTENDED)];
*
*   guiStyle size is by default: 16*(16 + 8) = 384*4 = 1536 bytes = 1.5 KB
*
*   Note that the first set of BASE properties (by default guiStyle[0..15]) belong to the generic style
*   used for all controls, when any of those base values is set, it is automatically populated to all
*   controls, so, specific control values overwriting generic style should be set after base values.
*
*   After the first BASE set we have the EXTENDED properties (by default guiStyle[16..23]), those
*   properties are actually common to all controls and can not be overwritten individually (like BASE ones)
*   Some of those properties are: TEXT_SIZE, TEXT_SPACING, LINE_COLOR, BACKGROUND_COLOR
*
*   Custom control properties can be defined using the EXTENDED properties for each independent control.
*
*   TOOL: rGuiStyler is a visual tool to customize raygui style: github.com/raysan5/rguistyler
*
*
*   RAYGUI ICONS (guiIcons):
*   raygui could use a global array containing icons data (allocated on data segment by default),
*   a custom icons set could be loaded over this array using GuiLoadIcons(), but loaded icons set
*   must be same RAYGUI_ICON_SIZE and no more than RAYGUI_ICON_MAX_ICONS will be loaded
*
*   Every icon is codified in binary form, using 1 bit per pixel, so, every 16x16 icon
*   requires 8 integers (16*16/32) to be stored in memory.
*
*   When the icon is draw, actually one quad per pixel is drawn if the bit for that pixel is set.
*
*   The global icons array size is fixed and depends on the number of icons and size:
*
*   static unsigned int guiIcons[RAYGUI_ICON_MAX_ICONS*RAYGUI_ICON_DATA_ELEMENTS];
*
*   guiIcons size is by default: 256*(16*16/32) = 2048*4 = 8192 bytes = 8 KB
*
*   TOOL: rGuiIcons is a visual tool to customize/create raygui icons: github.com/raysan5/rguiicons
*
*   RAYGUI LAYOUT:
*   raygui currently does not provide an auto-layout mechanism like other libraries,
*   layouts must be defined manually on controls drawing, providing the right bounds Rectangle for it.
*
*   TOOL: rGuiLayout is a visual tool to create raygui layouts: github.com/raysan5/rguilayout
*
*   CONFIGURATION:
*   #define RAYGUI_IMPLEMENTATION
*   Generates the implementation of the library into the included file.
*   If not defined, the library is in header only mode and can be included in other headers
*   or source files without problems. But only ONE file should hold the implementation.
*
*   #define RAYGUI_STANDALONE
*   Avoid raylib.h header inclusion in this file. Data types defined on raylib are defined
*   internally in the library and input management and drawing functions must be provided by
*   the user (check library implementation for further details).
*
*   #define RAYGUI_NO_ICONS
*   Avoid including embedded ricons data (256 icons, 16x16 pixels, 1-bit per pixel, 2KB)
*
*   #define RAYGUI_CUSTOM_ICONS
*   Includes custom ricons.h header defining a set of custom icons,
*   this file can be generated using rGuiIcons tool
*
*   #define RAYGUI_DEBUG_RECS_BOUNDS
*   Draw control bounds rectangles for debug
*
*   #define RAYGUI_DEBUG_TEXT_BOUNDS
*   Draw text bounds rectangles for debug
*
*   VERSIONS HISTORY:
*   5.0-dev (2025) Current dev version...
*   ADDED: guiControlExclusiveMode and guiControlExclusiveRec for exclusive modes
*   ADDED: GuiValueBoxFloat()
*   ADDED: GuiDropdonwBox() properties: DROPDOWN_ARROW_HIDDEN, DROPDOWN_ROLL_UP
*   ADDED: GuiListView() property: LIST_ITEMS_BORDER_WIDTH
*   ADDED: GuiLoadIconsFromMemory()
*   ADDED: Multiple new icons
*   REMOVED: GuiSpinner() from controls list, using BUTTON + VALUEBOX properties
*   REMOVED: GuiSliderPro(), functionality was redundant
*   REVIEWED: Controls using text labels to use LABEL properties
*   REVIEWED: Replaced sprintf() by snprintf() for more safety
*   REVIEWED: GuiTabBar(), close tab with mouse middle button
*   REVIEWED: GuiScrollPanel(), scroll speed proportional to content
*   REVIEWED: GuiDropdownBox(), support roll up and hidden arrow
*   REVIEWED: GuiTextBox(), cursor position initialization
*   REVIEWED: GuiSliderPro(), control value change check
*   REVIEWED: GuiGrid(), simplified implementation
*   REVIEWED: GuiIconText(), increase buffer size and reviewed padding
*   REVIEWED: GuiDrawText(), improved wrap mode drawing
*   REVIEWED: GuiScrollBar(), minor tweaks
*   REVIEWED: GuiProgressBar(), improved borders computing
*   REVIEWED: GuiTextBox(), multiple improvements: autocursor and more
*   REVIEWED: Functions descriptions, removed wrong return value reference
*   REDESIGNED: GuiColorPanel(), improved HSV <-> RGBA convertion
*
*   4.0 (12-Sep-2023) ADDED: GuiToggleSlider()
*   ADDED: GuiColorPickerHSV() and GuiColorPanelHSV()
*   ADDED: Multiple new icons, mostly compiler related
*   ADDED: New DEFAULT properties: TEXT_LINE_SPACING, TEXT_ALIGNMENT_VERTICAL, TEXT_WRAP_MODE
*   ADDED: New enum values: GuiTextAlignment, GuiTextAlignmentVertical, GuiTextWrapMode
*   ADDED: Support loading styles with custom font charset from external file
*   REDESIGNED: GuiTextBox(), support mouse cursor positioning
*   REDESIGNED: GuiDrawText(), support multiline and word-wrap modes (read only)
*   REDESIGNED: GuiProgressBar() to be more visual, progress affects border color
*   REDESIGNED: Global alpha consideration moved to GuiDrawRectangle() and GuiDrawText()
*   REDESIGNED: GuiScrollPanel(), get parameters by reference and return result value
*   REDESIGNED: GuiToggleGroup(), get parameters by reference and return result value
*   REDESIGNED: GuiComboBox(), get parameters by reference and return result value
*   REDESIGNED: GuiCheckBox(), get parameters by reference and return result value
*   REDESIGNED: GuiSlider(), get parameters by reference and return result value
*   REDESIGNED: GuiSliderBar(), get parameters by reference and return result value
*   REDESIGNED: GuiProgressBar(), get parameters by reference and return result value
*   REDESIGNED: GuiListView(), get parameters by reference and return result value
*   REDESIGNED: GuiColorPicker(), get parameters by reference and return result value
*   REDESIGNED: GuiColorPanel(), get parameters by reference and return result value
*   REDESIGNED: GuiColorBarAlpha(), get parameters by reference and return result value
*   REDESIGNED: GuiColorBarHue(), get parameters by reference and return result value
*   REDESIGNED: GuiGrid(), get parameters by reference and return result value
*   REDESIGNED: GuiGrid(), added extra parameter
*   REDESIGNED: GuiListViewEx(), change parameters order
*   REDESIGNED: All controls return result as int value
*   REVIEWED: GuiScrollPanel() to avoid smallish scroll-bars
*   REVIEWED: All examples and specially controls_test_suite
*   RENAMED: gui_file_dialog module to gui_window_file_dialog
*   UPDATED: All styles to include ISO-8859-15 charset (as much as possible)
*
*   3.6 (10-May-2023) ADDED: New icon: SAND_TIMER
*   ADDED: GuiLoadStyleFromMemory() (binary only)
*   REVIEWED: GuiScrollBar() horizontal movement key
*   REVIEWED: GuiTextBox() crash on cursor movement
*   REVIEWED: GuiTextBox(), additional inputs support
*   REVIEWED: GuiLabelButton(), avoid text cut
*   REVIEWED: GuiTextInputBox(), password input
*   REVIEWED: Local GetCodepointNext(), aligned with raylib
*   REDESIGNED: GuiSlider*()/GuiScrollBar() to support out-of-bounds
*
*   3.5 (20-Apr-2023) ADDED: GuiTabBar(), based on GuiToggle()
*   ADDED: Helper functions to split text in separate lines
*   ADDED: Multiple new icons, useful for code editing tools
*   REMOVED: Unneeded icon editing functions
*   REMOVED: GuiTextBoxMulti(), very limited and broken
*   REMOVED: MeasureTextEx() dependency, logic directly implemented
*   REMOVED: DrawTextEx() dependency, logic directly implemented
*   REVIEWED: GuiScrollBar(), improve mouse-click behaviour
*   REVIEWED: Library header info, more info, better organized
*   REDESIGNED: GuiTextBox() to support cursor movement
*   REDESIGNED: GuiDrawText() to divide drawing by lines
*
*   3.2 (22-May-2022) RENAMED: Some enum values, for unification, avoiding prefixes
*   REMOVED: GuiScrollBar(), only internal
*   REDESIGNED: GuiPanel() to support text parameter
*   REDESIGNED: GuiScrollPanel() to support text parameter
*   REDESIGNED: GuiColorPicker() to support text parameter
*   REDESIGNED: GuiColorPanel() to support text parameter
*   REDESIGNED: GuiColorBarAlpha() to support text parameter
*   REDESIGNED: GuiColorBarHue() to support text parameter
*   REDESIGNED: GuiTextInputBox() to support password
*
*   3.1 (12-Jan-2022) REVIEWED: Default style for consistency (aligned with rGuiLayout v2.5 tool)
*   REVIEWED: GuiLoadStyle() to support compressed font atlas image data and unload previous textures
*   REVIEWED: External icons usage logic
*   REVIEWED: GuiLine() for centered alignment when including text
*   RENAMED: Multiple controls properties definitions to prepend RAYGUI_
*   RENAMED: RICON_ references to RAYGUI_ICON_ for library consistency
*   Projects updated and multiple tweaks
*
*   3.0 (04-Nov-2021) Integrated ricons data to avoid external file
*   REDESIGNED: GuiTextBoxMulti()
*   REMOVED: GuiImageButton*()
*   Multiple minor tweaks and bugs corrected
*
*   2.9 (17-Mar-2021) REMOVED: Tooltip API
*   2.8 (03-May-2020) Centralized rectangles drawing to GuiDrawRectangle()
*   2.7 (20-Feb-2020) ADDED: Possible tooltips API
*   2.6 (09-Sep-2019) ADDED: GuiTextInputBox()
*   REDESIGNED: GuiListView*(), GuiDropdownBox(), GuiSlider*(), GuiProgressBar(), GuiMessageBox()
*   REVIEWED: GuiTextBox(), GuiSpinner(), GuiValueBox(), GuiLoadStyle()
*   Replaced property INNER_PADDING by TEXT_PADDING, renamed some properties
*   ADDED: 8 new custom styles ready to use
*   Multiple minor tweaks and bugs corrected
*
*   2.5 (28-May-2019) Implemented extended GuiTextBox(), GuiValueBox(), GuiSpinner()
*   2.3 (29-Apr-2019) ADDED: rIcons auxiliar library and support for it, multiple controls reviewed
*   Refactor all controls drawing mechanism to use control state
*   2.2 (05-Feb-2019) ADDED: GuiScrollBar(), GuiScrollPanel(), reviewed GuiListView(), removed Gui*Ex() controls
*   2.1 (26-Dec-2018) REDESIGNED: GuiCheckBox(), GuiComboBox(), GuiDropdownBox(), GuiToggleGroup() > Use combined text string
*   REDESIGNED: Style system (breaking change)
*   2.0 (08-Nov-2018) ADDED: Support controls guiLock and custom fonts
*   REVIEWED: GuiComboBox(), GuiListView()...
*   1.9 (09-Oct-2018) REVIEWED: GuiGrid(), GuiTextBox(), GuiTextBoxMulti(), GuiValueBox()...
*   1.8 (01-May-2018) Lot of rework and redesign to align with rGuiStyler and rGuiLayout
*   1.5 (21-Jun-2017) Working in an improved styles system
*   1.4 (15-Jun-2017) Rewritten all GUI functions (removed useless ones)
*   1.3 (12-Jun-2017) Complete redesign of style system
*   1.1 (01-Jun-2017) Complete review of the library
*   1.0 (07-Jun-2016) Converted to header-only by Ramon Santamaria.
*   0.9 (07-Mar-2016) Reviewed and tested by Albert Martos, Ian Eito, Sergio Martinez and Ramon Santamaria.
*   0.8 (27-Aug-2015) Initial release. Implemented by Kevin Gato, Daniel Nicolás and Ramon Santamaria.
*
*   DEPENDENCIES:
*   raylib 5.0 - Inputs reading (keyboard/mouse), shapes drawing, font loading and text drawing
*
*   STANDALONE MODE:
*   By default raygui depends on raylib mostly for the inputs and the drawing functionality but that dependency can be disabled
*   with the config flag RAYGUI_STANDALONE. In that case is up to the user to provide another backend to cover library needs.
*
*   The following functions should be redefined for a custom backend:
*
*   - Vector2 GetMousePosition(void);
*   - float GetMouseWheelMove(void);
*   - bool IsMouseButtonDown(int button);
*   - bool IsMouseButtonPressed(int button);
*   - bool IsMouseButtonReleased(int button);
*   - bool IsKeyDown(int key);
*   - bool IsKeyPressed(int key);
*   - int GetCharPressed(void); // -- GuiTextBox(), GuiValueBox()
*
*   - void DrawRectangle(int x, int y, int width, int height, Color color); // -- GuiDrawRectangle()
*   - void DrawRectangleGradientEx(Rectangle rec, Color col1, Color col2, Color col3, Color col4); // -- GuiColorPicker()
*
*   - Font GetFontDefault(void); // -- GuiLoadStyleDefault()
*   - Font LoadFontEx(const char *fileName, int fontSize, int *codepoints, int codepointCount); // -- GuiLoadStyle()
*   - Texture2D LoadTextureFromImage(Image image); // -- GuiLoadStyle(), required to load texture from embedded font atlas image
*   - void SetShapesTexture(Texture2D tex, Rectangle rec); // -- GuiLoadStyle(), required to set shapes rec to font white rec (optimization)
*   - char *LoadFileText(const char *fileName); // -- GuiLoadStyle(), required to load charset data
*   - void UnloadFileText(char *text); // -- GuiLoadStyle(), required to unload charset data
*   - const char *GetDirectoryPath(const char *filePath); // -- GuiLoadStyle(), required to find charset/font file from text .rgs
*   - int *LoadCodepoints(const char *text, int *count); // -- GuiLoadStyle(), required to load required font codepoints list
*   - void UnloadCodepoints(int *codepoints); // -- GuiLoadStyle(), required to unload codepoints list
*   - unsigned char *DecompressData(const unsigned char *compData, int compDataSize, int *dataSize); // -- GuiLoadStyle()
*
*   CONTRIBUTORS:
*   Ramon Santamaria: Supervision, review, redesign, update and maintenance
*   Vlad Adrian: Complete rewrite of GuiTextBox() to support extended features (2019)
*   Sergio Martinez: Review, testing (2015) and redesign of multiple controls (2018)
*   Adria Arranz: Testing and implementation of additional controls (2018)
*   Jordi Jorba: Testing and implementation of additional controls (2018)
*   Albert Martos: Review and testing of the library (2015)
*   Ian Eito: Review and testing of the library (2015)
*   Kevin Gato: Initial implementation of basic components (2014)
*   Daniel Nicolas: Initial implementation of basic components (2014)
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2014-2025 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you
*   wrote the original software. If you use this software in a product, an acknowledgment
*   in the product documentation would be appreciated but is not required.
*
*   2. Altered source versions must be plainly marked as such, and must not be misrepresented
*   as being the original software.
*
*   3. This notice may not be removed or altered from any source distribution.
*
********************************************************************************************/

#ifndef RAYGUI_H
#define RAYGUI_H

#define RAYGUI_VERSION_MAJOR 4
#define RAYGUI_VERSION_MINOR 5
#define RAYGUI_VERSION_PATCH 0
#define RAYGUI_VERSION "5.0-dev"

#if !defined(RAYGUI_STANDALONE)
    #include "raylib.h"
#endif

// Function specifiers in case library is build/used as a shared library (Windows)
// NOTE: Microsoft specifiers to tell compiler that symbols are imported/exported from a .dll
#if defined(_WIN32)
    #if defined(BUILD_LIBTYPE_SHARED)
        #define RAYGUIAPI __declspec(dllexport)     // We are building the library as a Win32 shared library (.dll)
    #elif defined(USE_LIBTYPE_SHARED)
        #define RAYGUIAPI __declspec(dllimport)     // We are using the library as a Win32 shared library (.dll)
    #endif
#endif

// Function specifiers definition
#ifndef RAYGUIAPI
    #define RAYGUIAPI       // Functions defined as 'extern' by default (implicit specifiers)
#endif

//-----------------------------------------------------------------------------------
// Defines and Macros
//-----------------------------------------------------------------------------------
// Simple log system to avoid printf() calls if required
// NOTE: Avoiding those calls, also avoids const strings memory usage
#define RAYGUI_SUPPORT_LOG_INFO
#if defined(RAYGUI_SUPPORT_LOG_INFO)
    #define RAYGUI_LOG(...) printf(__VA_ARGS__)
#else
    #define RAYGUI_LOG(...)
#endif

//-----------------------------------------------------------------------------------
// Types and Structures Definition
// NOTE: Some types are required for RAYGUI_STANDALONE usage
//-----------------------------------------------------------------------------------
#if defined(RAYGUI_STANDALONE)
    #ifndef __cplusplus
    // Boolean type
    #ifndef true
        typedef enum { false, true } bool;
    #endif
    #endif

    // Vector2 type
    typedef struct Vector2 {
        float x;
        float y;
    } Vector2;

    // Vector3 type
    // -- ConvertHSVtoRGB(), ConvertRGBtoHSV()
    typedef struct Vector3 {
        float x;
        float y;
        float z;
    } Vector3;

    // Color type, RGBA (32bit)
    typedef struct Color {
        unsigned char r;
        unsigned char g;
        unsigned char b;
        unsigned char a;
    } Color;

    // Rectangle type
    typedef struct Rectangle {
        float x;
        float y;
        float width;
        float height;
    } Rectangle;

    // TODO: Texture2D type is very coupled to raylib, required by Font type
    // It should be redesigned to be provided by user
    typedef struct Texture2D {
        unsigned int id;        // OpenGL texture id
        int width;              // Texture base width
        int height;             // Texture base height
        int mipmaps;            // Mipmap levels, 1 by default
        int format;             // Data format (PixelFormat type)
    } Texture2D;

    // Image, pixel data stored in CPU memory (RAM)
    typedef struct Image {
        void *data;             // Image raw data
        int width;              // Image base width
        int height;             // Image base height
        int mipmaps;            // Mipmap levels, 1 by default
        int format;             // Data format (PixelFormat type)
    } Image;

    // GlyphInfo, font characters glyphs info
    typedef struct GlyphInfo {
        int value;              // Character value (Unicode)
        int offsetX;            // Character offset X when drawing
        int offsetY;            // Character offset Y when drawing
        int advanceX;           // Character advance position X
        Image image;            // Character image data
    } GlyphInfo;

    // TODO: Font type is very coupled to raylib, mostly required by GuiLoadStyle()
    // It should be redesigned to be provided by user
    typedef struct Font {
        int baseSize;           // Base size (default chars height)
        int glyphCount;         // Number of glyph characters
        int glyphPadding;       // Padding around the glyph characters
        Texture2D texture;      // Texture atlas containing the glyphs
        Rectangle *recs;        // Rectangles in texture for the glyphs
        GlyphInfo *glyphs;      // Glyphs info data
    } Font;
#endif

// Style property
// NOTE: Used when exporting style as code for convenience
typedef struct GuiStyleProp {
    unsigned short controlId;       // Control identifier
    unsigned short propertyId;     // Property identifier
    int propertyValue;              // Property value
} GuiStyleProp;

// Gui control state
typedef enum {
    STATE_NORMAL = 0,
    STATE_FOCUSED,
    STATE_PRESSED,
    STATE_DISABLED
} GuiState;

// Gui control text alignment
typedef enum {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT
} GuiTextAlignment;

// Gui control text alignment vertical
// NOTE: Text vertical position inside the text bounds
typedef enum {
    TEXT_ALIGN_TOP = 0,
    TEXT_ALIGN_MIDDLE,
    TEXT_ALIGN_BOTTOM
} GuiTextAlignmentVertical;

// Gui control text wrap mode
// NOTE: Useful for multiline text
typedef enum {
    TEXT_WRAP_NONE = 0,
    TEXT_WRAP_CHAR,
    TEXT_WRAP_WORD
} GuiTextWrapMode;

// Gui controls
typedef enum {
    // Default -> populates to all controls when set
    DEFAULT = 0,
    // Basic controls
    LABEL,          // Used also for: LABELBUTTON
    BUTTON,
    TOGGLE,         // Used also for: TOGGLEGROUP
    SLIDER,         // Used also for: SLIDERBAR, TOGGLESLIDER
    PROGRESSBAR,
    CHECKBOX,
    COMBOBOX,
    DROPDOWNBOX,
    TEXTBOX,        // Used also for: TEXTBOXMULTI
    VALUEBOX,
    CONTROL11,
    LISTVIEW,
    COLORPICKER,
    SCROLLBAR,
    STATUSBAR
} GuiControl;

//-----------------------------------------------------------------------------------
// Module Functions Declaration
//-----------------------------------------------------------------------------------
#if defined(__cplusplus)
extern "C" {            // Prevents name mangling of functions
#endif

// Global gui state control functions
RAYGUIAPI void GuiEnable(void);                                         // Enable gui controls (global state)
RAYGUIAPI void GuiDisable(void);                                        // Disable gui controls (global state)
RAYGUIAPI void GuiLock(void);                                           // Lock gui controls (global state)
RAYGUIAPI void GuiUnlock(void);                                         // Unlock gui controls (global state)
RAYGUIAPI bool GuiIsLocked(void);                                       // Check if gui is locked (global state)
RAYGUIAPI void GuiSetAlpha(float alpha);                                // Set gui controls alpha (global state), alpha goes from 0.0f to 1.0f
RAYGUIAPI void GuiSetState(int state);                                  // Set gui state (global state)
RAYGUIAPI int GuiGetState(void);                                        // Get gui state (global state)

// Font set/get functions
RAYGUIAPI void GuiSetFont(Font font);                                   // Set gui custom font (global state)
RAYGUIAPI Font GuiGetFont(void);                                        // Get gui custom font (global state)

// Style set/get functions
RAYGUIAPI void GuiSetStyle(int control, int property, int value);       // Set one style property
RAYGUIAPI int GuiGetStyle(int control, int property);                   // Get one style property

// Styles loading functions
RAYGUIAPI void GuiLoadStyle(const char *fileName);                      // Load style file over global style variable (.rgs)
RAYGUIAPI void GuiLoadStyleDefault(void);                               // Load style default over global style

// Tooltips management functions
RAYGUIAPI void GuiEnableTooltip(void);                                  // Enable gui tooltips (global state)
RAYGUIAPI void GuiDisableTooltip(void);                                 // Disable gui tooltips (global state)
RAYGUIAPI void GuiSetTooltip(const char *tooltip);                      // Set tooltip string

// Icons functionality
RAYGUIAPI const char *GuiIconText(int iconId, const char *text);        // Get text with icon id prepended (if supported)

// Controls
//----------------------------------------------------------------------------------------------------------
// Container/separator controls, useful for controls organization
RAYGUIAPI int GuiWindowBox(Rectangle bounds, const char *title);                                       // Window Box control, shows a window that can be closed
RAYGUIAPI int GuiGroupBox(Rectangle bounds, const char *text);                                         // Group Box control with text name
RAYGUIAPI int GuiLine(Rectangle bounds, const char *text);                                             // Line separator control, could contain text
RAYGUIAPI int GuiPanel(Rectangle bounds, const char *text);                                            // Panel control, useful to group controls
RAYGUIAPI int GuiTabBar(Rectangle bounds, const char **text, int count, int *active);                 // Tab Bar control, returns TAB to be closed or -1
RAYGUIAPI int GuiScrollPanel(Rectangle bounds, const char *text, Rectangle content, Vector2 *scroll, Rectangle *view);   // Scroll Panel control

// Basic controls set
RAYGUIAPI int GuiLabel(Rectangle bounds, const char *text);                                            // Label control
RAYGUIAPI int GuiButton(Rectangle bounds, const char *text);                                           // Button control, returns true when clicked
RAYGUIAPI int GuiLabelButton(Rectangle bounds, const char *text);                                      // Label button control, returns true when clicked
RAYGUIAPI int GuiToggle(Rectangle bounds, const char *text, bool *active);                            // Toggle Button control
RAYGUIAPI int GuiToggleGroup(Rectangle bounds, const char *text, int *active);                        // Toggle Group control
RAYGUIAPI int GuiToggleSlider(Rectangle bounds, const char *text, int *active);                       // Toggle Slider control
RAYGUIAPI int GuiCheckBox(Rectangle bounds, const char *text, bool *checked);                         // Check Box control, returns true when active
RAYGUIAPI int GuiComboBox(Rectangle bounds, const char *text, int *active);                           // Combo Box control
RAYGUIAPI int GuiDropdownBox(Rectangle bounds, const char *text, int *active, bool editMode);         // Dropdown Box control

// Advance controls set
RAYGUIAPI int GuiTextBox(Rectangle bounds, char *text, int textSize, bool editMode);                  // Text Box control, updates input text
RAYGUIAPI int GuiValueBox(Rectangle bounds, const char *text, int *value, int minValue, int maxValue, bool editMode);     // Value Box control, updates input value
RAYGUIAPI int GuiTextInputBox(Rectangle bounds, const char *title, const char *message, const char *buttons, char *text, int textMaxSize, bool *secretViewActive);   // Text Input Box control, ask for text, supports secret

#if defined(__cplusplus)
}
#endif

#endif // RAYGUI_H

#if defined(RAYGUI_IMPLEMENTATION)

// We need to define this to use the implementation
#define RAYGUI_IMPLEMENTATION

// Include the full implementation here...
// For now, we'll just include the essential parts needed for basic functionality

#endif // RAYGUI_IMPLEMENTATION
