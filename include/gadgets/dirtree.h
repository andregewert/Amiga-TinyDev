/**
 * @file dirtree.h
 * @brief Public interface of dirtree.gadget, a lazily loaded directory
 *        tree (ReAction class for AmigaOS 3.2).
 *
 * dirtree.gadget shows a directory as a hierarchical list. The name of the
 * directory is the column title above the list, its contents the first
 * level. Directories are read lazily: only the children of an expanded
 * directory are read, collapsing a directory frees them again. Depth and
 * the number of entries are bounded. Entries get small built-in symbols
 * (drawer, open drawer, file, source code, script, picture), drawn with the
 * name by the class's render hook; icon files (*.info) are left out. The
 * shown directories are watched, so the tree follows changes on disk.
 * A pattern can choose the files that are shown, a hook the names they
 * show (e.g. the titles of HTML files), and the entries can be sorted in
 * natural order with directories among the files.
 *
 * - Public class name: "dirtree.gadget" (superclass: listbrowser.gadget)
 * - Library: SYS:Classes/Gadgets/dirtree.gadget
 *
 * All listbrowser.gadget attributes except LISTBROWSER_Labels,
 * LISTBROWSER_Hierarchical, LISTBROWSER_ColumnInfo and
 * LISTBROWSER_ColumnTitles may be used; the class owns its node list and
 * its column. With the symbols (#DIRTREE_Images) there are no expand
 * arrows: drawers open and close with a double-click. Without symbols and
 * without LISTBROWSER_ShowImage/HideImage the class uses glyph.image
 * arrows (when images/glyph.image is available). LISTBROWSER_ShowSelected
 * defaults to TRUE, so a clicked entry stays highlighted; the selection is
 * kept when directories are expanded or collapsed (unless the selected
 * entry disappears with a collapsed directory).
 *
 * @par Usage
 * Expanding, collapsing and activating happen while Intuition handles the
 * input, where DOS must not be used. The gadget therefore only reports
 * them with IDCMP_GADGETUP (WMHI_GADGETUP); the application then lets the
 * gadget do the work from its own process. SetGadgetAttrs() passes the
 * GadgetInfo the gadget needs to redraw itself:
 * @code
 * ULONG ev = DIRTREE_EV_NONE;
 * SetGadgetAttrs((struct Gadget *)tree, window, NULL,
 *                DIRTREE_HandleEvent, TRUE, TAG_END);
 * GetAttr(DIRTREE_Event, tree, &ev);
 * if (ev == DIRTREE_EV_ACTIVATE) {
 *     STRPTR path = NULL;
 *     GetAttr(DIRTREE_ActivePath, tree, (ULONG *)&path);
 *     ... open path ...
 * }
 * @endcode
 * #DIRTREE_Root, #DIRTREE_HandleEvent and #DIRTREE_Refresh must be set from
 * a process (SetGadgetAttrs()), not through a notification (OM_UPDATE).
 *
 * The shown directories are watched (#DIRTREE_Watch): wait for
 * #DIRTREE_SignalMask as well and update the tree when it arrives:
 * @code
 * SetGadgetAttrs((struct Gadget *)tree, window, NULL,
 *                DIRTREE_Refresh, DIRTREE_RF_CHANGED, TAG_END);
 * @endcode
 */
/*
**  $VER: dirtree.h 1.0 (18.10.2026)
**  Copyright (c) 2026 André Gewert <agewert@ubergeek.de>, MIT License
*/
#ifndef GADGETS_DIRTREE_H
#define GADGETS_DIRTREE_H

#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif
#ifndef INTUITION_CGHOOKS_H
#include <intuition/cghooks.h>
#endif

#define DIRTREE_CLASSNAME   "dirtree.gadget"    /**< public name of the class */
#define DIRTREE_VERSION     1                   /**< library version this header describes */

#define DIRTREE_Dummy       (TAG_USER + 0x04A80000) /**< base of the class's tags */

/** @name Attributes */
/** @{ */

/**
 * @brief Directory shown as the root of the tree.
 *
 * The path is resolved to its full name and copied. Setting it reads the
 * directory's first level; NULL empties the tree. If the directory cannot
 * be read, the old tree stays and #DIRTREE_Status is #DIRTREE_ST_FAILED.
 *
 * Type: STRPTR. Applicability: I S G. Default: NULL.
 */
#define DIRTREE_Root        (DIRTREE_Dummy + 1)

/**
 * @brief Deepest level that is read below the root (first level = 1).
 *
 * Type: ULONG. Applicability: I G. Default: 16.
 */
#define DIRTREE_MaxDepth    (DIRTREE_Dummy + 2)

