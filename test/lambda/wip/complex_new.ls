// Sophisticated Lambda Script function examples demonstrating advanced capabilities
// This file showcases complex data processing, mathematical calculations, and multi-format I/O

"=== LAMBDA SCRIPT COMPLEX FUNCTION EXAMPLES ==="

// 1. Advanced Data Pipeline Processing Function
pub fn process_multi_format_data(file_paths) {
    let loaded_data = (for (path in file_paths) {
        file_path: path,
        content: input(path, 'auto'),
        loaded_at: justnow()
    });
    
    let processed_data = (for (data in loaded_data) {
        original: data,
        record_count: (if (data.content is [object]) len(data.content) else 1),
        success: true
    });
    
    {
        input_summary: {
            total_files: len(file_paths),
            total_records: sum((for (data in processed_data) data.record_count))
        },
        processing_results: processed_data,
        output_generated_at: justnow(),
        success: true
    }
}

// 2. Financial Portfolio Analysis
pub fn analyze_investment_portfolio(holdings) {
    let asset_analysis = (for (holding in holdings) 
        let current_value = holding.shares * holding.current_price,
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
        total_value: sum((for (asset in asset_analysis) asset.current_value)),
        total_cost_basis: sum((for (asset in asset_analysis) asset.cost_basis)),
        total_gain_loss: sum((for (asset in asset_analysis) asset.unrealized_gain_loss)),
        overall_return: (sum((for (asset in asset_analysis) asset.unrealized_gain_loss)) / 
                        sum((for (asset in asset_analysis) asset.cost_basis))) * 100
    };
    
    {
        timestamp: justnow(),
        portfolio_summary: portfolio_metrics,
        individual_assets: asset_analysis,
        asset_count: len(holdings),
        diversification_score: len(asset_analysis) / max(len(asset_analysis), 1)
    }
}

// 3. Text Processing and Analysis
pub fn analyze_document_corpus(document_paths, max_keywords) {
    let documents = (for (path in document_paths)
        let content = input(path, 'auto'),
        {
            path: path,
            content: content,
            text: (if (content is string) content else string(content))
        });
    
    fn extract_keywords(text, max_count) {
        let words = text.lower().split(" ");
        let filtered_words = for (word in words) 
            if (len(word) > 3) word else null;
        let clean_words = for (word in filtered_words) 
            if (word != null) word else null;
        slice(clean_words, 0, max_count)
    }
    
    fn calculate_readability_score(text) {
        let sentences = len(text.split("."));
        let words = len(text.split(" "));
        let syllables = words * 1.5;  // Simplified syllable estimation
        206.835 - (1.015 * (words / sentences)) - (84.6 * (syllables / words))
    }
    
    fn detect_sentiment(text) {
        let positive_words = ["good", "great", "excellent", "positive", "amazing", "wonderful"];
        let negative_words = ["bad", "terrible", "awful", "negative", "horrible", "disappointing"];
        let words = text.lower().split(" ");
        
        let positive_count = len((for (word in words) 
            (if (word in positive_words) word else null)));
        let negative_count = len((for (word in words) 
            (if (word in negative_words) word else null)));
        
        {
            positive_score: positive_count,
            negative_score: negative_count,
            sentiment: (if (positive_count > negative_count) "positive" 
                       else (if (negative_count > positive_count) "negative" 
                            else "neutral")),
            confidence: abs(positive_count - negative_count) / max(len(words), 1)
        }
    }
    
    let document_analysis = (for (doc in documents)
        let text = doc.text,
        {
            document: doc.path,
            basic_stats: {
                word_count: len(text.split(" ")),
                sentence_count: len(text.split(".")),
                paragraph_count: len(text.split("\n\n")),
                character_count: len(text),
                avg_word_length: len(text) / max(len(text.split(" ")), 1)
            },
            content_analysis: {
                keywords: extract_keywords(text, max_keywords),
                readability_score: calculate_readability_score(text),
                sentiment: detect_sentiment(text),
                complexity: (if (len(text.split(" ")) > 500) "high" 
                           else (if (len(text.split(" ")) > 200) "medium" 
                                else "low"))
            }
        });
    
    let corpus_metrics = {
        total_documents: len(documents),
        total_words: sum((for (analysis in document_analysis) analysis.basic_stats.word_count)),
        avg_readability: avg((for (analysis in document_analysis) analysis.content_analysis.readability_score)),
        avg_sentiment_confidence: avg((for (analysis in document_analysis) analysis.content_analysis.sentiment.confidence))
    };
    
    {
        analysis_timestamp: justnow(),
        corpus_summary: corpus_metrics,
        individual_documents: document_analysis,
        recommendations: {
            readability_improvement: (if (corpus_metrics.avg_readability < 50) 
                                    "Consider simplifying language and shortening sentences" 
                                    else "Readability is acceptable"),
            sentiment_balance: (if (corpus_metrics.avg_sentiment_confidence > 0.5) 
                              "Documents have clear sentiment orientation" 
                              else "Consider more expressive language")
        }
    }
}

