// Lambda Charting Library Demo
// A comprehensive charting library for transforming Mark notation into SVG charts
// Supports bar charts, line charts, pie charts, and scatter plots

// ========== Core SVG Utilities ==========

// Generate SVG header with viewBox and styling
pub fn svg_header(width, height, title) {
    "<svg width=\"" ++ string(width) ++ "\" height=\"" ++ string(height) ++
    "\" viewBox=\"0 0 " ++ string(width) ++ " " ++ string(height) ++
    "\" xmlns=\"http://www.w3.org/2000/svg\">" ++
    if (title != null)
        "<title>" ++ title ++ "</title>"
    else ""
}

// Close SVG tag
pub fn svg_footer() {
    "</svg>"
}

// Generate SVG text element
pub fn svg_text(x, y, text, style) {
    let style_attr = if (style != null)
        " style=\"" ++ style ++ "\""
    else "";
    "<text x=\"" ++ string(x) ++ "\" y=\"" ++ string(y) ++ "\"" ++ style_attr ++ ">" ++
    text ++ "</text>"
}

// Generate SVG rectangle
pub fn svg_rect(x, y, width, height, fill, stroke) {
    let fill_attr = if (fill != null) " fill=\"" ++ fill ++ "\"" else "";
    let stroke_attr = if (stroke != null) " stroke=\"" ++ stroke ++ "\"" else "";
    "<rect x=\"" ++ string(x) ++ "\" y=\"" ++ string(y) ++
    "\" width=\"" ++ string(width) ++ "\" height=\"" ++ string(height) ++ "\"" ++
    fill_attr ++ stroke_attr ++ "/>"
}

// Generate SVG circle
pub fn svg_circle(cx, cy, r, fill, stroke) {
    let fill_attr = if (fill != null) " fill=\"" ++ fill ++ "\"" else "";
    let stroke_attr = if (stroke != null) " stroke=\"" ++ stroke ++ "\"" else "";
    "<circle cx=\"" ++ string(cx) ++ "\" cy=\"" ++ string(cy) ++
    "\" r=\"" ++ string(r) ++ "\"" ++ fill_attr ++ stroke_attr ++ "/>"
}

// Generate SVG line
pub fn svg_line(x1, y1, x2, y2, stroke, stroke_width) {
    let stroke_attr = if (stroke != null) " stroke=\"" ++ stroke ++ "\"" else "";
    let width_attr = if (stroke_width != null) " stroke-width=\"" ++ string(stroke_width) ++ "\"" else "";
    "<line x1=\"" ++ string(x1) ++ "\" y1=\"" ++ string(y1) ++
    "\" x2=\"" ++ string(x2) ++ "\" y2=\"" ++ string(y2) ++ "\"" ++
    stroke_attr ++ width_attr ++ "/>"
}

// Generate SVG path for complex shapes
pub fn svg_path(d, fill, stroke, stroke_width) {
    let fill_attr = if (fill != null) " fill=\"" ++ fill ++ "\"" else "";
    let stroke_attr = if (stroke != null) " stroke=\"" ++ stroke ++ "\"" else "";
    let width_attr = if (stroke_width != null) " stroke-width=\"" ++ string(stroke_width) ++ "\"" else "";
    "<path d=\"" ++ d ++ "\"" ++ fill_attr ++ stroke_attr ++ width_attr ++ "/>"
}

// ========== Data Processing Utilities ==========

// Extract numeric values from various data formats
pub fn extract_value(item) {
    if (type(item) == 'map') (
        if (item.value != null) item.value
        else if (item.y != null) item.y
        else if (item.amount != null) item.amount
        else if (item.count != null) item.count
        else 0
    )
    else if (type(item) == 'int' or type(item) == 'float') item
    else 0
}

// Extract label from data item
pub fn extract_label(item) {
    if (type(item) == 'map') (
        if (item.label != null) string(item.label)
        else if (item.name != null) string(item.name)
        else if (item.x != null) string(item.x)
        else if (item.category != null) string(item.category)
        else "Item"
    )
    else string(item)
}