/**
 * @brief Maximum number of list entries, including placeholders.
 *
 * A directory that would exceed it is shown incompletely.
 *
 * Type: ULONG. Applicability: I G. Default: 2048.
 */
#define DIRTREE_MaxEntries  (DIRTREE_Dummy + 3)

/**
 * @brief Result of the last #DIRTREE_Root, #DIRTREE_HandleEvent or
 *        #DIRTREE_Refresh.
 *
 * Type: ULONG, a DIRTREE_ST_ value. Applicability: G.
 */
#define DIRTREE_Status      (DIRTREE_Dummy + 4)

/**
 * @brief Full path of the file activated (double-clicked) at the last
 *        #DIRTREE_HandleEvent, NULL otherwise.
 *
 * Valid until the next #DIRTREE_HandleEvent, #DIRTREE_Root change or
 * disposal.
 *
 * Type: STRPTR. Applicability: G.
 */
#define DIRTREE_ActivePath  (DIRTREE_Dummy + 5)

/**
 * @brief Full path of the selected entry, NULL if none or a placeholder.
 *
 * Valid until the tree changes.
 *
 * Type: STRPTR. Applicability: G.
 */
#define DIRTREE_SelectedPath (DIRTREE_Dummy + 6)

/**
 * @brief Text of the placeholder of a directory that is not read yet.
 *
 * For localisation; not copied, it must stay valid while the object
 * exists.
 *
 * Type: STRPTR. Applicability: I. Default: "Loading...".
 */
#define DIRTREE_LoadingText     (DIRTREE_Dummy + 7)

/**
 * @brief Text of the placeholder of an empty directory.
 *
 * Not copied, it must stay valid while the object exists.
 *
 * Type: STRPTR. Applicability: I. Default: "(empty)".
 */
#define DIRTREE_EmptyText       (DIRTREE_Dummy + 8)

/**
 * @brief Text of the placeholder of a directory that cannot be read.
 *
 * Not copied, it must stay valid while the object exists.
 *
 * Type: STRPTR. Applicability: I. Default: "(unavailable)".
 */
#define DIRTREE_UnavailableText (DIRTREE_Dummy + 9)

/**
 * @brief Do what the listbrowser reported with its last GADGETUP.
 *
 * Set it to TRUE after each GADGETUP (with SetGadgetAttrs(), from the
 * application's process): it expands or collapses the directory or
 * activates the file. The result is #DIRTREE_Event.
 *
 * Type: BOOL. Applicability: S.
 */
#define DIRTREE_HandleEvent     (DIRTREE_Dummy + 10)

/**
 * @brief Result of the last #DIRTREE_HandleEvent.
 *
 * Type: ULONG, a DIRTREE_EV_ value. Applicability: G.
 */
#define DIRTREE_Event           (DIRTREE_Dummy + 11)

/**
 * @brief Leave out icon files (*.info).
 *
 * Type: BOOL. Applicability: I G. Default: TRUE.
 */
#define DIRTREE_HideIcons   (DIRTREE_Dummy + 12)

/**
 * @brief Draw small built-in symbols in front of the entries.
 *
 * Type: BOOL. Applicability: I G. Default: TRUE.
 */
#define DIRTREE_Images      (DIRTREE_Dummy + 13)

/**
 * @brief Watch the shown directories (the root and every expanded one)
 *        with DOS notification (StartNotify()).
 *
 * The gadget then has a message port whose signal belongs to the task that
 * created the object: only that task may wait for #DIRTREE_SignalMask and
 * set #DIRTREE_Refresh. File systems without notification are not watched;
 * #DIRTREE_Refresh with #DIRTREE_RF_ALL reads the tree again.
 *
 * Type: BOOL. Applicability: I G. Default: TRUE.
 */
#define DIRTREE_Watch           (DIRTREE_Dummy + 14)

/**
 * @brief Signal mask of the notifications, 0 without watching.
 *
 * Wait for it together with the window's signals; when it arrives, set
 * #DIRTREE_Refresh to #DIRTREE_RF_CHANGED.
 *
 * Type: ULONG. Applicability: G.
 */
#define DIRTREE_SignalMask      (DIRTREE_Dummy + 15)

/**
 * @brief Read directories again and update the tree.
 *
 * Set it with SetGadgetAttrs() from the application's process: new entries
 * are added in order, removed ones disappear with their subtrees; expanded
 * directories, the selection and the scroll position stay. #DIRTREE_Status
 * tells the result.
 *
 * Type: ULONG, a DIRTREE_RF_ value. Applicability: S.
 */
