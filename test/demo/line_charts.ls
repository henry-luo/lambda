// Line Chart Examples - Great for time series and trend analysis
// Import the charting library functions
import 'chart'

"=== Line Chart Demonstrations ==="

// Example 1: Stock Price Trend Analysis
let stock_price_trend = <chart
    type: 'line',
    title: "TECH Stock Price Trend (Last 12 Months)",
    width: 1000,
    height: 600,
    colors: ["#2c3e50"],
    data: [
        {label: "Jan", value: 142.50},
        {label: "Feb", value: 158.20},
        {label: "Mar", value: 151.80},
        {label: "Apr", value: 167.40},
        {label: "May", value: 172.90},
        {label: "Jun", value: 165.30},
        {label: "Jul", value: 178.60},
        {label: "Aug", value: 184.20},
        {label: "Sep", value: 189.70},
        {label: "Oct", value: 195.40},
        {label: "Nov", value: 203.80},
        {label: "Dec", value: 198.50}
    ]>

"Generating Stock Price Trend Line Chart..."
let stock_svg = generate_chart(stock_price_trend);

// Example 2: Temperature Variations Over Time
let temperature_trend = <chart
    type: 'line',
    title: "Daily Average Temperature (°C) - City Climate Data",
    width: 900,
    height: 550,
    colors: ["#e74c3c"],
    data: [
        {label: "Week 1", value: 22.4},
        {label: "Week 2", value: 24.1},
        {label: "Week 3", value: 26.8},
        {label: "Week 4", value: 28.3},
        {label: "Week 5", value: 30.2},
        {label: "Week 6", value: 29.7},
        {label: "Week 7", value: 31.5},
        {label: "Week 8", value: 33.1},
        {label: "Week 9", value: 31.8},
        {label: "Week 10", value: 29.4},
        {label: "Week 11", value: 27.2},
        {label: "Week 12", value: 25.6}
    ]>

"Generating Temperature Trend Line Chart..."
let temperature_svg = generate_chart(temperature_trend);

// Example 3: Website User Growth
let user_growth = <chart
    type: 'line',
    title: "Monthly Active Users Growth (in thousands)",
    width: 950,
    height: 600,
    colors: ["#27ae60"],
    data: [
        {label: "Q1 2023", value: 45},
        {label: "Q2 2023", value: 52},
        {label: "Q3 2023", value: 61},
        {label: "Q4 2023", value: 78},
        {label: "Q1 2024", value: 89},
        {label: "Q2 2024", value: 105},
        {label: "Q3 2024", value: 118},
        {label: "Q4 2024", value: 142},
        {label: "Q1 2025", value: 156},
        {label: "Q2 2025", value: 178}
    ]>

"Generating User Growth Line Chart..."
let growth_svg = generate_chart(user_growth);

// Example 4: CPU Performance Monitoring
let cpu_performance = <chart
    type: 'line',
    title: "Server CPU Usage Over 24 Hours (%)",
    width: 1100,
    height: 550,
    colors: ["#3498db"],
    data: [
        {label: "00:00", value: 12},
        {label: "02:00", value: 8},
        {label: "04:00", value: 6},
        {label: "06:00", value: 15},
        {label: "08:00", value: 35},
        {label: "10:00", value: 52},
        {label: "12:00", value: 68},
        {label: "14:00", value: 71},
        {label: "16:00", value: 76},
        {label: "18:00", value: 82},
        {label: "20:00", value: 74},
        {label: "22:00", value: 45}
    ]>

"Generating CPU Performance Line Chart..."
let cpu_svg = generate_chart(cpu_performance);

// Example 5: Sales Revenue Trend with Seasonal Patterns
let revenue_trend = <chart
    type: 'line',
    title: "Monthly Sales Revenue Trend (USD Thousands)",
    width: 1000,
    height: 650,
    colors: ["#9b59b6"],
    data: [
        {label: "Jan 2024", value: 285},
        {label: "Feb 2024", value: 310},
        {label: "Mar 2024", value: 342},
        {label: "Apr 2024", value: 378},
        {label: "May 2024", value: 395},
        {label: "Jun 2024", value: 412},
        {label: "Jul 2024", value: 448},
        {label: "Aug 2024", value: 467},
        {label: "Sep 2024", value: 489},
        {label: "Oct 2024", value: 512},
        {label: "Nov 2024", value: 598},
        {label: "Dec 2024", value: 645},
        {label: "Jan 2025", value: 312},
        {label: "Feb 2025", value: 338},
        {label: "Mar 2025", value: 365}
    ]>

"Generating Revenue Trend Line Chart..."
let revenue_svg = generate_chart(revenue_trend);

// Example 6: Multi-metric Performance Dashboard (simulated as single line)
let performance_metrics = <chart
    type: 'line',
    title: "Application Response Time Trend (milliseconds)",
    width: 900,
    height: 500,
    colors: ["#f39c12"],
    data: [
        {label: "Mon", value: 145},
        {label: "Tue", value: 132},
        {label: "Wed", value: 128},
        {label: "Thu", value: 156},
        {label: "Fri", value: 189},
        {label: "Sat", value: 167},
        {label: "Sun", value: 142}
    ]>

"Generating Performance Metrics Line Chart..."
let performance_svg = generate_chart(performance_metrics);

// Summary and analysis results
{
    chart_type: "Line Charts",
    description: "Great for time series and trend analysis, showing changes over continuous periods",
    examples_created: 6,
    use_cases: [
        "Stock price movements and financial trends",
        "Temperature and climate monitoring",
        "User growth and engagement metrics",
        "System performance monitoring",
        "Revenue and business trend analysis",
        "Application performance tracking"
    ],
    trend_analysis: {
        stock_trend: {
            title: "Stock Price Analysis",
            data_points: len(stock_price_trend.data),
            svg_size: len(stock_svg),
            trend_direction: "Generally upward with some volatility"
        },
        temperature_trend: {
            title: "Temperature Monitoring",
            data_points: len(temperature_trend.data),
            svg_size: len(temperature_svg),
            pattern: "Seasonal variation with peak in summer"
        },
        growth_trend: {
            title: "User Growth",
            data_points: len(user_growth.data),
            svg_size: len(growth_svg),
            growth_rate: "Consistent exponential growth"
        },
        cpu_trend: {
            title: "CPU Performance",
            data_points: len(cpu_performance.data),
            svg_size: len(cpu_svg),
            pattern: "Daily usage cycle with peak during business hours"
        },
        revenue_trend: {
            title: "Revenue Analysis",
            data_points: len(revenue_trend.data),
            svg_size: len(revenue_svg),
            seasonality: "Strong holiday seasonal boost visible"
        },
        performance_trend: {
            title: "Response Time Monitoring",
            data_points: len(performance_metrics.data),
            svg_size: len(performance_svg),
            weekly_pattern: "Higher response times on weekends"
        }
    },
    technical_features: [
        "Continuous data visualization",
        "Trend identification",
        "Pattern recognition",
        "Time-based analysis",
        "Performance monitoring"
    ],
    total_svg_output: len(stock_svg) + len(temperature_svg) + len(growth_svg) +
                     len(cpu_svg) + len(revenue_svg) + len(performance_svg),
    sample_svg_preview: stock_svg[0 to min([150, len(stock_svg)])]
}