// Calculate data ranges for scaling
pub fn calculate_ranges(data) {
    let values = for (item in data) extract_value(item);
    {
        min_value: min(values),
        max_value: max(values),
        range: max(values) - min(values),
        data_count: len(data)
    }
}

// Scale value to fit within chart bounds
pub fn scale_value(value, min_val, max_val, min_output, max_output) {
    if (max_val == min_val) min_output
    else min_output + ((value - min_val) / (max_val - min_val)) * (max_output - min_output)
}

// ========== Color Schemes ==========

// Default color palette
let default_colors = [
    "#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd",
    "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf",
    "#aec7e8", "#ffbb78", "#98df8a", "#ff9896", "#c5b0d5"
];

// Get color by index with cycling
pub fn get_color(index, colors) {
    let color_palette = if (colors != null) colors else default_colors;
    color_palette[index % len(color_palette)]
}

// Generate gradient colors
pub fn generate_gradient_colors(count, start_color, end_color) {
    // Simplified: just return default colors for now
    for (i in 0 to count-1) get_color(i, default_colors)
}

// ========== Chart Type Implementations ==========

// Generate Bar Chart SVG
pub fn generate_bar_chart(chart_config) {
    let data = chart_config.data;
    let title = if (chart_config.title != null) chart_config.title else "Bar Chart";
    let width = if (chart_config.width != null) chart_config.width else 800;
    let height = if (chart_config.height != null) chart_config.height else 600;
    let colors = chart_config.colors;

    // Chart margins and dimensions
    let margin = {top: 50, right: 50, bottom: 80, left: 80};
    let chart_width = width - margin.left - margin.right;
    let chart_height = height - margin.top - margin.bottom;

    // Calculate data ranges
    let ranges = calculate_ranges(data);
    let bar_width = chart_width / ranges.data_count;

    // Generate SVG elements
    let svg_start = svg_header(width, height, title);

    // Title
    let title_svg = svg_text(width/2, 30, title, "text-anchor: middle; font-size: 20px; font-weight: bold;");

    // Draw axes
    let x_axis = svg_line(margin.left, height - margin.bottom, width - margin.right, height - margin.bottom, "#333", 2);
    let y_axis = svg_line(margin.left, margin.top, margin.left, height - margin.bottom, "#333", 2);

    // Generate bars
    let bars = for (i in 0 to len(data)-1) (
        let item = data[i];
        let value = extract_value(item);
        let label = extract_label(item);
        let bar_height = scale_value(value, 0, ranges.max_value, 0, chart_height);
        let x = margin.left + i * bar_width + bar_width * 0.1;
        let y = height - margin.bottom - bar_height;
        let bar_color = get_color(i, colors);

        // Bar rectangle
        let bar_rect = svg_rect(x, y, bar_width * 0.8, bar_height, bar_color, "#333");

        // Value label on top of bar
        let value_label = svg_text(x + bar_width * 0.4, y - 5, string(value),
            "text-anchor: middle; font-size: 12px;");

        // Category label on x-axis
        let category_label = svg_text(x + bar_width * 0.4, height - margin.bottom + 20, label,
            "text-anchor: middle; font-size: 11px;");

        bar_rect ++ value_label ++ category_label
    );

    // Y-axis labels
    let y_labels = for (i in 0 to 5) (
        let value = (ranges.max_value / 5) * i;
        let y = height - margin.bottom - (chart_height / 5) * i;
        let tick_line = svg_line(margin.left - 5, y, margin.left, y, "#333", 1);
        let label_text = svg_text(margin.left - 10, y + 4, string(int(value)),
            "text-anchor: end; font-size: 10px;");
        tick_line ++ label_text
    );

    let svg_end = svg_footer();

    svg_start ++ title_svg ++ x_axis ++ y_axis ++
    (for (bar in bars) bar) ++ (for (label in y_labels) label) ++ svg_end
}

