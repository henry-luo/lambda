// Advanced Data Science Pipeline
// Demonstrates comprehensive data processing, statistical analysis, and ML preprocessing

// Statistical analysis functions
pub fn descriptive_stats(data) {
    let sorted_data = data; // for (x in data, _ in 1 to len(data)) min(data);  // simplified sort
    let n = len(data);
    let mean_val = avg(data);
    let variance = avg(for (x in data) (x - mean_val) ** 2);
    let std_dev = variance ** 0.5;
    
    // Quartiles (simplified calculation)
    let q1_idx = int(n * 0.25);
    let q2_idx = int(n * 0.5);
    let q3_idx = int(n * 0.75);
    
    {
        count: n,
        mean: mean_val,
        median: sorted_data[q2_idx],
        std_dev: std_dev,
        variance: variance,
        min: min(data),
        max: max(data),
        q1: if (q1_idx < n) sorted_data[q1_idx] else min(data),
        q3: if (q3_idx < n) sorted_data[q3_idx] else max(data),
        range: max(data) - min(data)
    }
}

// Correlation coefficient calculation
pub fn correlation(x_vals, y_vals) {
    if len(x_vals) != len(y_vals) { null } // error("Arrays must be same length");
    
    let n = len(x_vals);
    let x_mean = avg(x_vals);
    let y_mean = avg(y_vals);
    
    let numerator = sum(for (i in 0 to n-1) (x_vals[i] - x_mean) * (y_vals[i] - y_mean));
    let x_var = sum(for (x in x_vals) (x - x_mean) ** 2);
    let y_var = sum(for (y in y_vals) (y - y_mean) ** 2);
    let denominator = (x_var * y_var) ** 0.5;
    
    if (denominator == 0.0) 0.0 else numerator / denominator
}

// Data preprocessing pipeline
pub fn preprocess_dataset(raw_data) {
    // Handle missing values
    let cleaned_data = for (row in raw_data) {
        {
            id: row.id,
            features: for (val in row.features) if (val != null) val else 0.0,
            target: if (row.target != null) row.target else avg(for (r in raw_data) r.target)
        }
    };
    
    // Feature scaling (min-max normalization)
    let feature_count = len(cleaned_data[0].features);
    let feature_mins = for (i in 0 to feature_count-1) 
        min(for (row in cleaned_data) row.features[i]);
    let feature_maxs = for (i in 0 to feature_count-1) 
        max(for (row in cleaned_data) row.features[i]);
    
    let normalized_data = for (row in cleaned_data) {
        {
            id: row.id,
            features: for (i in 0 to len(row.features)-1) (
                let range_val = feature_maxs[i] - feature_mins[i],
                if (range_val == 0.0) 0.0 
                else (row.features[i] - feature_mins[i]) / range_val
            ),
            target: row.target
        }
    };
    
    {
        processed_data: normalized_data,
        feature_statistics: {
            mins: feature_mins,
            maxs: feature_maxs,
            ranges: for (i in 0 to feature_count-1) feature_maxs[i] - feature_mins[i]
        }
    }
}

// Advanced feature engineering
pub fn engineer_features(dataset) {
    for row in dataset {
        let base_features = row.features;
        let engineered = {
            // Polynomial features (degree 2)
            poly_features: for (i in 0 to len(base_features)-1)
                for (j in i to len(base_features)-1)
                    base_features[i] * base_features[j],

            // Interaction features
            interactions: for (i in 0 to len(base_features)-2)
                base_features[i] * base_features[i+1],
            
            // Statistical features
            feature_sum: sum(base_features),
            feature_mean: avg(base_features),
            feature_std: (
                let mean_val = avg(base_features),
                let variance = avg(for (x in base_features) (x - mean_val) ** 2),
                variance ** 0.5
            )
        };
        
        {
            id: row.id,
            original_features: base_features,
            engineered_features: engineered,
            target: row.target
        }
    }
}

