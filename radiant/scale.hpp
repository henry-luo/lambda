#pragma once

#include <math.h>
#include <stdbool.h>

// RSC8 coordinate-domain types. Logical values remain float through layout and
// interaction; device and host values exist only at their respective adapters.
typedef struct RdtLogicalPoint {
    float x;
    float y;
} RdtLogicalPoint;

typedef struct RdtLogicalRect {
    float x;
    float y;
    float width;
    float height;
} RdtLogicalRect;

typedef struct RdtDevicePoint {
    float x;
    float y;
} RdtDevicePoint;

typedef struct RdtDeviceRect {
    float x;
    float y;
    float width;
    float height;
} RdtDeviceRect;

typedef struct RdtDevicePixelPoint {
    int x;  // INT_CAST_OK: discrete surface pixel address
    int y;  // INT_CAST_OK: discrete surface pixel address
} RdtDevicePixelPoint;

typedef struct RdtDevicePixelSize {
    int width;   // INT_CAST_OK: discrete surface pixel dimension
    int height;  // INT_CAST_OK: discrete surface pixel dimension
} RdtDevicePixelSize;

typedef enum RdtHostYAxis {
    RDT_HOST_Y_DOWN,
    RDT_HOST_Y_UP,
} RdtHostYAxis;

typedef struct RdtHostWindowMetrics {
    float units_per_logical_x;
    float units_per_logical_y;
    float content_height;
    RdtHostYAxis y_axis;
} RdtHostWindowMetrics;

typedef struct RdtHostRect {
    float x;
    float y;
    float width;
    float height;
} RdtHostRect;

inline float rdt_valid_scale(float scale) {
    return scale > 0.0f ? scale : 1.0f;
}

inline RdtLogicalPoint rdt_platform_to_logical_point(
        float platform_x, float platform_y,
        float platform_units_per_logical_x,
        float platform_units_per_logical_y) {
    return {
        platform_x / rdt_valid_scale(platform_units_per_logical_x),
        platform_y / rdt_valid_scale(platform_units_per_logical_y),
    };
}

inline RdtDevicePoint rdt_logical_to_device_point(
        RdtLogicalPoint logical, float scale_x, float scale_y) {
    return {
        logical.x * rdt_valid_scale(scale_x),
        logical.y * rdt_valid_scale(scale_y),
    };
}

inline RdtDeviceRect rdt_logical_to_device_rect(
        RdtLogicalRect logical, float scale_x, float scale_y) {
    return {
        logical.x * rdt_valid_scale(scale_x),
        logical.y * rdt_valid_scale(scale_y),
        logical.width * rdt_valid_scale(scale_x),
        logical.height * rdt_valid_scale(scale_y),
    };
}

inline RdtDevicePixelPoint rdt_device_point_round(RdtDevicePoint device) {
    return {
        (int)lroundf(device.x),  // INT_CAST_OK: discrete surface pixel address
        (int)lroundf(device.y),  // INT_CAST_OK: discrete surface pixel address
    };
}

inline RdtDevicePixelSize rdt_device_rect_pixel_extent(RdtDeviceRect device) {
    return {
        (int)ceilf(device.width > 0.0f ? device.width : 0.0f),   // INT_CAST_OK: containing pixel extent
        (int)ceilf(device.height > 0.0f ? device.height : 0.0f), // INT_CAST_OK: containing pixel extent
    };
}

inline RdtHostRect rdt_logical_to_host_rect(
        RdtLogicalRect logical, RdtHostWindowMetrics metrics) {
    float scale_x = rdt_valid_scale(metrics.units_per_logical_x);
    float scale_y = rdt_valid_scale(metrics.units_per_logical_y);
    RdtHostRect host = {
        logical.x * scale_x,
        logical.y * scale_y,
        logical.width * scale_x,
        logical.height * scale_y,
    };
    if (metrics.y_axis == RDT_HOST_Y_UP) {
        host.y = metrics.content_height - host.y - host.height;
    }
    return host;
}