// Generate Line Chart SVG
pub fn generate_line_chart(chart_config) {
    let data = chart_config.data;
    let title = if (chart_config.title != null) chart_config.title else "Line Chart";
    let width = if (chart_config.width != null) chart_config.width else 800;
    let height = if (chart_config.height != null) chart_config.height else 600;
    let colors = chart_config.colors;

    let margin = {top: 50, right: 50, bottom: 80, left: 80};
    let chart_width = width - margin.left - margin.right;
    let chart_height = height - margin.top - margin.bottom;

    let ranges = calculate_ranges(data);

    let svg_start = svg_header(width, height, title);
    let title_svg = svg_text(width/2, 30, title, "text-anchor: middle; font-size: 20px; font-weight: bold;");

    // Axes
    let x_axis = svg_line(margin.left, height - margin.bottom, width - margin.right, height - margin.bottom, "#333", 2);
    let y_axis = svg_line(margin.left, margin.top, margin.left, height - margin.bottom, "#333", 2);

    // Generate line path
    let points = for (i in 0 to len(data)-1) (
        let value = extract_value(data[i]);
        let x = margin.left + (i / (len(data) - 1)) * chart_width;
        let y = height - margin.bottom - scale_value(value, 0, ranges.max_value, 0, chart_height);
        {x: x, y: y, value: value}
    );

    // Create path string
    let path_data = (
        let start_point = points[0];
        let path_start = "M " ++ string(start_point.x) ++ " " ++ string(start_point.y);
        let path_lines = for (i in 1 to len(points)-1) (
            let point = points[i];
            " L " ++ string(point.x) ++ " " ++ string(point.y)
        );
        path_start ++ (for (line in path_lines) line)
    );

    let line_path = svg_path(path_data, "none", get_color(0, colors), 2);

    // Data points
    let data_points = for (i in 0 to len(points)-1) (
        let point = points[i];
        svg_circle(point.x, point.y, 4, get_color(0, colors), "#fff")
    );

    // Labels
    let labels = for (i in 0 to len(data)-1) (
        let label = extract_label(data[i]);
        let point = points[i];
        svg_text(point.x, height - margin.bottom + 20, label, "text-anchor: middle; font-size: 11px;")
    );

    let svg_end = svg_footer();

    svg_start ++ title_svg ++ x_axis ++ y_axis ++ line_path ++
    (for (point in data_points) point) ++ (for (label in labels) label) ++ svg_end
}

// Generate Pie Chart SVG
pub fn generate_pie_chart(chart_config) {
    let data = chart_config.data;
    let title = if (chart_config.title != null) chart_config.title else "Pie Chart";
    let width = if (chart_config.width != null) chart_config.width else 600;
    let height = if (chart_config.height != null) chart_config.height else 600;
    let colors = chart_config.colors;

    let center_x = width / 2;
    let center_y = height / 2;
    let radius = min([width, height]) / 3;

    let total = sum(for (item in data) extract_value(item));

    let svg_start = svg_header(width, height, title);
    let title_svg = svg_text(center_x, 30, title, "text-anchor: middle; font-size: 20px; font-weight: bold;");

    // Generate pie slices
    let current_angle = 0;
    let slices = for (i in 0 to len(data)-1) (
        let item = data[i];
        let value = extract_value(item);
        let label = extract_label(item);
        let slice_angle = (value / total) * 2 * 3.14159;

        let start_x = center_x + radius * cos(current_angle);
        let start_y = center_y + radius * sin(current_angle);
        let end_x = center_x + radius * cos(current_angle + slice_angle);
        let end_y = center_y + radius * sin(current_angle + slice_angle);

        let large_arc = if (slice_angle > 3.14159) 1 else 0;

        let path_data = "M " ++ string(center_x) ++ " " ++ string(center_y) ++
                       " L " ++ string(start_x) ++ " " ++ string(start_y) ++
                       " A " ++ string(radius) ++ " " ++ string(radius) ++ " 0 " ++
                       string(large_arc) ++ " 1 " ++ string(end_x) ++ " " ++ string(end_y) ++ " Z";

        let slice_color = get_color(i, colors);
        let slice_path = svg_path(path_data, slice_color, "#fff", 2);

        // Label positioning
        let label_angle = current_angle + slice_angle / 2;
        let label_x = center_x + (radius + 30) * cos(label_angle);
        let label_y = center_y + (radius + 30) * sin(label_angle);
        let label_text = svg_text(label_x, label_y, label ++ " (" ++ string(value) ++ ")",
            "text-anchor: middle; font-size: 12px;");

        current_angle = current_angle + slice_angle;
        slice_path ++ label_text
    );

    let svg_end = svg_footer();

    svg_start ++ title_svg ++ (for (slice in slices) slice) ++ svg_end
}

