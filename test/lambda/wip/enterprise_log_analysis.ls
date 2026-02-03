// Enterprise Log Analysis System
// Demonstrates advanced text processing, pattern matching, and security analysis

import 'regex', 'datetime'

// Log parsing and classification
pub fn parse_log_entry(log_line: string) {
    // Extract common log format components
    let timestamp_pattern = "\\d{4}-\\d{2}-\\d{2}\\s+\\d{2}:\\d{2}:\\d{2}";
    let ip_pattern = "\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b";
    let http_status_pattern = "\\s(\\d{3})\\s";
    let user_agent_pattern = "\"([^\"]*User-Agent[^\"]*?)\"";
    
    // Simulate pattern extraction (in real Lambda, would use regex functions)
    let components = {
        timestamp: if (contains(log_line, "2024-")) 
            substring(log_line, 0, 19) else null,
        ip_address: if (contains(log_line, ".")) {
            // Simplified IP extraction
            let parts = string(log_line);  // would split on whitespace
            "192.168.1.100"  // placeholder for extracted IP
        } else null,
        http_status: if (contains(log_line, " 200 ")) 200
                    else if (contains(log_line, " 404 ")) 404
                    else if (contains(log_line, " 500 ")) 500
                    else if (contains(log_line, " 403 ")) 403
                    else 0,
        method: if (contains(log_line, "GET")) "GET"
               else if (contains(log_line, "POST")) "POST"
               else if (contains(log_line, "PUT")) "PUT"
               else if (contains(log_line, "DELETE")) "DELETE"
               else "UNKNOWN",
        user_agent: if (contains(log_line, "Mozilla")) "Mozilla-based"
                   else if (contains(log_line, "bot")) "Bot"
                   else "Other",
        request_size: if (contains(log_line, " ")) 
            int(1024 + int(string(len(log_line)) + "0")) else 0  // simulated
    };
    
    {
        raw_line: log_line,
        parsed: components,
        classification: classify_log_entry(components)
    }
}

// Security classification of log entries
pub fn classify_log_entry(components) {
    let risk_score = 0;
    let alerts = [];
    let category = "normal";
    
    // Analyze HTTP status codes
    let status_risk = if (components.http_status >= 400 and components.http_status < 500) 2
                     else if (components.http_status >= 500) 3
                     else 0;
    
    // Analyze request patterns
    let method_risk = if (components.method == "DELETE") 2
                     else if (components.method == "PUT") 1
                     else 0;
    
    // Check for bot activity
    let bot_risk = if (components.user_agent == "Bot") 1 else 0;
    
    // Calculate total risk
    let total_risk = status_risk + method_risk + bot_risk;
    
    let final_category = if (total_risk >= 4) "high_risk"
                        else if (total_risk >= 2) "medium_risk"
                        else "normal";
    
    let alert_messages = if (total_risk >= 3) ["Security alert: High risk activity detected"]
                        else if (components.http_status >= 500) ["Server error detected"]
                        else if (components.http_status == 404) ["Not found error"]
                        else [];
    
    {
        risk_score: total_risk,
        category: final_category,
        alerts: alert_messages,
        details: {
            status_code_risk: status_risk,
            method_risk: method_risk,
            bot_activity: bot_risk
        }
    }
}

