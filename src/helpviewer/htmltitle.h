/**
 * @file htmltitle.h
 * @brief Title of an HTML file and URL helpers of the HelpViewer
 *        (portable, no Amiga headers).
 */
#ifndef HELPVIEWER_HTMLTITLE_H
#define HELPVIEWER_HTMLTITLE_H

/**
 * @brief Find the title of an HTML text.
 *
 * Takes the text between \<title\> and \</title\> (ignoring case), joins
 * white space to single spaces, decodes character references (named ones
 * of ISO-8859-1, &\#NNN; and &\#xHH;) and turns UTF-8 sequences of the
 * Latin-1 range into ISO-8859-1. Only the given part of the file is
 * searched (the title is near its start).
 *
 * @param text The text (need not be terminated).
 * @param size Its length.
 * @param[out] out Buffer for the title (ISO-8859-1).
 * @param out_size Its size; the title is cut to fit.
 * @return Non-zero if a title that is not empty was found.
 */
int html_title(const char *text, long size, char *out, long out_size);

/**
 * @brief Turn the path of a link (relative, URL style) into an AmigaDOS
 *        path relative to the document's directory.
 *
 * "file:" is removed, %XX escapes are decoded, "./" is dropped and "../"
 * becomes "/" (the parent directory in AmigaDOS). An "#anchor" stays at
 * the end.
 *
 * @param url The link.
 * @param[out] out Buffer for the path.
 * @param out_size Its size.
 * @return Non-zero on success, zero if it does not fit.
 */
int html_link_to_path(const char *url, char *out, long out_size);

/**
 * @brief Tell whether a link leads outside (has a scheme such as http:,
 *        ftp: or mailto:, other than file:).
 * @param url The link.
 * @return Non-zero for an external link.
 */
int html_link_is_external(const char *url);

#endif
