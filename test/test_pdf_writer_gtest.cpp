/*
 * PDF Writer Test Suite (GTest Version)
 * =====================================
 * 
 * Tests for the Lambda PDF Writer library (pdf_writer.c/h).
 * Covers:
 * - Document creation and destruction
 * - Page management
 * - Font handling
 * - Graphics operations (colors, rectangles, paths)
 * - Text operations
 * - PDF file output
 */

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>

extern "C" {
#include "../lib/pdf_writer.h"
#include "../lib/log.h"
}

class PdfWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }

    void TearDown() override {
        // cleanup test files if any
    }
    
    // helper to check if file contains a string
    bool file_contains(const char* filename, const char* str) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        return content.find(str) != std::string::npos;
    }
    
    // helper to check if file starts with PDF header
    bool is_valid_pdf(const char* filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;
        
        char header[8];
        file.read(header, 7);
        header[7] = '\0';
        return strncmp(header, "%PDF-1.", 7) == 0;
    }
};

/*---------------------------------------------------------------------------*/
/*  Document Creation Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, CreateDocument) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr) << "HPDF_New should return non-null document";
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, CreateDocumentWithErrorHandler) {
    bool error_called = false;
    auto error_handler = [](HPDF_STATUS error_no, HPDF_STATUS detail_no, void* user_data) {
        *((bool*)user_data) = true;
    };
    
    HPDF_Doc doc = HPDF_New(error_handler, &error_called);
    ASSERT_NE(doc, nullptr);
    HPDF_Free(doc);
    // no error should have been called during normal creation
    ASSERT_FALSE(error_called);
}

TEST_F(PdfWriterTest, FreeNullDocument) {
    // should not crash
    HPDF_Free(NULL);
}

/*---------------------------------------------------------------------------*/
/*  Document Metadata Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, SetInfoAttr) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_STATUS status;
    
    status = HPDF_SetInfoAttr(doc, HPDF_INFO_CREATOR, "Test Creator");
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_SetInfoAttr(doc, HPDF_INFO_PRODUCER, "Test Producer");
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_SetInfoAttr(doc, HPDF_INFO_TITLE, "Test Title");
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_SetInfoAttr(doc, HPDF_INFO_AUTHOR, "Test Author");
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetInfoAttrNullParams) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_STATUS status;
    
    status = HPDF_SetInfoAttr(NULL, HPDF_INFO_CREATOR, "Test");
    EXPECT_EQ(status, HPDF_ERROR_INVALID_PARAM);
    
    status = HPDF_SetInfoAttr(doc, HPDF_INFO_CREATOR, NULL);
    EXPECT_EQ(status, HPDF_ERROR_INVALID_PARAM);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetCompressionMode) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_STATUS status = HPDF_SetCompressionMode(doc, HPDF_COMP_ALL);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

/*---------------------------------------------------------------------------*/
/*  Page Management Tests                                                    */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, AddPage) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_Page page = HPDF_AddPage(doc);
    ASSERT_NE(page, nullptr) << "HPDF_AddPage should return non-null page";
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, AddMultiplePages) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_Page page1 = HPDF_AddPage(doc);
    HPDF_Page page2 = HPDF_AddPage(doc);
    HPDF_Page page3 = HPDF_AddPage(doc);
    
    ASSERT_NE(page1, nullptr);
    ASSERT_NE(page2, nullptr);
    ASSERT_NE(page3, nullptr);
    ASSERT_NE(page1, page2);
    ASSERT_NE(page2, page3);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetPageDimensions) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    ASSERT_NE(page, nullptr);
    
    HPDF_STATUS status;
    
    status = HPDF_Page_SetWidth(page, 800.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_Page_SetHeight(page, 600.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetPageDimensionsNullPage) {
    HPDF_STATUS status;
    
    status = HPDF_Page_SetWidth(NULL, 800.0f);
    EXPECT_EQ(status, HPDF_ERROR_INVALID_PARAM);
    
    status = HPDF_Page_SetHeight(NULL, 600.0f);
    EXPECT_EQ(status, HPDF_ERROR_INVALID_PARAM);
}

/*---------------------------------------------------------------------------*/
/*  Font Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, GetBase14Font) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
    ASSERT_NE(font, nullptr) << "Should get Helvetica font";
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, GetMultipleFonts) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_Font helvetica = HPDF_GetFont(doc, "Helvetica", NULL);
    HPDF_Font times = HPDF_GetFont(doc, "Times-Roman", NULL);
    HPDF_Font courier = HPDF_GetFont(doc, "Courier", NULL);
    
    ASSERT_NE(helvetica, nullptr);
    ASSERT_NE(times, nullptr);
    ASSERT_NE(courier, nullptr);
    ASSERT_NE(helvetica, times);
    ASSERT_NE(times, courier);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, GetSameFontTwice) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    ASSERT_NE(doc, nullptr);
    
    HPDF_Font font1 = HPDF_GetFont(doc, "Helvetica", NULL);
    HPDF_Font font2 = HPDF_GetFont(doc, "Helvetica", NULL);
    
    ASSERT_NE(font1, nullptr);
    ASSERT_EQ(font1, font2) << "Getting same font twice should return same handle";
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetFontAndSize) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
    
    HPDF_STATUS status = HPDF_Page_SetFontAndSize(page, font, 12.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

/*---------------------------------------------------------------------------*/
/*  Graphics State Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, SetRGBFill) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status = HPDF_Page_SetRGBFill(page, 1.0f, 0.0f, 0.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetRGBStroke) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status = HPDF_Page_SetRGBStroke(page, 0.0f, 1.0f, 0.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SetLineWidth) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status = HPDF_Page_SetLineWidth(page, 2.5f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, GSaveGRestore) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status;
    
    status = HPDF_Page_GSave(page);
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_Page_GRestore(page);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

/*---------------------------------------------------------------------------*/
/*  Path Construction and Painting Tests                                     */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, Rectangle) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status = HPDF_Page_Rectangle(page, 100.0f, 100.0f, 200.0f, 150.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, MoveToLineTo) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status;
    
    status = HPDF_Page_MoveTo(page, 50.0f, 50.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_Page_LineTo(page, 200.0f, 200.0f);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, FillPath) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_Page_Rectangle(page, 100.0f, 100.0f, 200.0f, 150.0f);
    HPDF_STATUS status = HPDF_Page_Fill(page);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, StrokePath) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_Page_Rectangle(page, 100.0f, 100.0f, 200.0f, 150.0f);
    HPDF_STATUS status = HPDF_Page_Stroke(page);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