// Advanced log aggregation and analytics
pub fn analyze_log_batch(log_entries) {
    let parsed_logs = for (entry in log_entries) parse_log_entry(entry);
    
    // Traffic analysis
    let traffic_stats = {
        total_requests: len(parsed_logs),
        unique_ips: len(set(for (log in parsed_logs) log.parsed.ip_address)),
        status_distribution: {
            success_2xx: len(for (log in parsed_logs) 
                if (log.parsed.http_status >= 200 and log.parsed.http_status < 300) log else null),
            client_error_4xx: len(for (log in parsed_logs) 
                if (log.parsed.http_status >= 400 and log.parsed.http_status < 500) log else null),
            server_error_5xx: len(for (log in parsed_logs) 
                if (log.parsed.http_status >= 500) log else null)
        },
        method_distribution: {
            get_count: len(for (log in parsed_logs) if (log.parsed.method == "GET") log else null),
            post_count: len(for (log in parsed_logs) if (log.parsed.method == "POST") log else null),
            put_count: len(for (log in parsed_logs) if (log.parsed.method == "PUT") log else null),
            delete_count: len(for (log in parsed_logs) if (log.parsed.method == "DELETE") log else null)
        }
    };
    
    // Security analysis
    let security_analysis = {
        high_risk_events: for (log in parsed_logs) 
            if (log.classification.category == "high_risk") log else null,
        bot_traffic: for (log in parsed_logs) 
            if (log.parsed.user_agent == "Bot") log else null,
        error_patterns: for (log in parsed_logs) 
            if (log.parsed.http_status >= 400) {
                {
                    timestamp: log.parsed.timestamp,
                    status: log.parsed.http_status,
                    ip: log.parsed.ip_address,
                    method: log.parsed.method
                }
            } else null,
        total_alerts: sum(for (log in parsed_logs) len(log.classification.alerts))
    };
    
    // Performance metrics
    let performance_metrics = {
        avg_request_size: avg(for (log in parsed_logs) float(log.parsed.request_size)),
        total_bandwidth: sum(for (log in parsed_logs) log.parsed.request_size),
        error_rate: {
            let total = len(parsed_logs);
            let errors = len(for (log in parsed_logs) 
                if (log.parsed.http_status >= 400) log else null);
            if (total > 0) (float(errors) / float(total)) * 100.0 else 0.0
        }
    };
    
    // Time-based analysis (simplified)
    let temporal_analysis = {
        time_range: {
            start: if (len(parsed_logs) > 0) parsed_logs[0].parsed.timestamp else null,
            end: if (len(parsed_logs) > 0) parsed_logs[len(parsed_logs)-1].parsed.timestamp else null
        },
        requests_per_minute: {
            let duration_minutes = if (len(parsed_logs) > 1) 10.0 else 1.0;  // simplified
            float(len(parsed_logs)) / duration_minutes
        }
    };
    
    {
        summary: {
            processed_entries: len(parsed_logs),
            analysis_timestamp: justnow(),
            overall_health: if (security_analysis.total_alerts > 5) "critical"
                          else if (performance_metrics.error_rate > 10.0) "warning"
                          else "healthy"
        },
        traffic_analysis: traffic_stats,
        security_report: security_analysis,
        performance_data: performance_metrics,
        temporal_patterns: temporal_analysis,
        sample_alerts: for (log in parsed_logs, alert in log.classification.alerts) 
            {
                timestamp: log.parsed.timestamp,
                ip: log.parsed.ip_address,
                message: alert,
                risk_level: log.classification.category
            }
    }
}

// Network security monitoring
pub fn detect_anomalies(log_analysis) {
    let anomalies = [];
    let severity_levels = [];
    
    // Check for suspicious patterns
    let traffic_data = log_analysis.traffic_analysis;
    let security_data = log_analysis.security_report;
    let performance_data = log_analysis.performance_data;
    
    // Traffic volume anomalies
    let high_traffic_threshold = 1000.0;
    let traffic_anomaly = if (traffic_data.total_requests > high_traffic_threshold) {
        {
            type: "high_traffic_volume",
            severity: "medium",
            description: "Unusually high traffic volume detected: " + string(traffic_data.total_requests) + " requests",
            recommendation: "Monitor for potential DDoS attack"
        }
    } else null;
    
    // Error rate anomalies
    let error_rate_threshold = 15.0;
    let error_anomaly = if (performance_data.error_rate > error_rate_threshold) {
        {
            type: "high_error_rate",
            severity: "high",
            description: "Error rate exceeds threshold: " + string(performance_data.error_rate) + "%",
            recommendation: "Investigate server health and application issues"
        }
    } else null;
    
    // Security anomalies
    let security_anomaly = if (len(security_data.high_risk_events) > 3) {
        {
            type: "security_threats",
            severity: "critical",
            description: "Multiple high-risk security events detected: " + string(len(security_data.high_risk_events)),
            recommendation: "Immediate security review required"
        }
    } else null;
    
    // Bot traffic anomalies
    let bot_percentage = (float(len(security_data.bot_traffic)) / float(traffic_data.total_requests)) * 100.0;
    let bot_anomaly = if (bot_percentage > 30.0) {
        {
            type: "excessive_bot_traffic",
            severity: "medium",
            description: "High percentage of bot traffic: " + string(bot_percentage) + "%",
            recommendation: "Review bot filtering and rate limiting"
        }
    } else null;
    
    let detected_anomalies = for (anomaly in [traffic_anomaly, error_anomaly, security_anomaly, bot_anomaly])
        if (anomaly != null) anomaly else null;
    
    {
        anomaly_count: len(detected_anomalies),
        detected_anomalies: detected_anomalies,
        overall_threat_level: if (len(for (a in detected_anomalies) if (a.severity == "critical") a else null) > 0) "critical"
                            else if (len(for (a in detected_anomalies) if (a.severity == "high") a else null) > 0) "high"
                            else if (len(detected_anomalies) > 0) "medium"
                            else "low",
        recommendations: for (anomaly in detected_anomalies) anomaly.recommendation
    }
}

