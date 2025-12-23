// latex_picture.hpp - LaTeX picture environment SVG rendering
// Generates SVG for \put, \line, \vector, \circle, \oval, \qbezier commands

#ifndef LATEX_PICTURE_HPP
#define LATEX_PICTURE_HPP

#include <string>
#include <sstream>
#include <vector>
#include <cmath>

namespace lambda {

// forward declarations
class HtmlGenerator;
class LatexProcessor;

/**
 * Represents a 2D coordinate/vector in the picture environment.
 * Coordinates are in TeX units (unitlength-relative).
 */
struct PictureCoord {
    double x;
    double y;
    
    PictureCoord() : x(0), y(0) {}
    PictureCoord(double x_, double y_) : x(x_), y(y_) {}
    
    PictureCoord operator+(const PictureCoord& other) const {
        return PictureCoord(x + other.x, y + other.y);
    }
    
    PictureCoord operator-(const PictureCoord& other) const {
        return PictureCoord(x - other.x, y - other.y);
    }
    
    PictureCoord operator*(double s) const {
        return PictureCoord(x * s, y * s);
    }
    
    double length() const {
        return std::sqrt(x * x + y * y);
    }
    
    PictureCoord normalize() const {
        double len = length();
        if (len < 1e-10) return PictureCoord(0, 0);
        return PictureCoord(x / len, y / len);
    }
};

/**
 * PictureContext - Tracks state during picture environment rendering.
 * Manages unitlength, line thickness, and accumulated SVG elements.
 */
class PictureContext {
public:
    PictureContext();
    
    // picture dimensions (in unitlength units)
    double width;
    double height;
    
    // offset of lower-left corner
    double x_offset;
    double y_offset;
    
    // unit length in pixels (default: 1pt = 1.333px approximately)
    double unitlength_px;
    
    // line thickness
    double line_thickness_px;
    bool thick_lines;  // \thicklines active
    
    // SVG elements accumulated
    std::vector<std::string> svg_elements;
    
    // positioned objects (text/boxes placed with \put)
    std::vector<std::string> html_objects;
    
    // convert picture coordinates to SVG coordinates
    // SVG has Y-axis pointing down, picture has Y-axis pointing up
    double toSvgX(double pic_x) const;
    double toSvgY(double pic_y) const;
    
    // convert picture length to pixels
    double toPx(double pic_len) const;
    
    // get current line thickness in pixels
    double getLineThickness() const;
    
    // set line thickness modes
    void setThickLines();
    void setThinLines();
    void setLineThickness(double pt);
    
    // reset for new picture
    void reset();
    
    // SVG generation ID counter for markers
    int marker_id;
    int getNextMarkerId() { return marker_id++; }
};

/**
 * PictureRenderer - Renders LaTeX picture commands to SVG/HTML.
 */
class PictureRenderer {
public:
    PictureRenderer(PictureContext& ctx);
    
    // high-level picture environment handling
    void beginPicture(double width, double height, double x_off = 0, double y_off = 0);
    std::string endPicture();
    
    // picture commands
    void put(double x, double y, const std::string& content);
    void line(double slope_x, double slope_y, double length);
    void vector(double slope_x, double slope_y, double length);
    void circle(double diameter, bool filled = false);
    void oval(double width, double height, const std::string& portion = "");
    void qbezier(double x1, double y1, double cx, double cy, double x2, double y2, int n = 0);
    void multiput(double x, double y, double dx, double dy, int n, const std::string& obj);
    
    // line thickness commands
    void thicklines();
    void thinlines();
    void linethickness(double pt);
    
    // current position for relative commands
    void setPosition(double x, double y);
    
private:
    PictureContext& ctx_;
    double current_x_;
    double current_y_;
    
    // SVG generation helpers
    std::string svgLine(double x1, double y1, double x2, double y2);
    std::string svgCircle(double cx, double cy, double r, bool filled);
    std::string svgVector(double x1, double y1, double x2, double y2);
    std::string svgPath(const std::string& d, bool filled = false);
    std::string svgOval(double cx, double cy, double rx, double ry, const std::string& portion);
    
    // helper: convert slope/length to endpoint
    PictureCoord slopeLengthToEnd(double slope_x, double slope_y, double length);
    
    // helper: generate arrow marker definition
    std::string generateArrowMarker(int id);
};

/**
 * Parse a picture coordinate pair from string "(x,y)"
 */
bool parsePictureCoord(const char* str, double* x, double* y);

/**
 * Parse a picture size/offset "(width,height)" or "(width,height)(x_off,y_off)"
 */
bool parsePictureSize(const char* str, double* width, double* height, 
                      double* x_off = nullptr, double* y_off = nullptr);

} // namespace lambda

#endif // LATEX_PICTURE_HPP
