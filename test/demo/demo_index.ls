// Lambda Charting Library - Complete Demo Suite
// Comprehensive examples of all chart types with real-world scenarios

"=== Lambda Charting Library - Complete Demo Suite ==="

import 'chart'

// Quick reference for all chart types
let chart_type_overview = {
    supported_types: ["bar", "line", "pie", "scatter"],
    library_features: [
        "Mark notation integration",
        "SVG output generation",
        "Customizable styling",
        "Multiple data formats",
        "Responsive design support"
    ],
    demo_files: [
        "bar_charts.ls - Categorical data comparison",
        "line_charts.ls - Time series and trends",
        "pie_charts.ls - Proportional analysis",
        "scatter_plots.ls - Correlation studies"
    ]
}

"Chart Type Overview:"
chart_type_overview

// ========== Quick Demo Examples ==========

// 1. Simple Bar Chart - Sales Data
let quick_bar_demo = <chart
    type: 'bar',
    title: "Quick Demo - Monthly Sales",
    data: [
        {label: "Jan", value: 85000},
        {label: "Feb", value: 92000},
        {label: "Mar", value: 78000},
        {label: "Apr", value: 105000}
    ]>

// 2. Simple Line Chart - Growth Trend
let quick_line_demo = <chart
    type: 'line',
    title: "Quick Demo - User Growth",
    data: [
        {label: "Week 1", value: 1200},
        {label: "Week 2", value: 1450},
        {label: "Week 3", value: 1680},
        {label: "Week 4", value: 1920}
    ]>

// 3. Simple Pie Chart - Market Share
let quick_pie_demo = <chart
    type: 'pie',
    title: "Quick Demo - Browser Usage",
    data: [
        {label: "Chrome", value: 65},
        {label: "Firefox", value: 18},
        {label: "Safari", value: 12},
        {label: "Others", value: 5}
    ]>

// 4. Simple Scatter Plot - Correlation
let quick_scatter_demo = <chart
    type: 'scatter',
    title: "Quick Demo - Price vs Demand",
    data: [
        {x: 10, y: 100, label: "Low Price"},
        {x: 20, y: 80, label: "Med Price"},
        {x: 30, y: 60, label: "High Price"},
        {x: 40, y: 40, label: "Premium Price"}
    ]>

"Generating Quick Demo Charts..."

let bar_demo_svg = generate_chart(quick_bar_demo);
let line_demo_svg = generate_chart(quick_line_demo);
let pie_demo_svg = generate_chart(quick_pie_demo);
let scatter_demo_svg = generate_chart(quick_scatter_demo);

// ========== Advanced Configuration Examples ==========

// Advanced Bar Chart with Custom Styling
let advanced_bar = <chart
    type: 'bar',
    title: "Advanced Configuration - Revenue by Quarter",
    width: 1000,
    height: 700,
    colors: ["#2c3e50", "#3498db", "#e74c3c", "#2ecc71"],
    data: [
        {category: "Q1 2024", amount: 2450000, growth: "+12%"},
        {category: "Q2 2024", amount: 2780000, growth: "+18%"},
        {category: "Q3 2024", amount: 2650000, growth: "+8%"},
        {category: "Q4 2024", amount: 3120000, growth: "+25%"}
    ]>

// Advanced Line Chart with Multiple Data Points
let advanced_line = <chart
    type: 'line',
    title: "Advanced Configuration - System Performance Metrics",
    width: 1200,
    height: 600,
    colors: ["#e74c3c"],
    data: [
        {label: "00:00", value: 15.2},
        {label: "04:00", value: 12.8},
        {label: "08:00", value: 28.5},
        {label: "12:00", value: 45.7},
        {label: "16:00", value: 52.3},
        {label: "20:00", value: 38.9}
    ]>

// Advanced Pie Chart with Detailed Segments
let advanced_pie = <chart
    type: 'pie',
    title: "Advanced Configuration - Resource Allocation",
    width: 800,
    height: 800,
    colors: ["#1abc9c", "#3498db", "#9b59b6", "#f39c12", "#e74c3c", "#95a5a6"],
    data: [
        {label: "Development", value: 40},
        {label: "Testing", value: 25},
        {label: "Infrastructure", value: 15},
        {label: "Design", value: 10},
        {label: "Marketing", value: 7},
        {label: "Other", value: 3}
    ]>

// Advanced Scatter Plot with Rich Data
let advanced_scatter = <chart
    type: 'scatter',
    title: "Advanced Configuration - Performance Analysis",
    width: 900,
    height: 700,
    colors: ["#2980b9"],
    data: [
        {x: 1000, y: 95.5, label: "Server A"},
        {x: 1500, y: 92.3, label: "Server B"},
        {x: 2000, y: 88.7, label: "Server C"},
        {x: 2500, y: 85.2, label: "Server D"},
        {x: 3000, y: 81.8, label: "Server E"},
        {x: 3500, y: 78.4, label: "Server F"}
    ]>

"Generating Advanced Configuration Charts..."

let advanced_bar_svg = generate_chart(advanced_bar);
let advanced_line_svg = generate_chart(advanced_line);
let advanced_pie_svg = generate_chart(advanced_pie);
let advanced_scatter_svg = generate_chart(advanced_scatter);

// ========== Real-World Business Scenarios ==========