// Sample log data for testing
let sample_log_entries = [
    "2024-09-05 10:15:23 192.168.1.100 GET /api/users 200 1024 \"Mozilla/5.0 (Windows NT 10.0; Win64; x64)\"",
    "2024-09-05 10:15:24 192.168.1.101 POST /api/login 401 512 \"Mozilla/5.0 (Windows NT 10.0; Win64; x64)\"",
    "2024-09-05 10:15:25 192.168.1.102 GET /admin/panel 403 256 \"Mozilla/5.0 (compatible; Googlebot/2.1)\"",
    "2024-09-05 10:15:26 192.168.1.103 DELETE /api/data 500 128 \"curl/7.68.0\"",
    "2024-09-05 10:15:27 192.168.1.104 GET /api/products 200 2048 \"Mozilla/5.0 (Macintosh; Intel Mac OS X)\"",
    "2024-09-05 10:15:28 192.168.1.105 POST /api/upload 413 64 \"PostmanRuntime/7.26.8\"",
    "2024-09-05 10:15:29 192.168.1.106 GET /robots.txt 404 32 \"Mozilla/5.0 (compatible; bingbot/2.0)\"",
    "2024-09-05 10:15:30 192.168.1.107 PUT /api/settings 500 256 \"Mozilla/5.0 (X11; Linux x86_64)\"",
    "2024-09-05 10:15:31 192.168.1.108 GET /api/status 200 128 \"HealthCheck/1.0\"",
    "2024-09-05 10:15:32 192.168.1.109 DELETE /api/cache 200 64 \"AdminTool/2.1\""
];

// Execute comprehensive log analysis
"=== Enterprise Log Analysis Results ==="

let log_analysis_results = analyze_log_batch(sample_log_entries);
"Processed " + string(log_analysis_results.summary.processed_entries) + " log entries"

let anomaly_detection_results = detect_anomalies(log_analysis_results);
"Detected " + string(anomaly_detection_results.anomaly_count) + " anomalies"

// Final comprehensive security report
{
    analysis_summary: log_analysis_results.summary,
    traffic_overview: {
        total_requests: log_analysis_results.traffic_analysis.total_requests,
        error_rate: log_analysis_results.performance_data.error_rate,
        unique_clients: log_analysis_results.traffic_analysis.unique_ips
    },
    security_assessment: {
        threat_level: anomaly_detection_results.overall_threat_level,
        high_risk_events: len(log_analysis_results.security_report.high_risk_events),
        total_alerts: log_analysis_results.security_report.total_alerts,
        bot_traffic_percentage: (float(len(log_analysis_results.security_report.bot_traffic)) / 
                               float(log_analysis_results.traffic_analysis.total_requests)) * 100.0
    },
    performance_metrics: log_analysis_results.performance_data,
    detected_anomalies: anomaly_detection_results.detected_anomalies,
    recommendations: anomaly_detection_results.recommendations
}