/*---------------------------------------------------------------------------*/
/*  Text Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, BeginEndText) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status;
    
    status = HPDF_Page_BeginText(page);
    EXPECT_EQ(status, HPDF_OK);
    
    status = HPDF_Page_EndText(page);
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, BeginTextTwice) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_Page_BeginText(page);
    HPDF_STATUS status = HPDF_Page_BeginText(page);
    EXPECT_EQ(status, HPDF_ERROR_INVALID_STATE) << "Should not allow nested BeginText";
    
    HPDF_Page_EndText(page);
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, EndTextWithoutBegin) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    
    HPDF_STATUS status = HPDF_Page_EndText(page);
    EXPECT_EQ(status, HPDF_ERROR_INVALID_STATE) << "EndText without BeginText should error";
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, TextOut) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
    
    HPDF_Page_SetFontAndSize(page, font, 12.0f);
    HPDF_STATUS status = HPDF_Page_TextOut(page, 100.0f, 700.0f, "Hello, World!");
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, ShowText) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
    
    HPDF_Page_SetFontAndSize(page, font, 12.0f);
    HPDF_Page_BeginText(page);
    HPDF_Page_MoveTextPos(page, 100.0f, 700.0f);
    HPDF_STATUS status = HPDF_Page_ShowText(page, "Hello, World!");
    EXPECT_EQ(status, HPDF_OK);
    HPDF_Page_EndText(page);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, TextWithSpecialChars) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
    
    HPDF_Page_SetFontAndSize(page, font, 12.0f);
    // test special characters that need escaping: ( ) and backslash
    HPDF_STATUS status = HPDF_Page_TextOut(page, 100.0f, 700.0f, "Test (parens) and \\backslash");
    EXPECT_EQ(status, HPDF_OK);
    
    HPDF_Free(doc);
}

/*---------------------------------------------------------------------------*/
/*  File Output Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, SaveEmptyDocument) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_AddPage(doc);  // need at least one page
    
    const char* filename = "test_output/test_empty.pdf";
    HPDF_STATUS status = HPDF_SaveToFile(doc, filename);
    EXPECT_EQ(status, HPDF_OK);
    
    EXPECT_TRUE(is_valid_pdf(filename)) << "Output should be valid PDF";
    
    HPDF_Free(doc);
    remove(filename);
}

TEST_F(PdfWriterTest, SaveWithMetadata) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_SetInfoAttr(doc, HPDF_INFO_CREATOR, "Lambda PDF Writer Test");
    HPDF_SetInfoAttr(doc, HPDF_INFO_TITLE, "Test Document");
    HPDF_AddPage(doc);
    
    const char* filename = "test_output/test_metadata.pdf";
    HPDF_STATUS status = HPDF_SaveToFile(doc, filename);
    EXPECT_EQ(status, HPDF_OK);
    
    EXPECT_TRUE(is_valid_pdf(filename));
    EXPECT_TRUE(file_contains(filename, "Lambda PDF Writer Test"));
    EXPECT_TRUE(file_contains(filename, "Test Document"));
    
    HPDF_Free(doc);
    remove(filename);
}

TEST_F(PdfWriterTest, SaveWithText) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Page_SetWidth(page, 612.0f);
    HPDF_Page_SetHeight(page, 792.0f);
    
    HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
    HPDF_Page_SetFontAndSize(page, font, 24.0f);
    HPDF_Page_TextOut(page, 100.0f, 700.0f, "Hello PDF!");
    
    const char* filename = "test_output/test_text.pdf";
    HPDF_STATUS status = HPDF_SaveToFile(doc, filename);
    EXPECT_EQ(status, HPDF_OK);
    
    EXPECT_TRUE(is_valid_pdf(filename));
    EXPECT_TRUE(file_contains(filename, "Hello PDF!"));
    EXPECT_TRUE(file_contains(filename, "/Helvetica"));
    
    HPDF_Free(doc);
    remove(filename);
}

TEST_F(PdfWriterTest, SaveWithGraphics) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Page_SetWidth(page, 612.0f);
    HPDF_Page_SetHeight(page, 792.0f);
    
    // draw a red filled rectangle
    HPDF_Page_SetRGBFill(page, 1.0f, 0.0f, 0.0f);
    HPDF_Page_Rectangle(page, 100.0f, 600.0f, 200.0f, 100.0f);
    HPDF_Page_Fill(page);
    
    // draw a blue stroked rectangle
    HPDF_Page_SetRGBStroke(page, 0.0f, 0.0f, 1.0f);
    HPDF_Page_SetLineWidth(page, 2.0f);
    HPDF_Page_Rectangle(page, 100.0f, 400.0f, 200.0f, 100.0f);
    HPDF_Page_Stroke(page);
    
    const char* filename = "test_output/test_graphics.pdf";
    HPDF_STATUS status = HPDF_SaveToFile(doc, filename);
    EXPECT_EQ(status, HPDF_OK);
    
    EXPECT_TRUE(is_valid_pdf(filename));
    // check for PDF operators
    EXPECT_TRUE(file_contains(filename, "rg"));  // fill color
    EXPECT_TRUE(file_contains(filename, "re"));  // rectangle
    EXPECT_TRUE(file_contains(filename, "f"));   // fill
    
    HPDF_Free(doc);
    remove(filename);
}

TEST_F(PdfWriterTest, SaveMultiplePages) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    
    for (int i = 0; i < 3; i++) {
        HPDF_Page page = HPDF_AddPage(doc);
        HPDF_Page_SetWidth(page, 612.0f);
        HPDF_Page_SetHeight(page, 792.0f);
        
        HPDF_Font font = HPDF_GetFont(doc, "Helvetica", NULL);
        HPDF_Page_SetFontAndSize(page, font, 18.0f);
        
        char text[32];
        snprintf(text, sizeof(text), "Page %d", i + 1);
        HPDF_Page_TextOut(page, 100.0f, 700.0f, text);
    }
    
    const char* filename = "test_output/test_multipage.pdf";
    HPDF_STATUS status = HPDF_SaveToFile(doc, filename);
    EXPECT_EQ(status, HPDF_OK);
    
    EXPECT_TRUE(is_valid_pdf(filename));
    EXPECT_TRUE(file_contains(filename, "/Count 3"));  // 3 pages
    
    HPDF_Free(doc);
    remove(filename);
}

TEST_F(PdfWriterTest, SaveToInvalidPath) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_AddPage(doc);
    
    // try to save to non-existent directory
    HPDF_STATUS status = HPDF_SaveToFile(doc, "/nonexistent/dir/test.pdf");
    EXPECT_EQ(status, HPDF_ERROR_FILE_IO);
    
    HPDF_Free(doc);
}

TEST_F(PdfWriterTest, SaveWithNullParams) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    HPDF_AddPage(doc);
    
    HPDF_STATUS status;
    
    status = HPDF_SaveToFile(NULL, "test.pdf");
    EXPECT_EQ(status, HPDF_ERROR_INVALID_PARAM);
    
    status = HPDF_SaveToFile(doc, NULL);
    EXPECT_EQ(status, HPDF_ERROR_INVALID_PARAM);
    
    HPDF_Free(doc);
}

/*---------------------------------------------------------------------------*/
/*  Integration Test - Complex Document                                      */
/*---------------------------------------------------------------------------*/

