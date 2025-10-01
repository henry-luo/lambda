// Scatter Plot Examples - Excellent for correlation analysis
// Import the charting library functions
import 'chart'

"=== Scatter Plot Demonstrations ==="

// Example 1: Price vs Sales Volume Correlation
let price_sales_correlation = <chart
    type: 'scatter',
    title: "Product Price vs Sales Volume Analysis",
    width: 900,
    height: 650,
    colors: ["#3498db"],
    data: [
        {x: 25, y: 180, label: "Budget Phone"},
        {x: 45, y: 150, label: "Mid-range Phone"},
        {x: 80, y: 120, label: "Premium Phone"},
        {x: 120, y: 95, label: "Flagship Phone"},
        {x: 200, y: 65, label: "Ultra Premium"},
        {x: 350, y: 35, label: "Luxury Edition"},
        {x: 15, y: 220, label: "Basic Model"},
        {x: 60, y: 135, label: "Popular Model"},
        {x: 95, y: 105, label: "Pro Model"},
        {x: 150, y: 80, label: "Elite Model"},
        {x: 280, y: 45, label: "Collector Edition"},
        {x: 400, y: 25, label: "Limited Edition"}
    ]>

"Generating Price vs Sales Correlation Scatter Plot..."
let price_sales_svg = generate_chart(price_sales_correlation);

// Example 2: Employee Experience vs Salary Analysis
let experience_salary = <chart
    type: 'scatter',
    title: "Years of Experience vs Annual Salary (Tech Industry)",
    width: 950,
    height: 700,
    colors: ["#e74c3c"],
    data: [
        {x: 1, y: 65000, label: "Junior Dev"},
        {x: 2, y: 72000, label: "Dev I"},
        {x: 3, y: 78000, label: "Dev II"},
        {x: 4, y: 85000, label: "Senior Dev"},
        {x: 5, y: 92000, label: "Senior Dev II"},
        {x: 6, y: 105000, label: "Lead Dev"},
        {x: 7, y: 115000, label: "Principal Dev"},
        {x: 8, y: 125000, label: "Staff Engineer"},
        {x: 10, y: 140000, label: "Senior Staff"},
        {x: 12, y: 160000, label: "Principal Engineer"},
        {x: 15, y: 180000, label: "Distinguished Engineer"},
        {x: 18, y: 220000, label: "Fellow Engineer"}
    ]>

"Generating Experience vs Salary Scatter Plot..."
let experience_svg = generate_chart(experience_salary);

// Example 3: Marketing Spend vs Revenue Correlation
let marketing_revenue = <chart
    type: 'scatter',
    title: "Marketing Spend vs Revenue Generated (Monthly Data)",
    width: 850,
    height: 600,
    colors: ["#2ecc71"],
    data: [
        {x: 5000, y: 45000, label: "Jan"},
        {x: 7500, y: 62000, label: "Feb"},
        {x: 12000, y: 89000, label: "Mar"},
        {x: 8000, y: 68000, label: "Apr"},
        {x: 15000, y: 110000, label: "May"},
        {x: 18000, y: 125000, label: "Jun"},
        {x: 22000, y: 145000, label: "Jul"},
        {x: 25000, y: 160000, label: "Aug"},
        {x: 20000, y: 135000, label: "Sep"},
        {x: 30000, y: 185000, label: "Oct"},
        {x: 35000, y: 215000, label: "Nov"},
        {x: 40000, y: 245000, label: "Dec"}
    ]>

"Generating Marketing Spend vs Revenue Scatter Plot..."
let marketing_svg = generate_chart(marketing_revenue);

// Example 4: Website Load Time vs Bounce Rate
let performance_bounce = <chart
    type: 'scatter',
    title: "Website Load Time vs Bounce Rate Analysis",
    width: 800,
    height: 550,
    colors: ["#f39c12"],
    data: [
        {x: 0.8, y: 12, label: "Homepage"},
        {x: 1.2, y: 18, label: "Product Page"},
        {x: 1.5, y: 24, label: "Category Page"},
        {x: 2.1, y: 35, label: "Search Results"},
        {x: 2.8, y: 48, label: "Checkout Page"},
        {x: 3.2, y: 58, label: "User Profile"},
        {x: 3.8, y: 67, label: "Admin Panel"},
        {x: 4.5, y: 78, label: "Reports Page"},
        {x: 5.2, y: 85, label: "Analytics Page"},
        {x: 6.1, y: 92, label: "Legacy Form"}
    ]>

"Generating Performance vs Bounce Rate Scatter Plot..."
let performance_svg = generate_chart(performance_bounce);

// Example 5: Company Size vs Innovation Index
let size_innovation = <chart
    type: 'scatter',
    title: "Company Size vs Innovation Index (Tech Sector)",
    width: 900,
    height: 650,
    colors: ["#9b59b6"],
    data: [
        {x: 50, y: 85, label: "TechStart Inc"},
        {x: 150, y: 78, label: "InnovateLab"},
        {x: 300, y: 72, label: "DevCorp"},
        {x: 500, y: 69, label: "SoftwarePlus"},
        {x: 750, y: 65, label: "TechGlobal"},
        {x: 1200, y: 58, label: "DataSystems"},
        {x: 2000, y: 52, label: "EnterpriseNet"},
        {x: 3500, y: 45, label: "BigTechCorp"},
        {x: 5000, y: 38, label: "MegaSoft"},
        {x: 8000, y: 32, label: "TitanTech"},
        {x: 12000, y: 28, label: "GlobalGiant"},
        {x: 20000, y: 22, label: "TechMonolith"}
    ]>