// Time series analysis
pub fn analyze_timeseries(data) { // [{timestamp: datetime, value: float}]
    let sorted_data = data;  // assume pre-sorted by timestamp
    
    // Calculate moving averages
    let window_size = 7;
    let moving_averages = for (i in window_size-1 to len(sorted_data)-1) (
        let window_values = for (j in i-window_size+1 to i) sorted_data[j].value,
        {
            timestamp: sorted_data[i].timestamp,
            ma_7: avg(window_values)
        }
    )
    
    // Detect trends and seasonality
    let differences = for (i in 1 to len(sorted_data)-1)
        sorted_data[i].value - sorted_data[i-1].value;
    
    let trend_strength = (
        let positive_changes = len(for (diff in differences) if (diff > 0) diff else null),
        let total_changes = len(differences),
        if (total_changes > 0) positive_changes / total_changes else 0.0
    );
    
    {
        original_data: sorted_data,
        moving_averages: moving_averages,
        trend_analysis: {
            strength: trend_strength,
            avg_change: avg(differences),
            volatility: (
                let mean_change = avg(differences),
                let variance = avg(for (diff in differences) (diff - mean_change) ** 2),
                variance ** 0.5
            )
        },
        summary_stats: descriptive_stats(for (point in sorted_data) point.value)
    }
}

// Sample synthetic dataset generation
let synthetic_dataset = [
    {id: "sample_1", features: [0.8, 0.3, 0.7, 0.2], target: 1.5},
    {id: "sample_2", features: [0.2, 0.9, 0.1, 0.8], target: 2.1},
    {id: "sample_3", features: [0.6, 0.5, 0.4, 0.3], target: 1.2},
    {id: "sample_4", features: [0.9, 0.1, 0.8, 0.6], target: 2.8},
    {id: "sample_5", features: [0.3, 0.7, 0.2, 0.9], target: 1.9},
    {id: "sample_6", features: [null, 0.4, 0.6, 0.1], target: null},  // missing values
    {id: "sample_7", features: [0.7, 0.8, 0.3, 0.5], target: 2.3},
    {id: "sample_8", features: [0.1, 0.2, 0.9, 0.7], target: 1.8}
];

// Sample time series data
let timeseries_data = [
    {timestamp: t'2024-01-01', value: 100.5},
    {timestamp: t'2024-01-02', value: 102.3},
    {timestamp: t'2024-01-03', value: 98.7},
    {timestamp: t'2024-01-04', value: 105.1},
    {timestamp: t'2024-01-05', value: 107.8},
    {timestamp: t'2024-01-06', value: 103.2},
    {timestamp: t'2024-01-07', value: 109.5},
    {timestamp: t'2024-01-08', value: 111.2},
    {timestamp: t'2024-01-09', value: 108.9},
    {timestamp: t'2024-01-10', value: 113.4}
];

// Execute comprehensive analysis pipeline
'=== Data Science Pipeline Results ==='

let preprocessing_results = preprocess_dataset(synthetic_dataset);
"Preprocessing completed for " ++ string(len(preprocessing_results.processed_data)) ++ " samples"

'---'
let feature_engineering_results = engineer_features(preprocessing_results.processed_data);
"Feature engineering completed with " ++ string(len(feature_engineering_results[0].engineered_features.poly_features)) ++ " polynomial features"

let correlation_analysis = (
    let feature_1 = for (row in preprocessing_results.processed_data) row.features[0],
    let feature_2 = for (row in preprocessing_results.processed_data) row.features[1],
    let targets = for (row in preprocessing_results.processed_data) row.target,
    {
        feature_correlation: correlation(feature_1, feature_2),
        feature_1_target_corr: correlation(feature_1, targets),
        feature_2_target_corr: correlation(feature_2, targets)
    }
);

let timeseries_analysis = analyze_timeseries(timeseries_data);

// Final comprehensive report
{
    dataset_summary: {
        original_samples: len(synthetic_dataset),
        processed_samples: len(preprocessing_results.processed_data),
        feature_count: len(preprocessing_results.processed_data[0].features)
    },
    statistical_analysis: {
        target_stats: descriptive_stats(for (row in preprocessing_results.processed_data) row.target),
        correlation_matrix: correlation_analysis
    },
    preprocessing_metadata: preprocessing_results.feature_statistics,
    timeseries_insights: {
        trend_strength: timeseries_analysis.trend_analysis.strength,
        average_value: timeseries_analysis.summary_stats.mean,
        volatility: timeseries_analysis.trend_analysis.volatility,
        data_points: len(timeseries_analysis.original_data)
    },
    feature_engineering_sample: feature_engineering_results[0].engineered_features
}
