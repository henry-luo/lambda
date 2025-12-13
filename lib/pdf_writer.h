/**
 * Lambda PDF Writer Library
 * 
 * A lightweight PDF generation library compatible with libharu's C API.
 * Implements the subset of functions used by radiant for HTML-to-PDF rendering.
 * 
 * Copyright (c) 2024 Lambda Script Project
 * License: Same as Lambda Script project
 */

#ifndef PDF_WRITER_H
#define PDF_WRITER_H

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/*  Types                                                                    */
/*---------------------------------------------------------------------------*/

typedef unsigned long HPDF_STATUS;

// Opaque handle types (pointers to internal structures)
typedef struct HPDF_Doc_Rec*  HPDF_Doc;
typedef struct HPDF_Page_Rec* HPDF_Page;
typedef struct HPDF_Font_Rec* HPDF_Font;

// Error handler callback type
typedef void (*HPDF_ErrorHandler)(HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data);

/*---------------------------------------------------------------------------*/
/*  Constants                                                                */
/*---------------------------------------------------------------------------*/

// Status codes
#define HPDF_OK                     0
#define HPDF_ERROR                  0x1000
#define HPDF_ERROR_INVALID_PARAM    0x1001
#define HPDF_ERROR_OUT_OF_MEMORY    0x1002
#define HPDF_ERROR_FILE_IO          0x1003
#define HPDF_ERROR_INVALID_STATE    0x1004
#define HPDF_ERROR_FONT_NOT_FOUND   0x1005

// Compression modes
#define HPDF_COMP_NONE              0x00
#define HPDF_COMP_TEXT              0x01
#define HPDF_COMP_IMAGE             0x02
#define HPDF_COMP_METADATA          0x04
#define HPDF_COMP_ALL               0x0F

// Info attribute types
typedef enum {
    HPDF_INFO_CREATOR = 0,
    HPDF_INFO_PRODUCER,
    HPDF_INFO_TITLE,
    HPDF_INFO_AUTHOR,
    HPDF_INFO_SUBJECT,
    HPDF_INFO_KEYWORDS,
    HPDF_INFO_CREATION_DATE,
    HPDF_INFO_MOD_DATE
} HPDF_InfoType;

// Page sizes (in points, 1 point = 1/72 inch)
#define HPDF_PAGE_SIZE_A4_WIDTH     595.276f
#define HPDF_PAGE_SIZE_A4_HEIGHT    841.89f
#define HPDF_PAGE_SIZE_LETTER_WIDTH 612.0f
#define HPDF_PAGE_SIZE_LETTER_HEIGHT 792.0f

/*---------------------------------------------------------------------------*/
/*  Document Functions                                                       */
/*---------------------------------------------------------------------------*/

/**
 * Create a new PDF document.
 * 
 * @param error_fn    Error callback function (can be NULL)
 * @param user_data   User data passed to error callback
 * @return            Document handle, or NULL on failure
 */
HPDF_Doc HPDF_New(HPDF_ErrorHandler error_fn, void* user_data);

/**
 * Free a PDF document and all associated resources.
 * 
 * @param doc   Document handle
 */
void HPDF_Free(HPDF_Doc doc);

/**
 * Save the PDF document to a file.
 * 
 * @param doc       Document handle
 * @param filename  Output file path
 * @return          HPDF_OK on success, error code on failure
 */
HPDF_STATUS HPDF_SaveToFile(HPDF_Doc doc, const char* filename);

/**
 * Set compression mode for the document.
 * Note: In this implementation, compression is optional and may be a no-op.
 * 
 * @param doc   Document handle
 * @param mode  Compression mode (HPDF_COMP_* flags)
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_SetCompressionMode(HPDF_Doc doc, unsigned int mode);

/**
 * Set document info attribute (metadata).
 * 
 * @param doc   Document handle
 * @param type  Attribute type (HPDF_InfoType)
 * @param value Attribute value (string)
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_SetInfoAttr(HPDF_Doc doc, HPDF_InfoType type, const char* value);

/*---------------------------------------------------------------------------*/
/*  Page Functions                                                           */
/*---------------------------------------------------------------------------*/

/**
 * Add a new page to the document.
 * 
 * @param doc   Document handle
 * @return      Page handle, or NULL on failure
 */
HPDF_Page HPDF_AddPage(HPDF_Doc doc);

