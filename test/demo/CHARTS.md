# Lambda Charting Library Documentation

A comprehensive charting library for Lambda Script that transforms Mark notation into beautiful SVG charts.

## 📁 Chart Demo Files

### Core Library
- **`chart.ls`** - Main charting library with SVG generation functions

### Chart Type Examples
- **`bar_charts.ls`** - Bar chart examples for categorical data comparison
- **`line_charts.ls`** - Line chart examples for time series and trend analysis
- **`pie_charts.ls`** - Pie chart examples for proportional data visualization
- **`scatter_plots.ls`** - Scatter plot examples for correlation analysis

### Demo Suite
- **`demo_index.ls`** - Complete demo suite with quick examples and advanced configurations

## 🚀 Quick Start

```lambda
// Import the charting library
import 'chart'

// Create a chart using Mark notation
let sales_chart = <chart
    type: 'bar',
    title: "Monthly Sales",
    width: 800,
    height: 600,
    data: [
        {label: "Jan", value: 120},
        {label: "Feb", value: 98},
        {label: "Mar", value: 86}
    ]>

// Generate SVG output
let svg_output = generate_chart(sales_chart);
```

## 📊 Supported Chart Types

### Bar Charts
Perfect for comparing categorical data across different groups.

**Use Cases:**
- Sales performance comparison
- Regional revenue analysis
- Survey result visualization
- Technology adoption metrics

**Example:**
```lambda
let bar_chart = <chart
    type: 'bar',
    title: "Q4 Sales Performance",
    colors: ["#3498db", "#e74c3c", "#2ecc71"],
    data: [
        {label: "Product A", value: 120},
        {label: "Product B", value: 98},
        {label: "Product C", value: 86}
    ]>
```

### Line Charts
Great for time series and trend analysis over continuous periods.

**Use Cases:**
- Stock price movements
- Temperature monitoring
- User growth metrics
- Performance tracking

**Example:**
```lambda
let line_chart = <chart
    type: 'line',
    title: "Website Traffic Trend",
    data: [
        {label: "Jan", value: 1200},
        {label: "Feb", value: 1350},
        {label: "Mar", value: 1180}
    ]>
```

### Pie Charts
Ideal for showing proportions and percentages of parts to a whole.

**Use Cases:**
- Market share analysis
- Budget allocation
- Survey distributions
- Resource allocation

**Example:**
```lambda
let pie_chart = <chart
    type: 'pie',
    title: "Market Share Analysis",
    data: [
        {label: "Company A", value: 35},
        {label: "Company B", value: 28},
        {label: "Company C", value: 22}
    ]>
```

### Scatter Plots
Excellent for correlation analysis and relationship identification.

**Use Cases:**
- Price vs demand analysis
- Performance correlations
- Quality vs cost relationships
- Scientific data analysis

**Example:**
```lambda
let scatter_chart = <chart
    type: 'scatter',
    title: "Price vs Sales Correlation",
    data: [
        {x: 10, y: 150, label: "Item 1"},
        {x: 15, y: 120, label: "Item 2"},
        {x: 20, y: 95, label: "Item 3"}
    ]>
```

## 🎨 Customization Options

### Colors
```lambda
colors: ["#3498db", "#e74c3c", "#2ecc71", "#f39c12"]
```

### Dimensions
```lambda
width: 800,
height: 600
```

### Data Formats
```lambda
// Simple format
data: [{label: "Item", value: 42}]

// Extended format
data: [{category: "Q1", amount: 1500, growth: "+12%"}]

// Scatter plot format
data: [{x: 10, y: 25, label: "Point A"}]
```

## 🛠 Technical Implementation

### SVG Generation
The library generates complete, valid SVG markup including:
- Headers with viewBox and namespaces
- Styled text elements
- Geometric shapes (rectangles, circles, lines, paths)
- Proper scaling and positioning
- Responsive design support

### Data Processing
- Automatic value extraction from various data formats
- Dynamic scaling to fit chart dimensions
- Label processing and positioning
- Color scheme management

### Lambda Script Features
- **Mark Notation**: Native element syntax for chart configuration
- **Functional Programming**: Comprehensions for data transformation
- **Type System**: Proper handling of maps, arrays, primitives
- **String Processing**: Dynamic SVG markup generation

## 🏃‍♂️ Running the Examples

To run any of the demo files:

```bash
# Run specific chart type examples
lambda bar_charts.ls
lambda line_charts.ls
lambda pie_charts.ls
lambda scatter_plots.ls

# Run complete demo suite
lambda demo_index.ls
```

## 📈 Real-World Examples

The demo files include examples for:

### Business Intelligence
- Sales performance dashboards
- Revenue analysis charts
- Customer acquisition trends
- Market share visualizations

### Technical Monitoring
- System performance metrics
- Error rate distributions
- Load monitoring charts
- Benchmark comparisons

### Financial Analysis
- Investment portfolio distributions
- Budget allocation charts
- ROI analysis plots
- Price correlation studies

## 🔄 Integration

### Web Integration
The generated SVG can be:
- Embedded directly in HTML documents
- Saved as `.svg` files
- Styled with CSS
- Made interactive with JavaScript

### Document Generation
Perfect for:
- Report generation
- Dashboard creation
- Data visualization in documents
- Automated chart creation

## 🎯 Next Steps

1. **Export Functionality**: Add file export capabilities
2. **Interactive Features**: JavaScript integration for hover effects
3. **Animation Support**: Chart transition animations
4. **Advanced Layouts**: Multi-chart compositions
5. **Template System**: Reusable chart templates

## 📝 Notes

This charting library demonstrates Lambda Script's capabilities for:
- Document processing and generation
- Data visualization
- SVG markup creation
- Functional data transformation
- Mark notation utilization

The library serves as both a practical tool and a showcase of Lambda Script's unique features for data processing and document generation tasks.