// Generate Scatter Plot SVG
pub fn generate_scatter_plot(chart_config) {
    let data = chart_config.data;
    let title = if (chart_config.title != null) chart_config.title else "Scatter Plot";
    let width = if (chart_config.width != null) chart_config.width else 800;
    let height = if (chart_config.height != null) chart_config.height else 600;
    let colors = chart_config.colors;

    let margin = {top: 50, right: 50, bottom: 80, left: 80};
    let chart_width = width - margin.left - margin.right;
    let chart_height = height - margin.top - margin.bottom;

    // Extract x and y values
    let x_values = for (item in data) (
        if (type(item) == 'map' and item.x != null) item.x else 0
    );
    let y_values = for (item in data) extract_value(item);

    let x_range = {min: min(x_values), max: max(x_values)};
    let y_range = {min: min(y_values), max: max(y_values)};

    let svg_start = svg_header(width, height, title);
    let title_svg = svg_text(width/2, 30, title, "text-anchor: middle; font-size: 20px; font-weight: bold;");

    // Axes
    let x_axis = svg_line(margin.left, height - margin.bottom, width - margin.right, height - margin.bottom, "#333", 2);
    let y_axis = svg_line(margin.left, margin.top, margin.left, height - margin.bottom, "#333", 2);

    // Data points
    let points = for (i in 0 to len(data)-1) (
        let item = data[i];
        let x_val = if (type(item) == 'map' and item.x != null) item.x else i;
        let y_val = extract_value(item);

        let x = margin.left + scale_value(x_val, x_range.min, x_range.max, 0, chart_width);
        let y = height - margin.bottom - scale_value(y_val, y_range.min, y_range.max, 0, chart_height);

        svg_circle(x, y, 5, get_color(i, colors), "#333")
    );

    let svg_end = svg_footer();

    svg_start ++ title_svg ++ x_axis ++ y_axis ++ (for (point in points) point) ++ svg_end
}

// ========== Main Chart Generation Function ==========

// Parse chart element and generate appropriate SVG
pub fn generate_chart(chart_element) {
    // Extract chart configuration from element attributes and content
    let chart_type = if (chart_element.type != null) chart_element.type else 'bar';
    let chart_data = if (chart_element.data != null) chart_element.data else [];
    let chart_title = if (chart_element.title != null) chart_element.title else null;
    let chart_width = if (chart_element.width != null) chart_element.width else null;
    let chart_height = if (chart_element.height != null) chart_element.height else null;
    let chart_colors = if (chart_element.colors != null) chart_element.colors else null;

    let config = {
        data: chart_data,
        title: chart_title,
        width: chart_width,
        height: chart_height,
        colors: chart_colors
    };

    if (chart_type == 'bar') generate_bar_chart(config)
    else if (chart_type == 'line') generate_line_chart(config)
    else if (chart_type == 'pie') generate_pie_chart(config)
    else if (chart_type == 'scatter') generate_scatter_plot(config)
    else generate_bar_chart(config)  // default fallback
}