/**
 * Set the width of a page.
 * 
 * @param page  Page handle
 * @param width Page width in points
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_SetWidth(HPDF_Page page, float width);

/**
 * Set the height of a page.
 * 
 * @param page   Page handle
 * @param height Page height in points
 * @return       HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_SetHeight(HPDF_Page page, float height);

/*---------------------------------------------------------------------------*/
/*  Font Functions                                                           */
/*---------------------------------------------------------------------------*/

/**
 * Get a font by name.
 * Supports PDF Base14 fonts: Helvetica, Times-Roman, Courier, etc.
 * 
 * @param doc       Document handle
 * @param font_name Font name (e.g., "Helvetica", "Times-Roman")
 * @param encoding  Encoding name (can be NULL for default)
 * @return          Font handle, or NULL if font not found
 */
HPDF_Font HPDF_GetFont(HPDF_Doc doc, const char* font_name, const char* encoding);

/**
 * Set the current font and size for a page.
 * 
 * @param page  Page handle
 * @param font  Font handle
 * @param size  Font size in points
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_SetFontAndSize(HPDF_Page page, HPDF_Font font, float size);

/*---------------------------------------------------------------------------*/
/*  Graphics State Functions                                                 */
/*---------------------------------------------------------------------------*/

/**
 * Set fill color in RGB color space.
 * 
 * @param page  Page handle
 * @param r     Red component (0.0 to 1.0)
 * @param g     Green component (0.0 to 1.0)
 * @param b     Blue component (0.0 to 1.0)
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_SetRGBFill(HPDF_Page page, float r, float g, float b);

/**
 * Set stroke color in RGB color space.
 * 
 * @param page  Page handle
 * @param r     Red component (0.0 to 1.0)
 * @param g     Green component (0.0 to 1.0)
 * @param b     Blue component (0.0 to 1.0)
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_SetRGBStroke(HPDF_Page page, float r, float g, float b);

/**
 * Set line width for stroking.
 * 
 * @param page  Page handle
 * @param width Line width in points
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_SetLineWidth(HPDF_Page page, float width);

/*---------------------------------------------------------------------------*/
/*  Path Construction Functions                                              */
/*---------------------------------------------------------------------------*/

/**
 * Append a rectangle to the current path.
 * 
 * @param page   Page handle
 * @param x      X coordinate of lower-left corner
 * @param y      Y coordinate of lower-left corner
 * @param width  Rectangle width
 * @param height Rectangle height
 * @return       HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_Rectangle(HPDF_Page page, float x, float y, float width, float height);

/**
 * Move to a new point (start a new subpath).
 * 
 * @param page  Page handle
 * @param x     X coordinate
 * @param y     Y coordinate
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_MoveTo(HPDF_Page page, float x, float y);

/**
 * Append a line from current point to specified point.
 * 
 * @param page  Page handle
 * @param x     X coordinate
 * @param y     Y coordinate
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_LineTo(HPDF_Page page, float x, float y);

/*---------------------------------------------------------------------------*/
/*  Path Painting Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * Fill the current path using non-zero winding rule.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_Fill(HPDF_Page page);

/**
 * Stroke the current path.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_Stroke(HPDF_Page page);

/**
 * Close, fill, and stroke the current path.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_ClosePathFillStroke(HPDF_Page page);

/*---------------------------------------------------------------------------*/
/*  Text Functions                                                           */
/*---------------------------------------------------------------------------*/

/**
 * Begin a text object.
 * Must be paired with HPDF_Page_EndText.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_BeginText(HPDF_Page page);

/**
 * End a text object.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_EndText(HPDF_Page page);

/**
 * Print text at specified position.
 * 
 * @param page  Page handle
 * @param x     X coordinate
 * @param y     Y coordinate
 * @param text  Text string to print
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_TextOut(HPDF_Page page, float x, float y, const char* text);

/**
 * Move text position.
 * 
 * @param page  Page handle
 * @param x     X offset
 * @param y     Y offset
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_MoveTextPos(HPDF_Page page, float x, float y);

/**
 * Show text at current position.
 * 
 * @param page  Page handle
 * @param text  Text string to show
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_ShowText(HPDF_Page page, const char* text);

/*---------------------------------------------------------------------------*/
/*  Graphics State Stack                                                     */
/*---------------------------------------------------------------------------*/

/**
 * Save the current graphics state.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_GSave(HPDF_Page page);

/**
 * Restore the previously saved graphics state.
 * 
 * @param page  Page handle
 * @return      HPDF_OK on success
 */
HPDF_STATUS HPDF_Page_GRestore(HPDF_Page page);

#ifdef __cplusplus
}
#endif

#endif /* PDF_WRITER_H */