// 4. Statistical Data Analysis
pub fn perform_statistical_analysis(dataset) {
    fn validate_dataset(data) {
        {
            is_valid: len(data) > 0,
            sample_size: len(data),
            has_nulls: len((for (item in data) (if (item == null) item else null))) > 0
        }
    }
    
    fn calculate_descriptive_stats(values) {
        let sorted_values = sort(values);
        let n = len(values);
        let mean_val = avg(values);
        
        {
            count: n,
            mean: mean_val,
            median: (if (n % 2 == 0) 
                    (sorted_values[n/2 - 1] + sorted_values[n/2]) / 2 
                    else sorted_values[(n-1)/2]),
            min: min(values),
            max: max(values),
            range: max(values) - min(values),
            std_dev: sqrt(avg((for (val in values) (val - mean_val) * (val - mean_val))))
        }
    }
    
    fn perform_hypothesis_test(sample1, sample2, test_type) {
        (if (test_type == "t_test")
            let mean1 = avg(sample1),
            let mean2 = avg(sample2),
            let diff = abs(mean1 - mean2),
            {
                test_type: "Two-sample t-test",
                mean_difference: diff,
                sample1_mean: mean1,
                sample2_mean: mean2,
                is_significant: diff > 1.0,  // Simplified significance test
                interpretation: (if (diff > 1.0) "Statistically significant difference" 
                               else "No significant difference detected")
            }
        else {
            test_type: "Unknown test",
            error: "Unsupported test type: " + test_type
        })
    }
    
    let validation_results = validate_dataset(dataset);
    
    (if (!validation_results.is_valid)
        {
            success: false,
            error: "Dataset validation failed",
            validation_details: validation_results
        }
    else
        let numeric_values = (for (item in dataset) 
            (if (item is number) item 
             else (if (item.value is number) item.value
                  else 0))),
        let descriptive_stats = calculate_descriptive_stats(numeric_values),
        let sample_size = len(numeric_values),
        let split_point = sample_size / 2,
        let sample1 = slice(numeric_values, 0, split_point),
        let sample2 = slice(numeric_values, split_point, sample_size),
        let t_test_result = perform_hypothesis_test(sample1, sample2, "t_test"),
        {
            analysis_timestamp: justnow(),
            dataset_info: {
                total_rows: len(dataset),
                numeric_values_count: len(numeric_values),
                validation: validation_results
            },
            descriptive_statistics: descriptive_stats,
            hypothesis_testing: t_test_result,
            insights: {
                data_distribution: (if (descriptive_stats.std_dev < descriptive_stats.mean * 0.1) 
                                  "low variability" 
                                  else "high variability"),
                outlier_detection: (if (descriptive_stats.range > 3 * descriptive_stats.std_dev) 
                                  "potential outliers detected" 
                                  else "no obvious outliers")
            },
            success: true
        })
}

// 5. Configuration Management System
pub fn manage_system_configuration(config_files, environment) {
    let configurations = (for (file in config_files) {
        source: file.path,
        format: file.format,
        content: input(file.path, file.format),
        priority: file.priority,
        environment_specific: file.environment == environment
    });
    
    let merged_config = {
        total_configs: len(configurations),
        environment_configs: len((for (config in configurations) 
                                (if (config.environment_specific) config else null))),
        global_configs: len((for (config in configurations) 
                           (if (!config.environment_specific) config else null)))
    };
    
    {
        success: true,
        environment: environment,
        configuration_summary: merged_config,
        metadata: {
            generated_at: justnow(),
            source_files: (for (config in configurations) config.source),
            total_size: len(configurations)
        },
        recommendations: {
            security: (if (environment == "production") 
                      "Ensure sensitive data is encrypted" 
                      else "Development environment detected"),
            maintenance: "Review configuration quarterly for outdated settings"
        }
    }
}

// Test the complex functions with sample data
"Testing complex functions with sample data..."

// Sample test for the data pipeline function
let sample_files = ["./test/input/test.json"];
let pipeline_result = process_multi_format_data(sample_files);
"Data pipeline test result:"
pipeline_result

// Sample portfolio analysis
let sample_holdings = [
    {symbol: "AAPL", shares: 100, current_price: 150.0, cost_basis: 14000.0},
    {symbol: "GOOGL", shares: 50, current_price: 2500.0, cost_basis: 120000.0},
    {symbol: "TSLA", shares: 25, current_price: 800.0, cost_basis: 18000.0}
];
let portfolio_result = analyze_investment_portfolio(sample_holdings);
"Portfolio analysis test result:"
portfolio_result

// Sample document analysis
let sample_docs = ["./test/input/simple.txt"];
let doc_result = analyze_document_corpus(sample_docs, 10);
"Document analysis test result:"
doc_result

// Sample statistical analysis
let sample_data = [
    {value: 23}, {value: 25}, {value: 27}, {value: 31}, {value: 33},
    {value: 29}, {value: 35}, {value: 21}, {value: 37}, {value: 28}
];
let stats_result = perform_statistical_analysis(sample_data);
"Statistical analysis test result:"
stats_result

// Sample configuration management
let sample_configs = [
    {path: "./test/input/config.json", format: "json", priority: 1, environment: "development"}
];
let config_result = manage_system_configuration(sample_configs, "development");
"Configuration management test result:"
config_result

"=== COMPLEX FUNCTION EXAMPLES COMPLETED ==="