// ========== Demo Examples ==========

"=== Lambda Charting Library Demo ==="

// Example 1: Simple Bar Chart using Mark notation
let sales_chart = <chart
    type: 'bar',
    title: "Q4 Sales Performance",
    width: 800,
    height: 600,
    data: [
        {label: "Product A", value: 120},
        {label: "Product B", value: 98},
        {label: "Product C", value: 86},
        {label: "Product D", value: 140},
        {label: "Product E", value: 73}
    ]>

"Generating Bar Chart SVG..."
let bar_chart_svg = generate_chart(sales_chart);

"First 200 characters of Bar Chart SVG:"
bar_chart_svg[0 to min([200, len(bar_chart_svg)])]

// Example 2: Line Chart for time series data
let performance_chart = <chart
    type: 'line',
    title: "Website Traffic Over Time",
    width: 900,
    height: 500,
    data: [
        {label: "Jan", value: 1200},
        {label: "Feb", value: 1350},
        {label: "Mar", value: 1180},
        {label: "Apr", value: 1420},
        {label: "May", value: 1680},
        {label: "Jun", value: 1750}
    ]>

"Generating Line Chart SVG..."
let line_chart_svg = generate_chart(performance_chart);

"Line Chart Generated - Length: " ++ string(len(line_chart_svg)) ++ " characters"

// Example 3: Pie Chart for market share
let market_share_chart = <chart
    type: 'pie',
    title: "Market Share Analysis",
    width: 600,
    height: 600,
    data: [
        {label: "Company A", value: 35},
        {label: "Company B", value: 28},
        {label: "Company C", value: 22},
        {label: "Company D", value: 15}
    ]>

"Generating Pie Chart SVG..."
let pie_chart_svg = generate_chart(market_share_chart);

"Pie Chart Generated - Length: " ++ string(len(pie_chart_svg)) ++ " characters"

// Example 4: Scatter Plot
let correlation_chart = <chart
    type: 'scatter',
    title: "Price vs Sales Correlation",
    width: 800,
    height: 600,
    data: [
        {x: 10, y: 150, label: "Item 1"},
        {x: 15, y: 120, label: "Item 2"},
        {x: 20, y: 95, label: "Item 3"},
        {x: 25, y: 80, label: "Item 4"},
        {x: 30, y: 60, label: "Item 5"}
    ]>

"Generating Scatter Plot SVG..."
let scatter_chart_svg = generate_chart(correlation_chart);

"Scatter Plot Generated - Length: " ++ string(len(scatter_chart_svg)) ++ " characters"

// Example 5: Complex nested chart configuration
let dashboard_chart = <chart
    type: 'bar',
    title: "Revenue Dashboard",
    width: 1000,
    height: 700,
    colors: ["#FF6B6B", "#4ECDC4", "#45B7D1", "#FFA07A", "#98D8C8"],
    data: [
        {category: "Q1 2024", amount: 125000},
        {category: "Q2 2024", amount: 142000},
        {category: "Q3 2024", amount: 118000},
        {category: "Q4 2024", amount: 165000}
    ]>

"Generating Dashboard Chart SVG..."
let dashboard_svg = generate_chart(dashboard_chart);

"Dashboard Chart Generated - Length: " ++ string(len(dashboard_svg)) ++ " characters"

// Summary of generated charts
{
    charts_generated: 5,
    chart_types: ["bar", "line", "pie", "scatter"],
    total_svg_length: len(bar_chart_svg) + len(line_chart_svg) + len(pie_chart_svg) + len(scatter_chart_svg) + len(dashboard_svg),
    library_features: [
        "Mark notation parsing",
        "Multiple chart types",
        "Customizable styling",
        "SVG output generation",
        "Data scaling and positioning",
        "Color scheme support"
    ],
    sample_output: {
        bar_chart_preview: bar_chart_svg[0 to min([100, len(bar_chart_svg)])],
        chart_configurations_used: 5
    }
}
