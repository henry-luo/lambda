// let timeseries_data = [
//     {timestamp: t'2024-01-01', value: 100.5},
//     {timestamp: t'2024-01-02', value: 102.3},
//     {timestamp: t'2024-01-03', value: 98.7},
//     {timestamp: t'2024-01-04', value: 105.1},
//     {timestamp: t'2024-01-05', value: 107.8},
//     {timestamp: t'2024-01-06', value: 103.2},
//     {timestamp: t'2024-01-07', value: 109.5},
//     {timestamp: t'2024-01-08', value: 111.2},
//     {timestamp: t'2024-01-09', value: 108.9},
//     {timestamp: t'2024-01-10', value: 113.4}
// ];

// // Correlation coefficient calculation
// pub fn correlation(x_vals, y_vals) {
//     if len(x_vals) != len(y_vals) { null } // error("Arrays must be same length");
    
//     let n = len(x_vals);
//     let x_mean = avg(x_vals);
//     let y_mean = avg(y_vals);
    
//     let numerator = sum(for (i in 0 to n-1) (x_vals[i] - x_mean) * (y_vals[i] - y_mean));
//     // numerator
//     let x_var = sum(for (x in x_vals) (x - x_mean) ^ 2);
//     let y_var = sum(for (y in y_vals) (y - y_mean) ^ 2);
//     let denominator = (x_var * y_var) ^ 0.5;
//     denominator
    
//     if (denominator == 0.0) 0.0 else numerator / denominator
// }

// // Data preprocessing pipeline
// pub fn preprocess_dataset(raw_data) {
//     // Handle missing values
//     let cleaned_data = for (row in raw_data) {
//         {
//             id: row.id,
//             features: for (val in row.features) if (val != null) val else 0.0,
//             target: if (row.target != null) row.target else avg(for (r in raw_data) if (r.target != null) r.target else 0.0)
//         }
//     };
    
//     // Feature scaling (min-max normalization)
//     let feature_count = len(cleaned_data[0].features);
//     let feature_mins = for (i in 0 to feature_count-1) 
//         min(for (row in cleaned_data) row.features[i]);
//     let feature_maxs = for (i in 0 to feature_count-1) 
//         max(for (row in cleaned_data) row.features[i]);
    
//     let normalized_data = for (row in cleaned_data) {
//         {
//             id: row.id,
//             features: for (i in 0 to len(row.features)-1) (
//                 let range_val = feature_maxs[i] - feature_mins[i],
//                 if (range_val == 0.0) 0.0 
//                 else (row.features[i] - feature_mins[i]) / range_val
//             ),
//             target: row.target
//         }
//     };
    
//     {
//         processed_data: normalized_data,
//         feature_statistics: {
//             mins: feature_mins,
//             maxs: feature_maxs,
//             ranges: for (i in 0 to feature_count-1) feature_maxs[i] - feature_mins[i]
//         }
//     }
// }

// let synthetic_dataset = [
//     {id: "sample_1", features: [0.8, 0.3, 0.7, 0.2], target: 1.5},
//     {id: "sample_2", features: [0.2, 0.9, 0.1, 0.8], target: 2.1},
//     {id: "sample_3", features: [0.6, 0.5, 0.4, 0.3], target: 1.2},
//     {id: "sample_4", features: [0.9, 0.1, 0.8, 0.6], target: 2.8},
//     {id: "sample_5", features: [0.3, 0.7, 0.2, 0.9], target: 1.9},
//     {id: "sample_6", features: [null, 0.4, 0.6, 0.1], target: null},  // missing values
//     {id: "sample_7", features: [0.7, 0.8, 0.3, 0.5], target: 2.3},
//     {id: "sample_8", features: [0.1, 0.2, 0.9, 0.7], target: 1.8}
// ];

// let preprocessing_results = preprocess_dataset(synthetic_dataset);
// preprocessing_results

// // let correlation_analysis = (
//     let feature_1 = for (row in preprocessing_results.processed_data) row.features[0]
//     "feature_1"; feature_1
//     let feature_2 = for (row in preprocessing_results.processed_data) row.features[1]
//     "feature_2"; feature_2
//     let targets = for (row in preprocessing_results.processed_data) row.target
//     "targets"; targets
//     {
//         feature_correlation: correlation(feature_1, feature_2),
//         feature_1_target_corr: correlation(feature_1, targets),
//         feature_2_target_corr: correlation(feature_2, targets)
//     }
// //);

fn test(x_var, y_var) {
    "str"
}
test(1, 2)