TEST_F(PdfWriterTest, ComplexDocument) {
    HPDF_Doc doc = HPDF_New(NULL, NULL);
    
    HPDF_SetInfoAttr(doc, HPDF_INFO_CREATOR, "Lambda PDF Writer");
    HPDF_SetInfoAttr(doc, HPDF_INFO_TITLE, "Complex Test Document");
    HPDF_SetInfoAttr(doc, HPDF_INFO_AUTHOR, "Test Suite");
    HPDF_SetCompressionMode(doc, HPDF_COMP_ALL);
    
    HPDF_Page page = HPDF_AddPage(doc);
    HPDF_Page_SetWidth(page, 612.0f);
    HPDF_Page_SetHeight(page, 792.0f);
    
    // title
    HPDF_Font helvetica_bold = HPDF_GetFont(doc, "Helvetica-Bold", NULL);
    HPDF_Page_SetFontAndSize(page, helvetica_bold, 24.0f);
    HPDF_Page_SetRGBFill(page, 0.0f, 0.0f, 0.5f);  // dark blue
    HPDF_Page_TextOut(page, 100.0f, 720.0f, "PDF Writer Test Document");
    
    // body text
    HPDF_Font helvetica = HPDF_GetFont(doc, "Helvetica", NULL);
    HPDF_Page_SetFontAndSize(page, helvetica, 12.0f);
    HPDF_Page_SetRGBFill(page, 0.0f, 0.0f, 0.0f);  // black
    HPDF_Page_TextOut(page, 100.0f, 680.0f, "This is a test of the Lambda PDF Writer library.");
    HPDF_Page_TextOut(page, 100.0f, 660.0f, "It supports text, graphics, and multiple fonts.");
    
    // draw a colored box
    HPDF_Page_GSave(page);
    HPDF_Page_SetRGBFill(page, 0.9f, 0.9f, 0.95f);  // light blue bg
    HPDF_Page_Rectangle(page, 80.0f, 500.0f, 450.0f, 120.0f);
    HPDF_Page_Fill(page);
    
    HPDF_Page_SetRGBStroke(page, 0.0f, 0.0f, 0.8f);  // blue border
    HPDF_Page_SetLineWidth(page, 2.0f);
    HPDF_Page_Rectangle(page, 80.0f, 500.0f, 450.0f, 120.0f);
    HPDF_Page_Stroke(page);
    HPDF_Page_GRestore(page);
    
    // text inside box
    HPDF_Font courier = HPDF_GetFont(doc, "Courier", NULL);
    HPDF_Page_SetFontAndSize(page, courier, 10.0f);
    HPDF_Page_TextOut(page, 100.0f, 600.0f, "Features:");
    HPDF_Page_TextOut(page, 120.0f, 580.0f, "- Base14 fonts (Helvetica, Times, Courier)");
    HPDF_Page_TextOut(page, 120.0f, 560.0f, "- RGB colors for fill and stroke");
    HPDF_Page_TextOut(page, 120.0f, 540.0f, "- Rectangle and path operations");
    HPDF_Page_TextOut(page, 120.0f, 520.0f, "- Graphics state save/restore");
    
    const char* filename = "test_output/test_complex.pdf";
    HPDF_STATUS status = HPDF_SaveToFile(doc, filename);
    EXPECT_EQ(status, HPDF_OK);
    
    EXPECT_TRUE(is_valid_pdf(filename));
    EXPECT_TRUE(file_contains(filename, "PDF Writer Test Document"));
    EXPECT_TRUE(file_contains(filename, "/Helvetica-Bold"));
    EXPECT_TRUE(file_contains(filename, "/Courier"));
    
    HPDF_Free(doc);
    // keep this file for manual inspection
    // remove(filename);
}
