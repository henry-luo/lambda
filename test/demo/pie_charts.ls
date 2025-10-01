// Pie Chart Examples - Ideal for showing proportions and percentages
// Import the charting library functions
import 'chart'

"=== Pie Chart Demonstrations ==="

// Example 1: Market Share Analysis
let market_share = <chart
    type: 'pie',
    title: "Global Smartphone Market Share 2024",
    width: 700,
    height: 700,
    colors: ["#3498db", "#e74c3c", "#2ecc71", "#f39c12", "#9b59b6", "#1abc9c"],
    data: [
        {label: "Samsung", value: 28},
        {label: "Apple", value: 24},
        {label: "Xiaomi", value: 18},
        {label: "Oppo", value: 12},
        {label: "Vivo", value: 10},
        {label: "Others", value: 8}
    ]>

"Generating Market Share Pie Chart..."
let market_svg = generate_chart(market_share);

// Example 2: Budget Allocation
let budget_allocation = <chart
    type: 'pie',
    title: "Annual IT Budget Allocation (USD Millions)",
    width: 650,
    height: 650,
    colors: ["#e67e22", "#27ae60", "#8e44ad", "#2980b9", "#c0392b", "#16a085", "#f39c12"],
    data: [
        {label: "Infrastructure", value: 45},
        {label: "Software Licenses", value: 32},
        {label: "Personnel", value: 78},
        {label: "Training", value: 15},
        {label: "Security", value: 28},
        {label: "R&D", value: 35},
        {label: "Maintenance", value: 22}
    ]>

"Generating Budget Allocation Pie Chart..."
let budget_svg = generate_chart(budget_allocation);

// Example 3: Customer Satisfaction Distribution
let satisfaction_distribution = <chart
    type: 'pie',
    title: "Customer Satisfaction Survey Results",
    width: 600,
    height: 600,
    colors: ["#27ae60", "#2ecc71", "#f1c40f", "#e67e22", "#e74c3c"],
    data: [
        {label: "Excellent", value: 42},
        {label: "Good", value: 35},
        {label: "Average", value: 15},
        {label: "Poor", value: 6},
        {label: "Very Poor", value: 2}
    ]>

"Generating Customer Satisfaction Pie Chart..."
let satisfaction_svg = generate_chart(satisfaction_distribution);

// Example 4: Website Traffic Sources
let traffic_sources = <chart
    type: 'pie',
    title: "Website Traffic Sources (Last Quarter)",
    width: 750,
    height: 750,
    colors: ["#3498db", "#e74c3c", "#2ecc71", "#f39c12", "#9b59b6", "#1abc9c", "#34495e"],
    data: [
        {label: "Organic Search", value: 38},
        {label: "Direct Traffic", value: 22},
        {label: "Social Media", value: 18},
        {label: "Paid Search", value: 12},
        {label: "Email Marketing", value: 6},
        {label: "Referrals", value: 3},
        {label: "Others", value: 1}
    ]>

"Generating Traffic Sources Pie Chart..."
let traffic_svg = generate_chart(traffic_sources);

// Example 5: Project Time Allocation
let project_time = <chart
    type: 'pie',
    title: "Development Project Time Allocation",
    width: 680,
    height: 680,
    colors: ["#ff6b6b", "#4ecdc4", "#45b7d1", "#96ceb4", "#feca57", "#ff9ff3"],
    data: [
        {label: "Development", value: 45},
        {label: "Testing", value: 20},
        {label: "Design", value: 15},
        {label: "Planning", value: 10},
        {label: "Deployment", value: 6},
        {label: "Documentation", value: 4}
    ]>

"Generating Project Time Allocation Pie Chart..."
let project_svg = generate_chart(project_time);

