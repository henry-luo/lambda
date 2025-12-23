// latex_picture.cpp - LaTeX picture environment SVG rendering implementation

#include "latex_picture.hpp"
#include "../../lib/log.h"
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <algorithm>

namespace lambda {

// =============================================================================
// PictureContext implementation
// =============================================================================

PictureContext::PictureContext()
    : width(0), height(0)
    , x_offset(0), y_offset(0)
    , unitlength_px(1.0)  // default: 1pt, will be set by \setlength{\unitlength}
    , line_thickness_px(0.531)  // 0.4pt default (thin lines)
    , thick_lines(false)
    , marker_id(1000)
{
}

void PictureContext::reset() {
    width = 0;
    height = 0;
    x_offset = 0;
    y_offset = 0;
    line_thickness_px = thick_lines ? 1.063 : 0.531;  // 0.8pt or 0.4pt
    svg_elements.clear();
    html_objects.clear();
}

double PictureContext::toSvgX(double pic_x) const {
    // picture X coordinate to SVG X (adjusted for offset)
    return (pic_x - x_offset) * unitlength_px;
}

double PictureContext::toSvgY(double pic_y) const {
    // picture Y coordinate to SVG Y (inverted, since SVG Y goes down)
    // In picture environment, origin is at bottom-left
    // In SVG, origin is at top-left
    return (height - (pic_y - y_offset)) * unitlength_px;
}

double PictureContext::toPx(double pic_len) const {
    return pic_len * unitlength_px;
}

double PictureContext::getLineThickness() const {
    return line_thickness_px;
}

void PictureContext::setThickLines() {
    thick_lines = true;
    line_thickness_px = 1.063;  // 0.8pt
}

void PictureContext::setThinLines() {
    thick_lines = false;
    line_thickness_px = 0.531;  // 0.4pt
}

void PictureContext::setLineThickness(double pt) {
    // pt to px conversion (1pt ≈ 1.333px)
    line_thickness_px = pt * 1.333;
}

// =============================================================================
// PictureRenderer implementation
// =============================================================================

PictureRenderer::PictureRenderer(PictureContext& ctx)
    : ctx_(ctx)
    , current_x_(0)
    , current_y_(0)
{
}

void PictureRenderer::beginPicture(double width, double height, double x_off, double y_off) {
    ctx_.reset();
    ctx_.width = width;
    ctx_.height = height;
    ctx_.x_offset = x_off;
    ctx_.y_offset = y_off;
    current_x_ = 0;
    current_y_ = 0;
    
    log_debug("beginPicture: size=(%.2f,%.2f) offset=(%.2f,%.2f) unitlength=%.2fpx",
              width, height, x_off, y_off, ctx_.unitlength_px);
}

std::string PictureRenderer::endPicture() {
    std::ostringstream html;
    
    double pic_width_px = ctx_.toPx(ctx_.width);
    double pic_height_px = ctx_.toPx(ctx_.height);
    
    // outer picture container
    html << "<span class=\"picture\" style=\"width:" << std::fixed << std::setprecision(3)
         << pic_width_px << "px;height:" << pic_height_px << "px\">";
    
    // picture canvas (for positioning)
    html << "<span class=\"picture-canvas\"";
    
    // if there's an offset, apply it to the canvas
    if (ctx_.x_offset != 0 || ctx_.y_offset != 0) {
        double left = -ctx_.x_offset * ctx_.unitlength_px;
        double bottom = -ctx_.y_offset * ctx_.unitlength_px;
        html << " style=\"left:" << left << "px;bottom:" << bottom << "px\"";
    }
    html << ">";
    
    // add accumulated HTML objects (from \put with text/boxes)
    for (const auto& obj : ctx_.html_objects) {
        html << obj;
    }
    
    // if there are SVG elements, wrap them in an SVG
    if (!ctx_.svg_elements.empty()) {
        html << "<span class=\"picture-object\" style=\"left:0px;bottom:0px\">";
        html << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
             << "width=\"" << pic_width_px << "px\" height=\"" << pic_height_px << "px\" "
             << "viewBox=\"0 0 " << pic_width_px << " " << pic_height_px << "\" "
             << "transform=\"matrix(1,0,0,-1,0,0)\">";  // flip Y-axis
        
        // add defs for markers if needed
        html << "<defs>";
        // markers will be added by vector commands
        html << "</defs>";
        
        for (const auto& elem : ctx_.svg_elements) {
            html << elem;
        }
        
        html << "</svg></span>";
    }
    
    html << "</span></span>";
    
    return html.str();
}

void PictureRenderer::setPosition(double x, double y) {
    current_x_ = x;
    current_y_ = y;
}

void PictureRenderer::put(double x, double y, const std::string& content) {
    // \put(x,y){content} - place content at position (x,y)
    std::ostringstream html;
    
    double left_px = ctx_.toPx(x - ctx_.x_offset);
    double bottom_px = ctx_.toPx(y - ctx_.y_offset);
    
    html << "<span class=\"hbox rlap\"><span class=\"picture\">";
    html << "<span class=\"put-obj\" style=\"left:" << std::fixed << std::setprecision(3)
         << left_px << "px\">";
    html << content;
    html << "</span>";
    
    // add strut for vertical positioning if y > 0
    if (y > ctx_.y_offset) {
        html << "<span class=\"strut\" style=\"height:" << bottom_px << "px\"></span>";
    }
    
    html << "</span></span>";
    
    ctx_.html_objects.push_back(html.str());
    
    log_debug("put: (%.2f,%.2f) content='%s'", x, y, content.c_str());
}

PictureCoord PictureRenderer::slopeLengthToEnd(double slope_x, double slope_y, double length) {
    // convert (slope_x, slope_y, length) to endpoint coordinates
    // length is horizontal if slope_x != 0, otherwise vertical
    
    if (slope_x == 0 && slope_y == 0) {
        log_warn("slopeLengthToEnd: illegal slope (0,0)");
        return PictureCoord(0, 0);
    }
    
    double x, y;
    if (slope_x == 0) {
        // vertical line
        x = 0;
        y = (slope_y > 0 ? length : -length);
    } else {
        // horizontal component is length
        x = length;
        y = length * (slope_y / slope_x);
        
        if (slope_x < 0) {
            x = -x;
            y = -y;
        }
    }
    
    return PictureCoord(x, y);
}

std::string PictureRenderer::svgLine(double x1, double y1, double x2, double y2) {
    std::ostringstream svg;
    double thickness = ctx_.getLineThickness();
    
    // convert to SVG coordinates
    double sx1 = ctx_.toPx(x1 - ctx_.x_offset);
    double sy1 = ctx_.toPx(y1 - ctx_.y_offset);
    double sx2 = ctx_.toPx(x2 - ctx_.x_offset);
    double sy2 = ctx_.toPx(y2 - ctx_.y_offset);
    
    svg << "<line x1=\"" << std::fixed << std::setprecision(3) << sx1
        << "\" y1=\"" << sy1
        << "\" x2=\"" << sx2
        << "\" y2=\"" << sy2
        << "\" stroke-width=\"" << thickness << "px\" stroke=\"#000000\"/>";
    
    return svg.str();
}

std::string PictureRenderer::svgCircle(double cx, double cy, double r, bool filled) {
    std::ostringstream svg;
    double thickness = ctx_.getLineThickness();
    
    // convert to SVG coordinates
    double scx = ctx_.toPx(cx - ctx_.x_offset);
    double scy = ctx_.toPx(cy - ctx_.y_offset);
    double sr = ctx_.toPx(r);
    
    svg << "<circle cx=\"" << std::fixed << std::setprecision(3) << scx
        << "\" cy=\"" << scy
        << "\" r=\"" << sr << "\"";
    
    if (filled) {
        svg << " fill=\"#000000\" stroke-width=\"0\"";
    } else {
        svg << " fill=\"none\" stroke=\"#000000\" stroke-width=\"" << thickness << "px\"";
    }
    
    svg << "/>";
    
    return svg.str();
}

std::string PictureRenderer::generateArrowMarker(int id) {
    std::ostringstream svg;
    
    // arrow head dimensions (based on LaTeX.js)
    double hl = 9.75;  // head length
    double hw = 5.85;  // head width
    
    svg << "<marker markerWidth=\"" << hl << "\" markerHeight=\"" << hw
        << "\" refX=\"" << (hl / 2) << "\" refY=\"" << (hw / 2)
        << "\" viewBox=\"0 0 " << hl << " " << hw
        << "\" orient=\"auto\" id=\"SvgjsMarker" << id << "\">"
        << "<path d=\"M0,0 Q" << (2 * hl / 3) << "," << (hw / 2)
        << " " << hl << "," << (hw / 2)
        << " Q" << (2 * hl / 3) << "," << (hw / 2)
        << " 0," << hw << " z\"/></marker>";
    
    return svg.str();
}

std::string PictureRenderer::svgVector(double x1, double y1, double x2, double y2) {
    std::ostringstream svg;
    double thickness = ctx_.getLineThickness();
    int marker_id = ctx_.getNextMarkerId();
    
    // convert to SVG coordinates
    double sx1 = ctx_.toPx(x1 - ctx_.x_offset);
    double sy1 = ctx_.toPx(y1 - ctx_.y_offset);
    double sx2 = ctx_.toPx(x2 - ctx_.x_offset);
    double sy2 = ctx_.toPx(y2 - ctx_.y_offset);
    
    // add the arrow marker
    svg << "<defs>" << generateArrowMarker(marker_id) << "</defs>";
    
    // shorten line slightly to account for arrow head
    // (arrow head is at the end)
    
    svg << "<line x1=\"" << std::fixed << std::setprecision(3) << sx1
        << "\" y1=\"" << sy1
        << "\" x2=\"" << sx2
        << "\" y2=\"" << sy2
        << "\" stroke-width=\"" << thickness << "px\" stroke=\"#000000\""
        << " marker-end=\"url(#SvgjsMarker" << marker_id << ")\"/>";
    
    return svg.str();
}

std::string PictureRenderer::svgPath(const std::string& d, bool filled) {
    std::ostringstream svg;
    double thickness = ctx_.getLineThickness();
    
    svg << "<path d=\"" << d << "\" stroke-width=\"" << thickness
        << "px\" stroke=\"#000000\"";
    
    if (filled) {
        svg << " fill=\"#000000\"";
    } else {
        svg << " fill=\"none\"";
    }
    
    svg << "/>";
    
    return svg.str();
}

std::string PictureRenderer::svgOval(double cx, double cy, double rx, double ry, const std::string& portion) {
    std::ostringstream svg;
    double thickness = ctx_.getLineThickness();
    
    // convert to SVG coordinates
    double scx = ctx_.toPx(cx - ctx_.x_offset);
    double scy = ctx_.toPx(cy - ctx_.y_offset);
    double srx = ctx_.toPx(rx);
    double sry = ctx_.toPx(ry);
    
    if (portion.empty()) {
        // full oval (ellipse)
        svg << "<ellipse cx=\"" << std::fixed << std::setprecision(3) << scx
            << "\" cy=\"" << scy
            << "\" rx=\"" << srx
            << "\" ry=\"" << sry
            << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"" << thickness << "px\"/>";
    } else {
        // partial oval using arc paths
        // portion can be: t, b, l, r, tl, tr, bl, br
        std::ostringstream path;
        path << std::fixed << std::setprecision(3);
        
        // calculate corner points
        double left = scx - srx;
        double right = scx + srx;
        double top = scy - sry;
        double bottom = scy + sry;
        
        if (portion == "t") {
            // top half
            path << "M" << left << "," << scy << " A" << srx << "," << sry << " 0 0 1 " << right << "," << scy;
        } else if (portion == "b") {
            // bottom half
            path << "M" << right << "," << scy << " A" << srx << "," << sry << " 0 0 1 " << left << "," << scy;
        } else if (portion == "l") {
            // left half
            path << "M" << scx << "," << top << " A" << srx << "," << sry << " 0 0 0 " << scx << "," << bottom;
        } else if (portion == "r") {
            // right half
            path << "M" << scx << "," << bottom << " A" << srx << "," << sry << " 0 0 0 " << scx << "," << top;
        } else if (portion == "tl") {
            // top-left quarter
            path << "M" << left << "," << scy << " A" << srx << "," << sry << " 0 0 1 " << scx << "," << top;
        } else if (portion == "tr") {
            // top-right quarter
            path << "M" << scx << "," << top << " A" << srx << "," << sry << " 0 0 1 " << right << "," << scy;
        } else if (portion == "bl") {
            // bottom-left quarter
            path << "M" << scx << "," << bottom << " A" << srx << "," << sry << " 0 0 1 " << left << "," << scy;
        } else if (portion == "br") {
            // bottom-right quarter
            path << "M" << right << "," << scy << " A" << srx << "," << sry << " 0 0 1 " << scx << "," << bottom;
        } else {
            // unknown portion, draw full oval
            svg << "<ellipse cx=\"" << scx << "\" cy=\"" << scy
                << "\" rx=\"" << srx << "\" ry=\"" << sry
                << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"" << thickness << "px\"/>";
            return svg.str();
        }
        
        svg << "<path d=\"" << path.str() << "\" fill=\"none\" stroke=\"#000000\" stroke-width=\"" << thickness << "px\"/>";
    }
    
    return svg.str();
}

void PictureRenderer::line(double slope_x, double slope_y, double length) {
    PictureCoord end = slopeLengthToEnd(slope_x, slope_y, length);
    
    log_debug("line: slope=(%.2f,%.2f) length=%.2f -> end=(%.2f,%.2f)",
              slope_x, slope_y, length, end.x, end.y);
    
    ctx_.svg_elements.push_back(svgLine(current_x_, current_y_, 
                                         current_x_ + end.x, current_y_ + end.y));
}

void PictureRenderer::vector(double slope_x, double slope_y, double length) {
    PictureCoord end = slopeLengthToEnd(slope_x, slope_y, length);
    
    log_debug("vector: slope=(%.2f,%.2f) length=%.2f -> end=(%.2f,%.2f)",
              slope_x, slope_y, length, end.x, end.y);
    
    ctx_.svg_elements.push_back(svgVector(current_x_, current_y_,
                                           current_x_ + end.x, current_y_ + end.y));
}

void PictureRenderer::circle(double diameter, bool filled) {
    double radius = diameter / 2.0;
    
    log_debug("circle: diameter=%.2f filled=%d at (%.2f,%.2f)", 
              diameter, filled, current_x_, current_y_);
    
    ctx_.svg_elements.push_back(svgCircle(current_x_, current_y_, radius, filled));
}

void PictureRenderer::oval(double width, double height, const std::string& portion) {
    double rx = width / 2.0;
    double ry = height / 2.0;
    
    log_debug("oval: size=(%.2f,%.2f) portion='%s' at (%.2f,%.2f)",
              width, height, portion.c_str(), current_x_, current_y_);
    
    ctx_.svg_elements.push_back(svgOval(current_x_, current_y_, rx, ry, portion));
}

void PictureRenderer::qbezier(double x1, double y1, double cx, double cy, double x2, double y2, int n) {
    // convert picture coordinates to SVG coordinates
    double sx1 = ctx_.toPx(x1 - ctx_.x_offset);
    double sy1 = ctx_.toPx(y1 - ctx_.y_offset);
    double scx = ctx_.toPx(cx - ctx_.x_offset);
    double scy = ctx_.toPx(cy - ctx_.y_offset);
    double sx2 = ctx_.toPx(x2 - ctx_.x_offset);
    double sy2 = ctx_.toPx(y2 - ctx_.y_offset);
    
    std::ostringstream path;
    path << std::fixed << std::setprecision(3);
    path << "M" << sx1 << "," << sy1 << " Q" << scx << "," << scy << " " << sx2 << "," << sy2;
    
    log_debug("qbezier: (%f,%f) - (%f,%f) - (%f,%f)", x1, y1, cx, cy, x2, y2);
    
    ctx_.svg_elements.push_back(svgPath(path.str(), false));
}

void PictureRenderer::multiput(double x, double y, double dx, double dy, int n, const std::string& obj) {
    log_debug("multiput: start=(%.2f,%.2f) delta=(%.2f,%.2f) n=%d", x, y, dx, dy, n);
    
    for (int i = 0; i < n; i++) {
        double px = x + i * dx;
        double py = y + i * dy;
        put(px, py, obj);
    }
}

void PictureRenderer::thicklines() {
    ctx_.setThickLines();
}

void PictureRenderer::thinlines() {
    ctx_.setThinLines();
}

void PictureRenderer::linethickness(double pt) {
    ctx_.setLineThickness(pt);
}

// =============================================================================
// Parsing helpers
// =============================================================================

bool parsePictureCoord(const char* str, double* x, double* y) {
    if (!str || !x || !y) return false;
    
    // skip leading whitespace and opening paren
    while (*str && (*str == ' ' || *str == '\t')) str++;
    if (*str != '(') return false;
    str++;
    
    // parse x
    char* end;
    *x = strtod(str, &end);
    if (end == str) return false;
    str = end;
    
    // skip comma
    while (*str && (*str == ' ' || *str == '\t')) str++;
    if (*str != ',') return false;
    str++;
    
    // parse y
    *y = strtod(str, &end);
    if (end == str) return false;
    str = end;
    
    // skip closing paren
    while (*str && (*str == ' ' || *str == '\t')) str++;
    if (*str != ')') return false;
    
    return true;
}

bool parsePictureSize(const char* str, double* width, double* height, 
                      double* x_off, double* y_off) {
    if (!str || !width || !height) return false;
    
    // first coord pair is size
    if (!parsePictureCoord(str, width, height)) {
        return false;
    }
    
    // optional second coord pair is offset
    if (x_off && y_off) {
        // find the second opening paren
        const char* second = strchr(str, ')');
        if (second) {
            second++;
            while (*second && (*second == ' ' || *second == '\t')) second++;
            if (*second == '(') {
                parsePictureCoord(second, x_off, y_off);
            } else {
                *x_off = 0;
                *y_off = 0;
            }
        }
    }
    
    return true;
}

} // namespace lambda