#define DIRTREE_Refresh         (DIRTREE_Dummy + 16)

/**
 * @brief AmigaDOS pattern of the files that are shown, e.g.
 *        "#?.(htm|html)".
 *
 * Directories are not matched against it. The pattern is parsed when the
 * object is created; an invalid pattern shows all files.
 *
 * Type: STRPTR. Applicability: I. Default: NULL (all files).
 */
#define DIRTREE_Pattern         (DIRTREE_Dummy + 17)

/**
 * @brief Hook that gives the name an entry shows.
 *
 * Called with the object and a struct DirTreeNameMsg for every file and
 * directory when it is read (on the application's process, from OM_NEW or
 * OM_SET: DOS may be used, e.g. to read a file's title). It copies the
 * name into dtnm_Buffer and returns TRUE, or returns FALSE to show the
 * file name. The order of the entries still follows the file names.
 *
 * Type: struct Hook *. Applicability: I. Default: NULL (file names).
 */
#define DIRTREE_NameHook        (DIRTREE_Dummy + 18)

/**
 * @brief Order of the entries of a directory.
 *
 * Type: ULONG, a DIRTREE_SORT_ value. Applicability: I G.
 * Default: #DIRTREE_SORT_DIRSFIRST.
 */
#define DIRTREE_Sort            (DIRTREE_Dummy + 19)

/**
 * @brief Leave out directories that hold no shown file.
 *
 * A directory is shown only if it (or one of its subdirectories, as deep
 * as #DIRTREE_MaxDepth allows) holds a file that is shown (see
 * #DIRTREE_Pattern and #DIRTREE_HideIcons). This reads the subdirectories
 * when their parent is read, up to the first such file.
 *
 * Type: BOOL. Applicability: I G. Default: FALSE.
 */
#define DIRTREE_HideEmpty       (DIRTREE_Dummy + 20)

/**
 * @brief AmigaDOS pattern of files that are not listed although they
 *        match #DIRTREE_Pattern, e.g. "index.(htm|html)".
 *
 * They still count as contents for #DIRTREE_HideEmpty: a directory that
 * holds only such a file is shown (it may stand for that file).
 *
 * Type: STRPTR. Applicability: I. Default: NULL (none).
 */
#define DIRTREE_ExcludePattern  (DIRTREE_Dummy + 21)

/**
 * @brief Text of the column title above the list.
 *
 * Copied. Setting #DIRTREE_Root shows the root's name again; NULL or ""
 * does that as well. In one tag list #DIRTREE_Title is applied after
 * #DIRTREE_Root.
 *
 * Type: STRPTR. Applicability: I S G. Default: the root's name.
 */
#define DIRTREE_Title           (DIRTREE_Dummy + 22)

/** @} */

/** @name Values of DIRTREE_Status */
/** @{ */
#define DIRTREE_ST_OK           0   /**< read completely */
#define DIRTREE_ST_INCOMPLETE   1   /**< partly read (error, limits, memory) */
#define DIRTREE_ST_FAILED       2   /**< not read at all */
/** @} */

/** @name Values of DIRTREE_Event */
/** @{ */
#define DIRTREE_EV_NONE         0   /**< nothing to do (selection, cursor) */
#define DIRTREE_EV_EXPANDED     1   /**< a directory was read and opened */
#define DIRTREE_EV_COLLAPSED    2   /**< a directory was closed */
#define DIRTREE_EV_ACTIVATE     3   /**< a file was double-clicked, see #DIRTREE_ActivePath */
/** @} */

/** @name Values of DIRTREE_Sort */
/** @{ */
#define DIRTREE_SORT_DIRSFIRST  0   /**< directories first, then by name ignoring case */
#define DIRTREE_SORT_NATURAL    1   /**< natural order of the file names ("2" before "10"),
                                         directories among the files */
/** @} */

/** @name Values of DIRTREE_Refresh */
/** @{ */
#define DIRTREE_RF_CHANGED      1   /**< the directories DOS reported changed */
#define DIRTREE_RF_ALL          2   /**< the root and every expanded directory */
/** @} */

/** Message of #DIRTREE_NameHook: the entry whose shown name is asked for. */
struct DirTreeNameMsg {
    STRPTR dtnm_Path;       /**< full path of the entry */
    STRPTR dtnm_Name;       /**< its file name */
    LONG   dtnm_Directory;  /**< non-zero for a directory */
    STRPTR dtnm_Buffer;     /**< buffer for the shown name (ISO-8859-1) */
    ULONG  dtnm_BufferSize; /**< its size, including the terminating NUL */
};

#endif /* GADGETS_DIRTREE_H */