// Example 6: Energy Consumption by Department
let energy_consumption = <chart
    type: 'pie',
    title: "Corporate Energy Consumption by Department",
    width: 720,
    height: 720,
    colors: ["#2c3e50", "#e74c3c", "#f39c12", "#27ae60", "#8e44ad", "#3498db"],
    data: [
        {label: "Manufacturing", value: 52},
        {label: "Office Spaces", value: 18},
        {label: "Data Center", value: 15},
        {label: "R&D Labs", value: 8},
        {label: "Warehouse", value: 5},
        {label: "Facilities", value: 2}
    ]>

"Generating Energy Consumption Pie Chart..."
let energy_svg = generate_chart(energy_consumption);

// Example 7: Investment Portfolio Distribution
let portfolio_distribution = <chart
    type: 'pie',
    title: "Investment Portfolio Asset Allocation (%)",
    width: 700,
    height: 700,
    colors: ["#16a085", "#2980b9", "#8e44ad", "#f39c12", "#e67e22", "#c0392b", "#27ae60"],
    data: [
        {label: "Stocks", value: 35},
        {label: "Bonds", value: 25},
        {label: "Real Estate", value: 15},
        {label: "Commodities", value: 10},
        {label: "Cash", value: 8},
        {label: "Crypto", value: 5},
        {label: "Others", value: 2}
    ]>

"Generating Portfolio Distribution Pie Chart..."
let portfolio_svg = generate_chart(portfolio_distribution);

// Summary and proportional analysis
{
    chart_type: "Pie Charts",
    description: "Ideal for showing proportions and percentages of parts to a whole",
    examples_created: 7,
    use_cases: [
        "Market share analysis and competitive positioning",
        "Budget allocation and financial distribution",
        "Survey results and satisfaction metrics",
        "Traffic source analysis and marketing attribution",
        "Resource allocation and time management",
        "Energy consumption and efficiency analysis",
        "Investment portfolio and asset distribution"
    ],
    proportional_analysis: {
        market_analysis: {
            title: "Smartphone Market Share",
            total_segments: len(market_share.data),
            svg_size: len(market_svg),
            largest_segment: "Samsung (28%)"
        },
        budget_analysis: {
            title: "IT Budget Distribution",
            total_categories: len(budget_allocation.data),
            svg_size: len(budget_svg),
            largest_allocation: "Personnel (78M USD)"
        },
        satisfaction_analysis: {
            title: "Customer Satisfaction",
            total_ratings: len(satisfaction_distribution.data),
            svg_size: len(satisfaction_svg),
            positive_ratio: "77% (Excellent + Good)"
        },
        traffic_analysis: {
            title: "Website Traffic Sources",
            total_channels: len(traffic_sources.data),
            svg_size: len(traffic_svg),
            primary_source: "Organic Search (38%)"
        },
        project_analysis: {
            title: "Development Time Allocation",
            total_phases: len(project_time.data),
            svg_size: len(project_svg),
            core_development: "45% actual coding time"
        },
        energy_analysis: {
            title: "Energy Consumption",
            total_departments: len(energy_consumption.data),
            svg_size: len(energy_svg),
            major_consumer: "Manufacturing (52%)"
        },
        portfolio_analysis: {
            title: "Investment Portfolio",
            total_assets: len(portfolio_distribution.data),
            svg_size: len(portfolio_svg),
            risk_distribution: "60% growth assets vs 40% conservative"
        }
    },
    visualization_benefits: [
        "Immediate visual comparison of proportions",
        "Easy identification of dominant segments",
        "Clear representation of percentage distributions",
        "Effective for categorical data representation",
        "Intuitive understanding of relative sizes"
    ],
    total_svg_output: len(market_svg) + len(budget_svg) + len(satisfaction_svg) +
                     len(traffic_svg) + len(project_svg) + len(energy_svg) + len(portfolio_svg),
    sample_svg_preview: market_svg[0 to min([150, len(market_svg)])],
    design_considerations: {
        optimal_segments: "5-8 segments for best readability",
        color_scheme: "High contrast colors for accessibility",
        labeling: "Both percentage and category labels recommended",
        size_guidelines: "Square aspect ratio works best for pie charts"
    }
}