// Business Intelligence Dashboard Components
let dashboard_components = {

    // Sales Performance Chart
    sales_performance: <chart
        type: 'bar',
        title: "Sales Performance by Region",
        width: 800,
        height: 500,
        colors: ["#3498db", "#e74c3c", "#2ecc71", "#f39c12"],
        data: [
            {label: "North America", value: 3200000},
            {label: "Europe", value: 2800000},
            {label: "Asia Pacific", value: 3500000},
            {label: "Latin America", value: 1200000}
        ]>,

    // Customer Acquisition Trend
    acquisition_trend: <chart
        type: 'line',
        title: "Monthly Customer Acquisition",
        width: 900,
        height: 400,
        colors: ["#27ae60"],
        data: [
            {label: "Jan", value: 1250},
            {label: "Feb", value: 1380},
            {label: "Mar", value: 1520},
            {label: "Apr", value: 1690},
            {label: "May", value: 1840},
            {label: "Jun", value: 2010}
        ]>,

    // Market Share Distribution
    market_distribution: <chart
        type: 'pie',
        title: "Market Share by Product Line",
        width: 600,
        height: 600,
        colors: ["#e74c3c", "#3498db", "#2ecc71", "#f39c12", "#9b59b6"],
        data: [
            {label: "Product A", value: 35},
            {label: "Product B", value: 28},
            {label: "Product C", value: 20},
            {label: "Product D", value: 12},
            {label: "Product E", value: 5}
        ]>,

    // ROI Analysis
    roi_analysis: <chart
        type: 'scatter',
        title: "Marketing Campaign ROI Analysis",
        width: 800,
        height: 600,
        colors: ["#e67e22"],
        data: [
            {x: 50000, y: 125000, label: "Campaign A"},
            {x: 75000, y: 180000, label: "Campaign B"},
            {x: 100000, y: 220000, label: "Campaign C"},
            {x: 150000, y: 290000, label: "Campaign D"}
        ]>
};

"Generating Business Dashboard Components..."

let sales_perf_svg = generate_chart(dashboard_components.sales_performance);
let acquisition_svg = generate_chart(dashboard_components.acquisition_trend);
let market_dist_svg = generate_chart(dashboard_components.market_distribution);
let roi_analysis_svg = generate_chart(dashboard_components.roi_analysis);

// ========== Technical Documentation Examples ==========

let technical_examples = {

    // System Load Monitoring
    system_load: <chart
        type: 'line',
        title: "Server Load Average (24 Hour Period)",
        width: 1000,
        height: 400,
        data: for (hour in 0 to 23) {
            label: string(hour) ++ ":00",
            value: 20 + 30 * sin(hour * 0.26) + 10 * (hour % 3)
        }>,

    // Error Rate Distribution
    error_distribution: <chart
        type: 'pie',
        title: "Error Types Distribution",
        width: 500,
        height: 500,
        data: [
            {label: "404 Not Found", value: 45},
            {label: "500 Server Error", value: 25},
            {label: "403 Forbidden", value: 15},
            {label: "408 Timeout", value: 10},
            {label: "Others", value: 5}
        ]>,

    // Performance Benchmarks
    performance_bench: <chart
        type: 'bar',
        title: "Framework Performance Comparison (ops/sec)",
        width: 800,
        height: 600,
        data: [
            {label: "Framework A", value: 25000},
            {label: "Framework B", value: 18000},
            {label: "Framework C", value: 32000},
            {label: "Framework D", value: 21000},
            {label: "Lambda Script", value: 38000}
        ]>
};

"Generating Technical Documentation Charts..."

let system_svg = generate_chart(technical_examples.system_load);
let error_svg = generate_chart(technical_examples.error_distribution);
let benchmark_svg = generate_chart(technical_examples.performance_bench);

// ========== Final Summary and Statistics ==========

{
    demo_suite_summary: {
        total_charts_generated: 15,
        chart_type_breakdown: {
            bar_charts: 5,
            line_charts: 4,
            pie_charts: 3,
            scatter_plots: 3
        },
        use_case_categories: [
            "Business Intelligence",
            "Technical Monitoring",
            "Financial Analysis",
            "Performance Metrics",
            "Market Research"
        ]
    },

    svg_generation_stats: {
        quick_demos: {
            bar_chart_size: len(bar_demo_svg),
            line_chart_size: len(line_demo_svg),
            pie_chart_size: len(pie_demo_svg),
            scatter_chart_size: len(scatter_demo_svg)
        },
        advanced_configs: {
            bar_chart_size: len(advanced_bar_svg),
            line_chart_size: len(advanced_line_svg),
            pie_chart_size: len(advanced_pie_svg),
            scatter_chart_size: len(advanced_scatter_svg)
        },
        business_dashboards: {
            sales_chart_size: len(sales_perf_svg),
            acquisition_chart_size: len(acquisition_svg),
            market_chart_size: len(market_dist_svg),
            roi_chart_size: len(roi_analysis_svg)
        },
        technical_docs: {
            system_chart_size: len(system_svg),
            error_chart_size: len(error_svg),
            benchmark_chart_size: len(benchmark_svg)
        }
    },

    total_svg_output: len(bar_demo_svg) + len(line_demo_svg) + len(pie_demo_svg) + len(scatter_demo_svg) +
                     len(advanced_bar_svg) + len(advanced_line_svg) + len(advanced_pie_svg) + len(advanced_scatter_svg) +
                     len(sales_perf_svg) + len(acquisition_svg) + len(market_dist_svg) + len(roi_analysis_svg) +
                     len(system_svg) + len(error_svg) + len(benchmark_svg),

    library_capabilities: [
        "✅ Mark notation chart configuration",
        "✅ Multiple chart type support",
        "✅ Custom color schemes",
        "✅ Flexible data input formats",
        "✅ SVG output generation",
        "✅ Responsive sizing options",
        "✅ Professional styling",
        "✅ Real-world use case examples"
    ],

    sample_svg_output: bar_demo_svg[0 to min([200, len(bar_demo_svg)])],

    next_steps: [
        "Export SVG files for web integration",
        "Add interactive features with JavaScript",
        "Implement custom CSS styling",
        "Create animated chart transitions",
        "Build chart composition workflows"
    ]
}
