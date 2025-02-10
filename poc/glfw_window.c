#include <stdio.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wchar.h>
#include <locale.h>
#include <resvg.h>

void render_svg(unsigned char* surface_data, int bmp_width, int bmp_height);

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void character_callback(GLFWwindow* window, unsigned int codepoint) {
    // codepoint in UFT32
    if (codepoint > 127) {
        // wchar_t unicodeChar = codepoint;
        wprintf(L"Unicode codepoint: %u, %lc\n", codepoint, codepoint);
    } else {
        printf("Character entered: %u, %c\n", codepoint, codepoint);
    }
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    printf("Cursor position: (%.2f, %.2f)\n", xpos, ypos);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
        printf("Right mouse button pressed\n");
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
        printf("Right mouse button released\n");
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        printf("Left mouse button pressed\n");
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
        printf("Left mouse button released\n");
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    printf("Scroll offset: (%.2f, %.2f)\n", xoffset, yoffset);
}

// Window dimensions
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
resvg_options *resvg_opt;
resvg_render_tree *tree;
bool is_svg_dirty = true;

void render_text_to_screen(GLFWwindow *window, const char *text, const char *font_path, int font_size) {
    int canvas_width = 400, canvas_height = 400;

    // Initialize FreeType
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        fprintf(stderr, "Error: Could not initialize FreeType library.\n");
        return;
    }

    // Load the font
    FT_Face face;
    if (FT_New_Face(ft, font_path, 0, &face)) {
        fprintf(stderr, "Error: Could not load font at path %s.\n", font_path);
        FT_Done_FreeType(ft);
        return;
    }

    // Set font size
    FT_Set_Pixel_Sizes(face, 0, font_size);

    // Determine the size of the big bitmap
    int total_width = 0;
    int max_height = 0;

    for (const char *p = text; *p; p++) {
        if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) {
            fprintf(stderr, "Error: Could not load glyph for character '%c'.\n", *p);
            continue;
        }

        total_width += face->glyph->bitmap.width;
        if (face->glyph->bitmap.rows > max_height) {
            max_height = face->glyph->bitmap.rows;
        }
    }

    // Create a large buffer for the bitmap
    unsigned char *big_bitmap = (unsigned char *)calloc(canvas_width * canvas_height * 4, sizeof(unsigned char));
    if (!big_bitmap) {
        fprintf(stderr, "Error: Could not allocate memory for the big bitmap.\n");
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return;
    }

    // Render all glyphs into the bitmap
    int x_offset = 0;
    for (const char *p = text; *p; p++) {
        if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) {
            fprintf(stderr, "Error: Could not load glyph for character '%c'.\n", *p);
            continue;
        }

        FT_Bitmap bitmap = face->glyph->bitmap;
        for (int y = 0; y < bitmap.rows; y++) {
            for (int x = 0; x < bitmap.width; x++) {
                int big_x = x + x_offset;
                int big_y = bitmap.rows - 1 - y; // flip vertically
                int index = (big_y * canvas_width + big_x) * 4;
                unsigned char value = bitmap.buffer[y * bitmap.width + x];
                big_bitmap[index + 0] = value; // Red
                big_bitmap[index + 1] = value; // Green
                big_bitmap[index + 2] = value; // Blue          
                big_bitmap[index + 3] = 255; // Alpha      
            }
        }
        x_offset += bitmap.width;
    }

    render_svg(big_bitmap, canvas_width, canvas_height);

    // Generate a texture for the big bitmap
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        // GL_TEXTURE_2D, 0, GL_RED, total_width, max_height, 0, GL_RED, GL_UNSIGNED_BYTE, big_bitmap
        // GL_TEXTURE_2D, 0, GL_RGB, total_width, max_height, 0, GL_RGB, GL_UNSIGNED_BYTE, big_bitmap
        GL_TEXTURE_2D, 0, GL_RGBA, canvas_width, canvas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, big_bitmap
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Render the big bitmap as a quad
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);

    float x_ratio = canvas_width / (float)WINDOW_WIDTH / 2;
    float y_ratio = canvas_height / (float)WINDOW_HEIGHT / 2;

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-x_ratio, -y_ratio);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x_ratio, -y_ratio);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x_ratio, y_ratio);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-x_ratio, y_ratio);
    glEnd();

    glDisable(GL_TEXTURE_2D);

    // Cleanup
    free(big_bitmap);
    glDeleteTextures(1, &texture);

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
}

void render_svg(unsigned char* surface_data, int bmp_width, int bmp_height) {
    if (is_svg_dirty) {
        printf("Rendering SVG\n");
    } else {
        printf("skip SVG rendering\n");
        return;
    }

    resvg_size size = resvg_get_image_size(tree);
    int width = (int)size.width;
    int height = (int)size.height;
    printf("SVG width: %d, height: %d\n", width, height);

    resvg_render(tree, resvg_transform_identity(), bmp_width, bmp_height, (char*)surface_data);    

    // De-initialize the allocated memory
    // resvg_tree_destroy(tree);
    is_svg_dirty = false;
    printf("SVG rendered\n");
}

resvg_options* resvg_lib_init() {
    // Initialize the resvg library
    resvg_init_log();  // Initialize resvg's library logging system
    resvg_opt = resvg_options_create();
    resvg_options_load_system_fonts(resvg_opt);
    // Optionally, you can add some CSS to control the SVG rendering.
    resvg_options_set_stylesheet(resvg_opt, "svg { fill: black; }");

    // Construct a tree from the svg file and pass in some options
    int err = resvg_parse_tree_from_file("./tiger.svg", resvg_opt, &tree);
    // resvg_options_destroy(opt);
    if (err != RESVG_OK) {
        printf("Error id: %i\n", err);
        abort();
    }    
}

int main() {
    setlocale(LC_ALL, "");  // Set locale to support Unicode (input)

    resvg_lib_init();

    // Initialize GLFW
    if (!glfwInit()) {
        fprintf(stderr, "Error: Could not initialize GLFW.\n");
        return -1;
    }

    // Create a windowed mode window and its OpenGL context
    GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "FreeType and GLFW Text Rendering", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Error: Could not create GLFW window.\n");
        glfwTerminate();
        return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(window);
    // Set up OpenGL
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Disable byte-alignment restriction

    glfwSetInputMode(window, GLFW_LOCK_KEY_MODS, GLFW_TRUE);  // receive the state of the Caps Lock and Num Lock keys
    glfwSetKeyCallback(window, key_callback);  // receive raw keyboard input
    glfwSetCharCallback(window, character_callback);  // receive character input
    // glfwSetCursorPosCallback(window, cursor_position_callback);  // receive cursor position
    glfwSetMouseButtonCallback(window, mouse_button_callback);  // receive mouse button input
    glfwSetScrollCallback(window, scroll_callback);  // receive mouse/touchpad scroll input    

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        // Clear the screen
        glClear(GL_COLOR_BUFFER_BIT);

        // Render text to screen
        render_text_to_screen(window, "Hello, FreeType!!!", "../test/lato.ttf", 48);

        // Swap front and back buffers
        glfwSwapBuffers(window);

        // Poll for and process events
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

/*
clang window.c -o window  -framework OpenGL \
-lfreetype -L/opt/homebrew/Cellar/freetype/2.13.3/lib -I/opt/homebrew/Cellar/freetype/2.13.3/include/freetype2 \
-lglfw -I/opt/homebrew/include -L/opt/homebrew/lib \
-lresvg -I./lib/resvg/crates/c-api -L./lib/resvg/target/release 
*/