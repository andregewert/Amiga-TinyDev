/**
 * @file appprefs.h
 * @brief Portable reader and writer of TinyDev's global settings
 *        (the "settings" of PROGDIR:TinyDev.json).
 */
#ifndef TINYDEV_APPPREFS_H
#define TINYDEV_APPPREFS_H

/*
 * TinyDev keeps all its global settings in PROGDIR:TinyDev.json, next to
 * the settings per file type (filetypes.h), not in the tool types of its
 * icon: an update may replace the icon, and the file holds every value,
 * defaults included, so it shows what can be set. No Amiga headers, so
 * the host tests can use it. The project settings are elsewhere
 * (projconf.h).
 *
 *   "settings": {
 *     "splash": true,
 *     "scout": true,
 *     "toolbar": { "show": "images", "frames": false },
 *     "window": { "left": -1, "top": -1, "width": 0, "height": 0 },
 *     "view": { "foldertree": true, "minimap": false, "showinfo": false },
 *     "toolwindows": {
 *       "bookmarks": { "open": false, "left": -1, "top": -1, "width": 0, "height": 0 },
 *       "output": { ... }, "symbols": { ... }, "problems": { ... }
 *     },
 *     "tempdir": "T:",
 *     "shellwindow": "CON:",
 *     "outputfont": { "name": "", "size": 0 },
 *     "projectdir": "",
 *     "author": "",
 *     "authormail": "",
 *     "colours": { "text": "#000000", "keyword": "#0030D0", ... }
 *   }
 *
 * - "splash": the splash window while starting;
 * - "scout": analyse the sources with TinyDev-Scout;
 * - "toolbar": what its buttons show ("images", "both" or "text", at the
 *   next start) and frames around them;
 * - "window": the main window at the start; width and height 0 are
 *   automatic, left and top -1 centre it;
 * - "view": the view at the start (the View menu changes it for the
 *   session only) and whether the folder tree shows .info files;
 * - "toolwindows": the tool windows as View/Snapshot Tool Windows left
 *   them: open at the start, and their box (width 0: placed beside or
 *   below the main window);
 * - "tempdir": the drawer of the temporary files;
 * - "shellwindow": the console of Project/New Shell, a handler (CON:) or
 *   a whole window;
 * - "outputfont": the font of the output window's lines, a name with
 *   ".font" and a size; "" (or size 0) is the system default text font,
 *   the one Font preferences set for "System Default Text";
 * - "projectdir": the drawer suggested for new projects ("" = the parent
 *   of the current project);
 * - "author", "authormail": the author filled into templates;
 * - "colours": the colours of the highlighting, "#RRGGBB", with the keys
 *   text, keyword, string, comment, preprocessor, type, function, macro,
 *   constant and variable.
 * Missing keys and wrong values keep the defaults. Other keys, in the file
 * and in "settings", are kept by appprefs_format().
 */

/** What the toolbar buttons show. */
enum ToolbarMode {
    TBMODE_IMAGES = 0,  /**< images (the default) */
    TBMODE_BOTH,        /**< images with a text below */
    TBMODE_TEXT,        /**< text only, no images are loaded */
    TBMODE_COUNT        /**< number of modes */
};

/** Configurable colours of the editor (syntax highlighting), in this order:
 *  the order of SyntaxStyle (syntax.h). */
enum SettingsColour {
    SCOL_TEXT = 0,      /**< normal text */
    SCOL_KEYWORD,       /**< keywords */
    SCOL_STRING,        /**< strings */
    SCOL_COMMENT,       /**< comments */
    SCOL_PREPROCESSOR,  /**< preprocessor lines */
    SCOL_TYPE,          /**< types */
    SCOL_FUNCTION,      /**< functions */
    SCOL_MACRO,         /**< macros */
    SCOL_CONSTANT,      /**< constants and numbers */
    SCOL_VARIABLE,      /**< parameters and script variables */
    SCOL_COUNT          /**< number of colours */
};

/** TinyDev's global settings, edited in the settings window. */
typedef struct Settings {
    int tbmode;         /**< ToolbarMode, takes effect at the next start */
    int tbframes;       /**< frames around the toolbar buttons */
    int splash;         /**< splash window while starting */
    int foldertree;     /**< folder tree shown at the start */
    int minimap;        /**< minimap shown at the start */
    int showinfo;       /**< the folder tree shows .info files */
    long winwidth;      /**< width of the main window at the start: 0 = automatic */
    long winheight;     /**< its height: 0 = automatic */
    long winleft;       /**< its left edge: -1 = centred */
    long wintop;        /**< its top edge: -1 = centred */
    unsigned long colours[SCOL_COUNT];  /**< 0xRRGGBB, SettingsColour order */
    long bmwin[4];      /**< bookmark list as snapshot: left, top, width, height; width 0 = automatic */
    int bmlist;         /**< bookmark list open at the start */
    long outwin[4];     /**< output window as snapshot; width 0 = automatic */
    int outlist;        /**< output window open at the start */
    long symwin[4];     /**< symbol list as snapshot; width 0 = automatic */
    int symlist;        /**< symbol list open at the start */
    long diagwin[4];    /**< problem list as snapshot; width 0 = automatic */
    int diaglist;       /**< problem list open at the start */
    int scout;          /**< analyse the sources with TinyDev-Scout */
    char tempdir[256];  /**< drawer of the temporary files (default T:) */
    char shellwindow[256]; /**< console of Project/New Shell: a handler (CON:) or a whole window */
    char outfont[64];      /**< font of the output window ("name.font"); "" = the system default text font */
    long outfontsize;      /**< its size; 0 = the system default text font */
    char projectdir[256];  /**< drawer suggested for new projects; "" = the current project's parent */
    char author[128];      /**< the author for templates, Sprout's AUTHOR */
    char authormail[128];  /**< the author's e-mail address, Sprout's AUTHORMAIL */
    char icon_name[300];   /**< program icon without .info, for splash, About and AppIcon (not saved) */
} Settings;

/** Default colours, SettingsColour order. */
extern const unsigned long settings_default_colours[SCOL_COUNT];

/**
 * @brief Set the default settings.
 * @param s The settings to fill in.
 */
void settings_default(Settings *s);

/**
 * @brief Read the "settings" of TinyDev.json over settings.
 *
 * A file without "settings" leaves them as they are; missing keys and
 * wrong values keep theirs. icon_name is never touched.
 *
 * @param s The settings, usually preset with settings_default().
 * @param json The file's text.
 * @return 1 on success, 0 if the text is no JSON object.
 */
int appprefs_parse(Settings *s, const char *json);

/**
 * @brief Write the settings into the text of TinyDev.json.
 *
 * Every setting is written, defaults included. The rest of the old text
 * (the file types, unknown keys) is kept; without an old text a new file
 * is made.
 *
 * @param s The settings.
 * @param old_json The old file's text, or NULL/"" for a new file.
 * @return The new text (free() it), or NULL if out of memory or the old
 *         text is no JSON object.
 */
char *appprefs_format(const Settings *s, const char *old_json);

#endif
