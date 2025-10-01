// Bar Chart Examples - Perfect for comparing categorical data
// Import the charting library functions
import 'chart'

"=== Bar Chart Demonstrations ==="

// Example 1: Simple Sales Performance Bar Chart
let sales_performance = <chart
    type: 'bar',
    title: "Q4 2024 Sales Performance by Product",
    width: 900,
    height: 600,
    colors: ["#3498db", "#e74c3c", "#2ecc71", "#f39c12", "#9b59b6"],
    data: [
        {label: "Laptops", value: 156},
        {label: "Smartphones", value: 243},
        {label: "Tablets", value: 89},
        {label: "Accessories", value: 178},
        {label: "Monitors", value: 134}
    ]>

"Generating Sales Performance Bar Chart..."
let sales_svg = generate_chart(sales_performance);

// Example 2: Regional Revenue Comparison
let regional_revenue = <chart
    type: 'bar',
    title: "Regional Revenue Comparison (in thousands USD)",
    width: 1000,
    height: 650,
    colors: ["#1abc9c", "#3498db", "#9b59b6", "#e67e22", "#e74c3c", "#f1c40f"],
    data: [
        {category: "North America", amount: 2450},
        {category: "Europe", amount: 1890},
        {category: "Asia Pacific", amount: 2780},
        {category: "Latin America", amount: 1240},
        {category: "Middle East", amount: 890},
        {category: "Africa", amount: 560}
    ]>

"Generating Regional Revenue Bar Chart..."
let revenue_svg = generate_chart(regional_revenue);

// Example 3: Employee Satisfaction Survey Results
let satisfaction_survey = <chart
    type: 'bar',
    title: "Employee Satisfaction Survey Results (%)",
    width: 800,
    height: 550,
    colors: ["#27ae60", "#2ecc71", "#f39c12", "#e67e22", "#e74c3c"],
    data: [
        {label: "Very Satisfied", value: 35},
        {label: "Satisfied", value: 42},
        {label: "Neutral", value: 15},
        {label: "Dissatisfied", value: 6},
        {label: "Very Dissatisfied", value: 2}
    ]>

"Generating Employee Satisfaction Bar Chart..."
let satisfaction_svg = generate_chart(satisfaction_survey);

// Example 4: Monthly Website Traffic
let monthly_traffic = <chart
    type: 'bar',
    title: "Monthly Website Traffic (Unique Visitors)",
    width: 1100,
    height: 600,
    colors: ["#3498db", "#5dade2", "#85c1e9", "#aed6f1", "#d6eaf8",
             "#2980b9", "#1f618d", "#154360", "#0e2f44", "#081b2b",
             "#34495e", "#2c3e50"],
    data: [
        {label: "Jan", value: 45000},
        {label: "Feb", value: 52000},
        {label: "Mar", value: 48000},
        {label: "Apr", value: 61000},
        {label: "May", value: 58000},
        {label: "Jun", value: 67000},
        {label: "Jul", value: 72000},
        {label: "Aug", value: 69000},
        {label: "Sep", value: 71000},
        {label: "Oct", value: 78000},
        {label: "Nov", value: 85000},
        {label: "Dec", value: 92000}
    ]>

"Generating Monthly Traffic Bar Chart..."
let traffic_svg = generate_chart(monthly_traffic);

// Example 5: Technology Stack Usage in Development Team
let tech_stack = <chart
    type: 'bar',
    title: "Technology Stack Usage (Number of Projects)",
    width: 850,
    height: 580,
    colors: ["#ff6b6b", "#4ecdc4", "#45b7d1", "#96ceb4", "#feca57", "#ff9ff3", "#54a0ff"],
    data: [
        {label: "JavaScript", value: 28},
        {label: "Python", value: 24},
        {label: "Java", value: 19},
        {label: "C++", value: 15},
        {label: "Go", value: 12},
        {label: "Rust", value: 8},
        {label: "Lambda Script", value: 5}
    ]>

"Generating Technology Stack Bar Chart..."
let tech_svg = generate_chart(tech_stack);

// Summary and output information
{
    chart_type: "Bar Charts",
    description: "Perfect for comparing categorical data across different groups",
    examples_created: 5,
    use_cases: [
        "Sales performance comparison",
        "Regional revenue analysis",
        "Survey result visualization",
        "Time-based discrete data",
        "Technology adoption metrics"
    ],
    chart_details: {
        sales_chart: {
            title: "Q4 2024 Sales Performance",
            data_points: len(sales_performance.data),
            svg_length: len(sales_svg)
        },
        revenue_chart: {
            title: "Regional Revenue Comparison",
            data_points: len(regional_revenue.data),
            svg_length: len(revenue_svg)
        },
        satisfaction_chart: {
            title: "Employee Satisfaction Survey",
            data_points: len(satisfaction_survey.data),
            svg_length: len(satisfaction_svg)
        },
        traffic_chart: {
            title: "Monthly Website Traffic",
            data_points: len(monthly_traffic.data),
            svg_length: len(traffic_svg)
        },
        tech_chart: {
            title: "Technology Stack Usage",
            data_points: len(tech_stack.data),
            svg_length: len(tech_svg)
        }
    },
    total_svg_output: len(sales_svg) + len(revenue_svg) + len(satisfaction_svg) + len(traffic_svg) + len(tech_svg),
    sample_svg_preview: sales_svg[0 to min([150, len(sales_svg)])]
}
