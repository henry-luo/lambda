// Financial Portfolio Analysis
pub fn analyze_investment_portfolio(holdings) {
    let asset_analysis = for (holding in holdings) 
        (let current_value = holding.shares * holding.current_price,
        let gain_loss = current_value - holding.cost_basis,
        {
            'symbol': holding['symbol'],
            shares: holding.shares,
            current_value: current_value,
            cost_basis: holding.cost_basis,
            unrealized_gain_loss: gain_loss,
            return_percent: (gain_loss / holding.cost_basis) * 100
        });
    
    let portfolio_metrics = {
        total_value: sum(for (asset in asset_analysis) asset.current_value),
        total_cost_basis: sum(for (asset in asset_analysis) asset.cost_basis),
        total_gain_loss: sum(for (asset in asset_analysis) asset.unrealized_gain_loss),
        overall_return: (sum(for (asset in asset_analysis) asset.unrealized_gain_loss) / 
                        sum(for (asset in asset_analysis) asset.cost_basis)) * 100
    };
    
    // Calculate Herfindahl-Hirschman Index (HHI) for diversification
    let total_portfolio_value = sum(for (asset in asset_analysis) asset.current_value);
    let asset_weights = for (asset in asset_analysis) 
        (asset.current_value / total_portfolio_value);
    let hhi = sum(for (weight in asset_weights) weight * weight);
    let diversification_score = 1 - hhi;
    
    {
        portfolio_summary: portfolio_metrics,
        individual_assets: asset_analysis,
        asset_count: len(holdings),
        diversification_score: diversification_score
    }
}

// Sample portfolio data for testing
let sample_holdings = [
    {
        'symbol': "AAPL",
        shares: 100,
        current_price: 175.50,
        cost_basis: 15000.00
    },
    {
        'symbol': "GOOGL", 
        shares: 50,
        current_price: 140.25,
        cost_basis: 6800.00
    },
    {
        'symbol': "MSFT",
        shares: 80,
        current_price: 420.75,
        cost_basis: 28000.00
    },
    {
        'symbol': "TSLA",
        shares: 25,
        current_price: 245.60,
        cost_basis: 7500.00
    },
    {
        'symbol': "NVDA",
        shares: 30,
        current_price: 125.80,
        cost_basis: 3200.00
    }
];

// Test the function with sample data
let portfolio_analysis = analyze_investment_portfolio(sample_holdings);

// Test sum() function with simple data
let simple_numbers = [1, 2, 3, 4, 5];
let sum_test = sum(simple_numbers);

// Test sum() with extracted values 
let test_asset = sample_holdings[0];
let test_current_value = test_asset.shares * test_asset.current_price;

// Test sum() with a comprehension
let test_comprehension = for (holding in sample_holdings) holding.shares * holding.current_price;
let sum_comprehension = sum(test_comprehension);
// "sum_comprehension"; sum_comprehension

// Return test results and portfolio analysis
{
    simple_sum: sum_test,
    comprehension_sum: sum_comprehension,
    test_current_value: test_current_value,
    portfolio: portfolio_analysis
}
