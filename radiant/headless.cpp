// headless.cpp
// Null windowing backend for the lambda-headless build (LAMBDA_NO_GUI): the
// binary links neither GLFW nor OpenGL nor the native GUI toolkits.
//
// Contract: every GLFW entry behaves as GLFW does before glfwInit() succeeds —
// glfwInit() fails and every other call is a no-op returning zero/NULL. That is
// the state Radiant's windowless mode already runs in (ui_context_init never
// initializes GLFW when headless), so layout, render and event simulation take
// their existing paths. The OpenGL entries are reachable only from a live
// window's paint loop, which this backend never creates.

#ifdef LAMBDA_NO_GUI

#include <GLFW/glfw3.h>
#include <stddef.h>

extern "C" {

// --- GLFW: library lifetime ---

int glfwInit(void) { return GLFW_FALSE; }
void glfwTerminate(void) {}
void glfwInitHint(int hint, int value) {}
double glfwGetTime(void) { return 0.0; }

// --- GLFW: window ---

void glfwWindowHint(int hint, int value) {}
GLFWwindow* glfwCreateWindow(int width, int height, const char* title,
        GLFWmonitor* monitor, GLFWwindow* share) {
    return NULL;
}
void glfwDestroyWindow(GLFWwindow* window) {}
void glfwShowWindow(GLFWwindow* window) {}
void glfwFocusWindow(GLFWwindow* window) {}
// the one non-zero answer: a paint loop handed no window must end, not spin
int glfwWindowShouldClose(GLFWwindow* window) { return GLFW_TRUE; }
void glfwSetWindowShouldClose(GLFWwindow* window, int value) {}
void glfwSetWindowTitle(GLFWwindow* window, const char* title) {}
void glfwSetWindowIcon(GLFWwindow* window, int count, const GLFWimage* images) {}
void glfwGetWindowPos(GLFWwindow* window, int* xpos, int* ypos) {
    if (xpos) *xpos = 0;
    if (ypos) *ypos = 0;
}
void glfwGetWindowSize(GLFWwindow* window, int* width, int* height) {
    if (width) *width = 0;
    if (height) *height = 0;
}
void glfwGetFramebufferSize(GLFWwindow* window, int* width, int* height) {
    if (width) *width = 0;
    if (height) *height = 0;
}
void glfwGetWindowContentScale(GLFWwindow* window, float* xscale, float* yscale) {
    if (xscale) *xscale = 0.0f;
    if (yscale) *yscale = 0.0f;
}

// --- GLFW: context ---

void glfwMakeContextCurrent(GLFWwindow* window) {}
void glfwSwapInterval(int interval) {}
void glfwSwapBuffers(GLFWwindow* window) {}

// --- GLFW: events and input ---

void glfwPollEvents(void) {}
void glfwWaitEventsTimeout(double timeout) {}
void glfwPostEmptyEvent(void) {}
void glfwSetInputMode(GLFWwindow* window, int mode, int value) {}
void glfwGetCursorPos(GLFWwindow* window, double* xpos, double* ypos) {
    if (xpos) *xpos = 0.0;
    if (ypos) *ypos = 0.0;
}
GLFWcursor* glfwCreateStandardCursor(int shape) { return NULL; }
void glfwDestroyCursor(GLFWcursor* cursor) {}
void glfwSetCursor(GLFWwindow* window, GLFWcursor* cursor) {}
const char* glfwGetClipboardString(GLFWwindow* window) { return NULL; }
void glfwSetClipboardString(GLFWwindow* window, const char* string) {}

// each setter returns the previously installed callback: none
#define NULL_GLFW_CALLBACK_SETTER(setter, callback_type) \
    callback_type setter(GLFWwindow* window, callback_type callback) { return NULL; }
NULL_GLFW_CALLBACK_SETTER(glfwSetKeyCallback, GLFWkeyfun)
NULL_GLFW_CALLBACK_SETTER(glfwSetCharCallback, GLFWcharfun)
NULL_GLFW_CALLBACK_SETTER(glfwSetCursorPosCallback, GLFWcursorposfun)
NULL_GLFW_CALLBACK_SETTER(glfwSetMouseButtonCallback, GLFWmousebuttonfun)
NULL_GLFW_CALLBACK_SETTER(glfwSetScrollCallback, GLFWscrollfun)
NULL_GLFW_CALLBACK_SETTER(glfwSetFramebufferSizeCallback, GLFWframebuffersizefun)
NULL_GLFW_CALLBACK_SETTER(glfwSetWindowRefreshCallback, GLFWwindowrefreshfun)
NULL_GLFW_CALLBACK_SETTER(glfwSetWindowCloseCallback, GLFWwindowclosefun)
NULL_GLFW_CALLBACK_SETTER(glfwSetWindowContentScaleCallback, GLFWwindowcontentscalefun)
#undef NULL_GLFW_CALLBACK_SETTER

// --- OpenGL: the window surface blit in window.cpp ---

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {}
void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {}
void glClear(GLbitfield mask) {}
void glEnable(GLenum cap) {}
void glDisable(GLenum cap) {}
void glFinish(void) {}
void glMatrixMode(GLenum mode) {}
void glLoadIdentity(void) {}
void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
        GLdouble near_val, GLdouble far_val) {}
void glGenTextures(GLsizei n, GLuint* textures) {
    for (GLsizei i = 0; textures && i < n; i++) textures[i] = 0;
}
void glDeleteTextures(GLsizei n, const GLuint* textures) {}
void glBindTexture(GLenum target, GLuint texture) {}
void glPixelStorei(GLenum pname, GLint param) {}
void glTexParameteri(GLenum target, GLenum pname, GLint param) {}
void glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width,
        GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* pixels) {}
void glBegin(GLenum mode) {}
void glEnd(void) {}
void glTexCoord2f(GLfloat s, GLfloat t) {}
void glVertex2f(GLfloat x, GLfloat y) {}

// --- native toolkit shims (ime_mac.mm, app_icon_mac.mm are not built) ---

void radiant_ime_mac_attach(void* uicon) {}
void radiant_app_icon_mac_set(const unsigned char* png, size_t length) {}

} // extern "C"

#endif // LAMBDA_NO_GUI