"Generating Company Size vs Innovation Scatter Plot..."
let innovation_svg = generate_chart(size_innovation);

// Example 6: Temperature vs Ice Cream Sales
let temperature_sales = <chart
    type: 'scatter',
    title: "Daily Temperature vs Ice Cream Sales Revenue",
    width: 850,
    height: 600,
    colors: ["#1abc9c"],
    data: [
        {x: 15, y: 1200, label: "Cool Day"},
        {x: 18, y: 1850, label: "Mild Day"},
        {x: 22, y: 2400, label: "Warm Day"},
        {x: 25, y: 3200, label: "Hot Day"},
        {x: 28, y: 4100, label: "Very Hot"},
        {x: 32, y: 5200, label: "Scorching"},
        {x: 35, y: 6800, label: "Extreme Heat"},
        {x: 20, y: 2100, label: "Pleasant"},
        {x: 26, y: 3600, label: "Summer Day"},
        {x: 30, y: 4900, label: "Beach Weather"},
        {x: 33, y: 5900, label: "Heat Wave"},
        {x: 37, y: 7500, label: "Record High"}
    ]>

"Generating Temperature vs Sales Scatter Plot..."
let temp_sales_svg = generate_chart(temperature_sales);

// Example 7: Study Hours vs Exam Scores
let study_scores = <chart
    type: 'scatter',
    title: "Study Hours vs Exam Scores (Student Performance)",
    width: 800,
    height: 650,
    colors: ["#34495e"],
    data: [
        {x: 2, y: 45, label: "Student A"},
        {x: 4, y: 58, label: "Student B"},
        {x: 6, y: 67, label: "Student C"},
        {x: 8, y: 72, label: "Student D"},
        {x: 10, y: 78, label: "Student E"},
        {x: 12, y: 84, label: "Student F"},
        {x: 15, y: 88, label: "Student G"},
        {x: 18, y: 92, label: "Student H"},
        {x: 20, y: 95, label: "Student I"},
        {x: 25, y: 97, label: "Student J"},
        {x: 3, y: 52, label: "Student K"},
        {x: 7, y: 70, label: "Student L"},
        {x: 14, y: 86, label: "Student M"},
        {x: 22, y: 94, label: "Student N"}
    ]>

"Generating Study Hours vs Scores Scatter Plot..."
let study_svg = generate_chart(study_scores);

// Correlation analysis and summary
{
    chart_type: "Scatter Plots",
    description: "Excellent for correlation analysis and identifying relationships between two variables",
    examples_created: 7,
    use_cases: [
        "Price sensitivity and demand analysis",
        "HR analytics and compensation studies",
        "Marketing ROI and budget optimization",
        "Website performance and user behavior",
        "Organizational studies and efficiency metrics",
        "Sales forecasting and seasonal correlations",
        "Educational research and performance analysis"
    ],
    correlation_insights: {
        price_sales: {
            title: "Price vs Sales Volume",
            data_points: len(price_sales_correlation.data),
            svg_size: len(price_sales_svg),
            correlation_type: "Strong negative correlation",
            business_insight: "Higher prices generally lead to lower sales volume"
        },
        experience_salary: {
            title: "Experience vs Salary",
            data_points: len(experience_salary.data),
            svg_size: len(experience_svg),
            correlation_type: "Strong positive correlation",
            career_insight: "Clear salary progression with experience"
        },
        marketing_revenue: {
            title: "Marketing Spend vs Revenue",
            data_points: len(marketing_revenue.data),
            svg_size: len(marketing_svg),
            correlation_type: "Strong positive correlation",
            roi_insight: "Marketing investment shows positive returns"
        },
        performance_bounce: {
            title: "Load Time vs Bounce Rate",
            data_points: len(performance_bounce.data),
            svg_size: len(performance_svg),
            correlation_type: "Strong positive correlation",
            ux_insight: "Slower load times significantly increase bounce rates"
        },
        size_innovation: {
            title: "Company Size vs Innovation",
            data_points: len(size_innovation.data),
            svg_size: len(innovation_svg),
            correlation_type: "Negative correlation",
            org_insight: "Larger companies tend to be less innovative"
        },
        temperature_sales: {
            title: "Temperature vs Ice Cream Sales",
            data_points: len(temperature_sales.data),
            svg_size: len(temp_sales_svg),
            correlation_type: "Strong positive correlation",
            seasonal_insight: "Classic example of weather-driven sales"
        },
        study_performance: {
            title: "Study Hours vs Exam Scores",
            data_points: len(study_scores.data),
            svg_size: len(study_svg),
            correlation_type: "Positive correlation with diminishing returns",
            education_insight: "More study time improves scores but with decreasing marginal benefit"
        }
    },
    analytical_features: [
        "Relationship identification between variables",
        "Outlier detection and analysis",
        "Trend pattern recognition",
        "Correlation strength assessment",
        "Data distribution visualization"
    ],
    statistical_applications: [
        "Regression analysis preparation",
        "Hypothesis testing support",
        "Predictive modeling foundation",
        "Data quality assessment",
        "Business intelligence insights"
    ],
    total_svg_output: len(price_sales_svg) + len(experience_svg) + len(marketing_svg) +
                     len(performance_svg) + len(innovation_svg) + len(temp_sales_svg) + len(study_svg),
    sample_svg_preview: price_sales_svg[0 to min([150, len(price_sales_svg)])],
    design_best_practices: {
        point_sizing: "Use consistent point sizes for readability",
        color_coding: "Single color for simple correlations, multiple for categories",
        axis_scaling: "Ensure appropriate scaling to show correlation clearly",
        labeling: "Include data point labels for context and identification"
    }
}
