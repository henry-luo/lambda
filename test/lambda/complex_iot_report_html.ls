// Smart City IoT Data Processing and Urban Analytics Platform
// Generates elegant HTML dashboard with comprehensive analytics

fn set_data(data) { data }
fn slice_data(data, start, end) { for (i in start to end-1) data[i] }
fn float_val(val) { val }

// Environmental sensor data processing and analysis
pub fn process_environmental_data(sensor_readings) {
    
    // Data quality assessment and cleaning
    let quality_assessment = for (reading in sensor_readings) (
        let measurements = reading.measurements,
        let quality_flags = [],
        
        // Check for out-of-range values
        let temp_flag = if (measurements.temperature < -40.0 or measurements.temperature > 60.0) "temperature_anomaly" else null,
        let humidity_flag = if (measurements.humidity < 0.0 or measurements.humidity > 100.0) "humidity_anomaly" else null,
        let pm25_flag = if (measurements.air_quality_pm25 < 0.0 or measurements.air_quality_pm25 > 500.0) "pm25_anomaly" else null,
        let noise_flag = if (measurements.noise_level < 0.0 or measurements.noise_level > 120.0) "noise_anomaly" else null,
        
        let all_flags = [temp_flag, humidity_flag, pm25_flag, noise_flag],
        let valid_flags = for (flag in all_flags) if (flag != null) flag else null,
        
        {
            sensor_id: reading.sensor_id,
            timestamp: reading.timestamp,
            quality_score: if (len(valid_flags) == 0) 1.0 
                          else 1.0 - (float_val(len(valid_flags)) / 4.0),
            anomalies: valid_flags,
            is_valid: len(valid_flags) <= 1
        }
    );
    
    let valid_readings = for (i in 0 to len(sensor_readings)-1) 
        if (quality_assessment[i].is_valid) sensor_readings[i] else null;
    
    // Temporal analysis and trending
    let temporal_analysis = {
        time_range: {
            start: if (len(valid_readings) > 0) valid_readings[0].timestamp else null,
            end: if (len(valid_readings) > 0) valid_readings[len(valid_readings)-1].timestamp else null,
            duration_hours: float_val(len(valid_readings))
        },
        
        air_quality_trends: {
            avg_pm25: avg(for (reading in valid_readings) reading.measurements.air_quality_pm25),
            avg_pm10: avg(for (reading in valid_readings) reading.measurements.air_quality_pm10),
            pm25_trend: (
                let half_point = len(valid_readings) / 2,
                let first_half = slice_data(valid_readings, 0, half_point),
                let second_half = slice_data(valid_readings, half_point, len(valid_readings)),
                let first_avg = avg(for (reading in first_half) reading.measurements.air_quality_pm25),
                let second_avg = avg(for (reading in second_half) reading.measurements.air_quality_pm25),
                if (second_avg > first_avg + 5.0) "deteriorating"
                else if (second_avg < first_avg - 5.0) "improving"
                else "stable"
            ),
            air_quality_alerts: for (reading in valid_readings) 
                if (reading.measurements.air_quality_pm25 > 35.0 or reading.measurements.air_quality_pm10 > 50.0) {
                    sensor_id: reading.sensor_id,
                    timestamp: reading.timestamp,
                    location: reading.location,
                    pm25_level: reading.measurements.air_quality_pm25,
                    severity: if (reading.measurements.air_quality_pm25 > 75.0) "hazardous"
                                else if (reading.measurements.air_quality_pm25 > 55.0) "unhealthy"
                                else "moderate"
                }
                else null
        },
        
        climate_analysis: {
            avg_temperature: avg(for (reading in valid_readings) reading.measurements.temperature),
            avg_humidity: avg(for (reading in valid_readings) reading.measurements.humidity),
            heat_index_alerts: for (reading in valid_readings) (
                let temp_f = (reading.measurements.temperature * 9.0 / 5.0) + 32.0,
                let humidity = reading.measurements.humidity,
                let heat_index = temp_f + (humidity / 10.0),
                if (heat_index > 90.0) {
                    sensor_id: reading.sensor_id,
                    timestamp: reading.timestamp,
                    heat_index: heat_index,
                    risk_level: if (heat_index > 105.0) "extreme" else "caution"
                }
                else null
            ),
            comfort_score: (
                let ideal_temp = 22.0,
                let ideal_humidity = 50.0,
                let temp_scores = for (reading in valid_readings) 
                    1.0 - (abs(reading.measurements.temperature - ideal_temp) / 20.0),
                let humidity_scores = for (reading in valid_readings) 
                    1.0 - (abs(reading.measurements.humidity - ideal_humidity) / 50.0),
                (avg(temp_scores) + avg(humidity_scores)) / 2.0
            )
        },
        
        noise_analysis: {
            avg_noise_level: avg(for (reading in valid_readings) reading.measurements.noise_level),
            noise_violations: for (reading in valid_readings) 
                if (reading.measurements.noise_level > 70.0){
                    sensor_id: reading.sensor_id,
                    timestamp: reading.timestamp,
                    noise_level: reading.measurements.noise_level,
                    violation_severity: if (reading.measurements.noise_level > 85.0) "severe"
                                        else "moderate"
                }
                else null,
            quiet_hours_compliance: (
                let total_readings = len(valid_readings),
                let quiet_violations = len(for (reading in valid_readings) 
                    if (reading.measurements.noise_level > 55.0) reading else null),
                if (total_readings > 0) ((float_val(total_readings - quiet_violations)) / float_val(total_readings)) * 100.0 else 100.0
            )
        }
    };
    
    {
        data_quality: {
            total_readings: len(sensor_readings),
            valid_readings: len(valid_readings),
            data_quality_score: avg(for (qa in quality_assessment) qa.quality_score),
            anomaly_summary: {
                temperature_anomalies: len(for (qa in quality_assessment) 
                    if (len(for (anomaly in qa.anomalies) if (anomaly == "temperature_anomaly") anomaly else null) > 0) qa else null),
                humidity_anomalies: len(for (qa in quality_assessment) 
                    if (len(for (anomaly in qa.anomalies) if (anomaly == "humidity_anomaly") anomaly else null) > 0) qa else null),
                air_quality_anomalies: len(for (qa in quality_assessment) 
                    if (len(for (anomaly in qa.anomalies) if (anomaly == "pm25_anomaly") anomaly else null) > 0) qa else null)
            }
        },
        environmental_analysis: temporal_analysis,
        processed_readings: valid_readings
    }
}

// Urban traffic flow analysis and optimization
pub fn analyze_traffic_patterns(traffic_data) {
    
    let flow_analysis = {
        total_intersections: len(set_data(for (data in traffic_data) data.intersection_id)),
        total_vehicle_count: sum(for (data in traffic_data) data.vehicle_count),
        avg_speed_citywide: avg(for (data in traffic_data) data.avg_speed),
        
        congestion_distribution: {
            low_congestion: len(for (data in traffic_data) if (data.congestion_level <= 2) data else null),
            moderate_congestion: len(for (data in traffic_data) if (data.congestion_level >= 3 and data.congestion_level <= 6) data else null),
            high_congestion: len(for (data in traffic_data) if (data.congestion_level >= 7) data else null)
        },
        
        speed_by_zone: {
            downtown_avg: (
                let downtown_readings = for (data in traffic_data) if (data.location.zone == "downtown") data else null,
                if (len(downtown_readings) > 0) avg(for (reading in downtown_readings) reading.avg_speed) else 0.0
            ),
            residential_avg: (
                let residential_readings = for (data in traffic_data) if (data.location.zone == "residential") data else null,
                if (len(residential_readings) > 0) avg(for (reading in residential_readings) reading.avg_speed) else 0.0
            ),
            industrial_avg: (
                let industrial_readings = for (data in traffic_data) if (data.location.zone == "industrial") data else null,
                if (len(industrial_readings) > 0) avg(for (reading in industrial_readings) reading.avg_speed) else 0.0
            )
        }
    };
    
    let safety_analysis = {
        total_incidents: sum(for (data in traffic_data) len(data.incident_reports)),
        incident_hotspots: for (data in traffic_data) 
            if (len(data.incident_reports) >= 2) {
                intersection_id: data.intersection_id,
                location: data.location,
                incident_count: len(data.incident_reports),
                incident_types: data.incident_reports,
                risk_level: if (len(data.incident_reports) >= 5) "high"
                            else if (len(data.incident_reports) >= 3) "medium"
                            else "low"
            }
            else null,
        
        safety_score_by_zone: (
            let zones = ["downtown", "residential", "industrial"],
            for (zone in zones) (
                let zone_data = for (data in traffic_data) if (data.location.zone == zone) data else null,
                let zone_incidents = sum(for (data in zone_data) len(data.incident_reports)),
                let zone_readings = len(zone_data),
                {
                    zone: zone,
                    incident_rate: if (zone_readings > 0) float_val(zone_incidents) / float_val(zone_readings) else 0.0,
                    safety_score: if (zone_readings > 0) max([0.0, 1.0 - (float_val(zone_incidents) / float_val(zone_readings))]) else 1.0
                }
            )
        )
    };
    
    let optimization_recommendations = {
        signal_timing_adjustments: for (data in traffic_data) 
            if (data.congestion_level >= 7 and data.avg_speed < 15.0) {
                {
                    intersection_id: data.intersection_id,
                    current_congestion: data.congestion_level,
                    avg_speed: data.avg_speed,
                    recommendation: "Increase green light duration for main arterial",
                    priority: "high"
                }
            } else null,
        
        route_diversions: for (data in traffic_data) 
            if (data.congestion_level >= 8) {
                {
                    intersection_id: data.intersection_id,
                    location: data.location,
                    recommendation: "Activate dynamic message signs for route diversion",
                    estimated_relief: "15-25% volume reduction"
                }
            } else null,
        
        infrastructure_improvements: for (data in safety_analysis.incident_hotspots) 
            if (data.risk_level == "high") {
                {
                    location: data.intersection_id,
                    issue: "High incident rate: " ++ string(data.incident_count) ++ " incidents",
                    recommendation: "Install additional safety infrastructure (cameras, improved signage)",
                    estimated_cost_category: "medium"
                }
            } else null
    };
    
    {
        traffic_summary: {
            monitoring_coverage: flow_analysis.total_intersections,
            overall_flow_rate: flow_analysis.total_vehicle_count,
            citywide_avg_speed: flow_analysis.avg_speed_citywide,
            congestion_status: if (flow_analysis.congestion_distribution.high_congestion > flow_analysis.congestion_distribution.low_congestion) 
                              "high_congestion_citywide" else "manageable_congestion"
        },
        performance_metrics: {
            flow_analysis: flow_analysis,
            safety_metrics: safety_analysis
        },
        optimization_strategies: optimization_recommendations
    }
}

// Energy consumption and smart grid analytics
pub fn analyze_energy_consumption(energy_data) {
    
    let consumption_analysis = {
        total_consumption: sum(for (data in energy_data) data.consumption_kwh),
        avg_consumption_per_hour: avg(for (data in energy_data) data.consumption_kwh),
        peak_demand_analysis: {
            max_peak_demand: max(for (data in energy_data) data.peak_demand_kw),
            avg_peak_demand: avg(for (data in energy_data) data.peak_demand_kw),
            demand_variability: (
                let mean_demand = avg(for (data in energy_data) data.peak_demand_kw),
                let variance = avg(for (data in energy_data) (data.peak_demand_kw - mean_demand) ^ 2),
                variance ^ 0.5
            )
        },
        
        consumption_by_type: {
            residential_total: sum(for (data in energy_data) 
                if (data.location.building_type == "residential") data.consumption_kwh else 0.0),
            commercial_total: sum(for (data in energy_data) 
                if (data.location.building_type == "commercial") data.consumption_kwh else 0.0),
            industrial_total: sum(for (data in energy_data) 
                if (data.location.building_type == "industrial") data.consumption_kwh else 0.0),
            public_total: sum(for (data in energy_data) 
                if (data.location.building_type == "public") data.consumption_kwh else 0.0)
        },
        
        renewable_analysis: {
            avg_renewable_percentage: avg(for (data in energy_data) data.renewable_percentage),
            renewable_leaders: for (data in energy_data) 
                if (data.renewable_percentage >= 75.0) {
                    {
                        meter_id: data.meter_id,
                        location: data.location,
                        renewable_percentage: data.renewable_percentage,
                        category: "high_renewable"
                    }
                } else null,
            grid_dependency: {
                high_dependency: len(for (data in energy_data) if (data.renewable_percentage < 25.0) data else null),
                medium_dependency: len(for (data in energy_data) if (data.renewable_percentage >= 25.0 and data.renewable_percentage < 50.0) data else null),
                low_dependency: len(for (data in energy_data) if (data.renewable_percentage >= 50.0) data else null)
            }
        }
    };
    
    let grid_analysis = {
        avg_stability_score: avg(for (data in energy_data) data.grid_stability_score),
        stability_issues: for (data in energy_data) 
            if (data.grid_stability_score < 0.8) {
                {
                    meter_id: data.meter_id,
                    location: data.location,
                    stability_score: data.grid_stability_score,
                    issue_severity: if (data.grid_stability_score < 0.6) "critical"
                                   else if (data.grid_stability_score < 0.7) "major"
                                   else "minor"
                }
            } else null,
        
        load_balancing_opportunities: for (data in energy_data) 
            if (data.peak_demand_kw > avg(for (d in energy_data) d.peak_demand_kw) * 1.5) {
                {
                    meter_id: data.meter_id,
                    peak_demand: data.peak_demand_kw,
                    load_shift_potential: (data.peak_demand_kw - avg(for (d in energy_data) d.peak_demand_kw)) * 0.3,
                    recommendation: "Implement demand response programs"
                }
            } else null
    };
    
    let efficiency_recommendations = {
        consumption_optimization: {
            high_consumers: for (data in energy_data) 
                if (data.consumption_kwh > avg(for (d in energy_data) d.consumption_kwh) * 1.8) {
                    {
                        meter_id: data.meter_id,
                        building_type: data.location.building_type,
                        consumption: data.consumption_kwh,
                        savings_potential: data.consumption_kwh * 0.15,
                        priority: "high"
                    }
                } else null,
            
            renewable_expansion: for (data in energy_data) 
                if (data.renewable_percentage < 30.0 and data.location.building_type != "residential") {
                    {
                        meter_id: data.meter_id,
                        current_renewable: data.renewable_percentage,
                        expansion_potential: "Solar installation feasible",
                        estimated_renewable_increase: 40.0
                    }
                } else null
        },
        
        grid_improvements: {
            stability_enhancements: for (issue in grid_analysis.stability_issues) 
                if (issue.issue_severity == "critical" or issue.issue_severity == "major") {
                    {
                        location: issue.location,
                        issue: issue.issue_severity ++ " stability issue",
                        recommendation: "Upgrade grid infrastructure and install battery storage",
                        priority: if (issue.issue_severity == "critical") "immediate" else "high"
                    }
                } else null,
            
            smart_grid_initiatives: [
                "Deploy advanced metering infrastructure (AMI) for real-time monitoring",
                "Implement predictive maintenance for grid components",
                "Establish microgrid pilots in high renewable areas"
            ]
        }
    };
    
    {
        energy_overview: {
            total_consumption_kwh: consumption_analysis.total_consumption,
            avg_renewable_integration: consumption_analysis.renewable_analysis.avg_renewable_percentage,
            grid_reliability_score: grid_analysis.avg_stability_score,
            monitored_meters: len(energy_data)
        },
        consumption_insights: consumption_analysis,
        grid_performance: grid_analysis,
        optimization_roadmap: efficiency_recommendations
    }
}

// HTML generation helper functions
fn format_number(num, decimals) {
    let rounded = round(num * (10.0 ^ decimals)) / (10.0 ^ decimals)
    string(rounded)
}

fn status_badge(status) {
    let badge_class = if (status == "deteriorating" or status == "high_congestion_citywide" or status == "concerning") 
        "badge badge-danger"
    else if (status == "improving" or status == "good" or status == "flowing")
        "badge badge-success"
    else
        "badge badge-warning"
    <span class:badge_class; status>
}

fn progress_bar(value, max_val, color) {
    let percentage = (value / max_val) * 100.0
    let bar_class = "progress-bar bg-" ++ color
    let bar_style = "width: " ++ format_number(percentage, 1) ++ "%"
    let bar_text = format_number(percentage, 1) ++ "%"
    <div class:"progress"
        <div class:bar_class, style:bar_style; bar_text>
    >
}

fn info_item(label_text, value_content) {
    <div class:"info-item"
        <div class:"label"; label_text>
        <div class:"value"; value_content>
    >
}

fn metric_card(label_text, value_text, description_text) {
    <div class:"metric-card"
        <div class:"label"; label_text>
        <div class:"value"; value_text>
        <div class:"description"; description_text>
    >
}

fn alert_box(alert_class, content) {
    <div class:("alert-box " ++ alert_class), style:"margin-top: 15px;"; content>
}

// Sample IoT sensor data
let sample_environmental_sensors = [
    {
        sensor_id: "ENV_001", 
        location: {lat: 40.7589, lon: -73.9851, zone: "downtown"},
        timestamp: t'2024-09-05 08:00:00',
        measurements: {air_quality_pm25: 28.5, air_quality_pm10: 42.3, temperature: 24.2, humidity: 65.0, noise_level: 68.5, light_intensity: 850.0}
    },
    {
        sensor_id: "ENV_002",
        location: {lat: 40.7505, lon: -73.9934, zone: "residential"},
        timestamp: t'2024-09-05 08:00:00',
        measurements: {air_quality_pm25: 15.2, air_quality_pm10: 25.8, temperature: 23.8, humidity: 58.0, noise_level: 45.2, light_intensity: 920.0}
    },
    {
        sensor_id: "ENV_003",
        location: {lat: 40.7614, lon: -73.9776, zone: "industrial"},
        timestamp: t'2024-09-05 08:00:00',
        measurements: {air_quality_pm25: 45.8, air_quality_pm10: 78.2, temperature: 26.1, humidity: 72.0, noise_level: 82.1, light_intensity: 780.0}
    },
    {
        sensor_id: "ENV_004",
        location: {lat: 40.7580, lon: -73.9855, zone: "downtown"},
        timestamp: t'2024-09-05 09:00:00',
        measurements: {air_quality_pm25: 32.1, air_quality_pm10: 48.7, temperature: 25.5, humidity: 63.0, noise_level: 75.3, light_intensity: 1150.0}
    }
];

let sample_traffic_data = [
    {
        intersection_id: 'INT_001',
        location: {lat: 40.7589, lon: -73.9851, zone: "downtown"},
        timestamp: t'2024-09-05 08:00:00',
        vehicle_count: 145,
        avg_speed: 12.5,
        congestion_level: 8,
        incident_reports: ["minor_fender_bender", "pedestrian_jaywalking"]
    },
    {
        intersection_id: 'INT_002',
        location: {lat: 40.7505, lon: -73.9934, zone: "residential"},
        timestamp: t'2024-09-05 08:00:00',
        vehicle_count: 78,
        avg_speed: 25.0,
        congestion_level: 3,
        incident_reports: []
    },
    {
        intersection_id: 'INT_003',
        location: {lat: 40.7614, lon: -73.9776, zone: "industrial"},
        timestamp: t'2024-09-05 08:00:00',
        vehicle_count: 210,
        avg_speed: 18.2,
        congestion_level: 6,
        incident_reports: ["truck_breakdown"]
    }
];

let sample_energy_data = [
    {
        meter_id: "MTR_001",
        location: {zone: "downtown", building_type: "commercial"},
        timestamp: t'2024-09-05 08:00:00',
        consumption_kwh: 125.5,
        peak_demand_kw: 180.2,
        renewable_percentage: 35.0,
        grid_stability_score: 0.92
    },
    {
        meter_id: "MTR_002",
        location: {zone: "residential", building_type: "residential"},
        timestamp: t'2024-09-05 08:00:00',
        consumption_kwh: 45.2,
        peak_demand_kw: 65.8,
        renewable_percentage: 65.0,
        grid_stability_score: 0.88
    },
    {
        meter_id: "MTR_003",
        location: {zone: "industrial", building_type: "industrial"},
        timestamp: t'2024-09-05 08:00:00',
        consumption_kwh: 450.8,
        peak_demand_kw: 680.5,
        renewable_percentage: 20.0,
        grid_stability_score: 0.75
    }
];

// Execute analytics
let environmental_analysis = process_environmental_data(sample_environmental_sensors);
let traffic_analysis = analyze_traffic_patterns(sample_traffic_data);
let energy_analysis = analyze_energy_consumption(sample_energy_data);

// Calculate overall metrics
let overall_livability_score = (environmental_analysis.environmental_analysis.climate_analysis.comfort_score + 
                               (traffic_analysis.traffic_summary.citywide_avg_speed / 30.0) + 
                               (energy_analysis.energy_overview.avg_renewable_integration / 100.0)) / 3.0;


// Generate HTML report
<html lang:"en"
<head
    <meta charset:"UTF-8">
    <meta name:"viewport", content:"width=device-width, initial-scale=1.0">
    <title "Smart City IoT Analytics Dashboard">
    <style
        "* { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background: linear-gradient(135deg, #e3f2fd 0%, #bbdefb 100%);
            padding: 20px;
            line-height: 1.6;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #5c7cfa 0%, #748ffc 100%);
            color: white;
            padding: 40px;
            text-align: center;
        }
        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            font-weight: 700;
        }
        .header p {
            font-size: 1.1em;
            opacity: 0.95;
        }
        .content {
            padding: 40px;
        }
        .overview-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 40px;
        }
        .metric-card {
            background: linear-gradient(135deg, #4299e1 0%, #5a9fd4 100%);
            color: white;
            padding: 25px;
            border-radius: 15px;
            box-shadow: 0 10px 30px rgba(66, 153, 225, 0.3);
            transition: transform 0.3s ease;
        }
        .metric-card:hover {
            transform: translateY(-5px);
        }
        .metric-card .label {
            font-size: 0.9em;
            opacity: 0.9;
            margin-bottom: 5px;
        }
        .metric-card .value {
            font-size: 2.2em;
            font-weight: 700;
            margin-bottom: 5px;
        }
        .metric-card .description {
            font-size: 0.85em;
            opacity: 0.8;
        }
        .section {
            margin-bottom: 40px;
        }
        .section-title {
            font-size: 1.8em;
            color: #333;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 3px solid #4299e1;
        }
        .card {
            background: #f8f9fa;
            border-radius: 12px;
            padding: 25px;
            margin-bottom: 20px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.08);
        }
        .card-title {
            font-size: 1.3em;
            color: #4299e1;
            margin-bottom: 15px;
            font-weight: 600;
        }
        .info-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
        }
        .info-item {
            padding: 12px;
            background: white;
            border-radius: 8px;
            border-left: 4px solid #4299e1;
        }
        .info-item .label {
            font-size: 0.85em;
            color: #666;
            margin-bottom: 3px;
        }
        .info-item .value {
            font-size: 1.2em;
            font-weight: 600;
            color: #333;
        }
        .badge {
            display: inline-block;
            padding: 5px 12px;
            border-radius: 20px;
            font-size: 0.85em;
            font-weight: 600;
            text-transform: uppercase;
        }
        .badge-success { background: #28a745; color: white; }
        .badge-warning { background: #ffc107; color: #333; }
        .badge-danger { background: #dc3545; color: white; }
        .progress {
            height: 30px;
            background: #e9ecef;
            border-radius: 15px;
            overflow: hidden;
            margin: 10px 0;
        }
        .progress-bar {
            height: 100%;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: 600;
            transition: width 0.6s ease;
        }
        .bg-success { background: #28a745; }
        .bg-info { background: #17a2b8; }
        .bg-warning { background: #ffc107; }
        .bg-danger { background: #dc3545; }
        .alert-box {
            padding: 15px 20px;
            border-radius: 8px;
            margin: 10px 0;
            border-left: 5px solid;
        }
        .alert-warning {
            background: #fff3cd;
            border-color: #ffc107;
            color: #856404;
        }
        .alert-danger {
            background: #f8d7da;
            border-color: #dc3545;
            color: #721c24;
        }
        .alert-info {
            background: #d1ecf1;
            border-color: #17a2b8;
            color: #0c5460;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 15px 0;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #dee2e6;
        }
        th {
            background: #4299e1;
            color: white;
            font-weight: 600;
        }
        tr:hover {
            background: #f1f3f5;
        }
        .recommendations {
            background: #ffffff;
            color: #333;
            padding: 30px;
            border-radius: 12px;
            margin-top: 30px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.08);
            border-left: 5px solid #4299e1;
        }
        .recommendations h3 {
            margin-bottom: 20px;
            color: #4299e1;
            font-size: 1.8em;
            font-weight: 700;
        }
        .recommendations h4 {
            color: #2d3748;
            font-weight: 600;
            font-size: 1.2em;
        }
        .recommendations ul {
            list-style: none;
            padding-left: 0;
        }
        .recommendations li {
            padding: 12px 0;
            padding-left: 35px;
            position: relative;
            border-bottom: 1px solid #e2e8f0;
            transition: background 0.2s ease;
        }
        .recommendations li:last-child {
            border-bottom: none;
        }
        .recommendations li:hover {
            background: #f7fafc;
            padding-left: 40px;
        }
        .recommendations li:before {
            content: '▸';
            position: absolute;
            left: 10px;
            font-weight: bold;
            color: #4299e1;
            font-size: 1.2em;
        }
        .priority-badge {
            display: inline-block;
            padding: 3px 10px;
            border-radius: 12px;
            font-size: 0.75em;
            font-weight: 600;
            margin-left: 10px;
            text-transform: uppercase;
        }
        .priority-high { background: #feb2b2; color: #742a2a; }
        .priority-medium { background: #fbd38d; color: #744210; }
        .priority-strategic { background: #bee3f8; color: #2c5282; }
        .footer {
            text-align: center;
            padding: 20px;
            color: #666;
            font-size: 0.9em;
            background: #f8f9fa;
        }"
    >
>
<body
    <div class:"container"
        <div class:"header"
            <h1 "🌆 Smart City IoT Analytics Dashboard">
            <p "Real-time Urban Intelligence & Environmental Monitoring">
            <p style:"margin-top: 10px; font-size: 0.9em;" 
                ("Generated: September 5, 2024 | Data Quality: " ++ format_number(environmental_analysis.data_quality.data_quality_score * 100.0, 1) ++ "%")
            >
        >
        <div class:"content"
            // Key Metrics Overview
            <div class:"overview-grid"
                metric_card("Overall Livability Score", 
                    format_number(overall_livability_score * 100.0, 1) ++ "%",
                    "City health composite index")
                metric_card("Environmental Sensors",
                    string(environmental_analysis.data_quality.total_readings),
                    "Active monitoring stations")
                metric_card("Traffic Intersections",
                    string(traffic_analysis.traffic_summary.monitoring_coverage),
                    "Real-time flow monitoring")
                metric_card("Energy Meters",
                    string(energy_analysis.energy_overview.monitored_meters),
                    "Smart grid integration")
            >

            // Environmental Intelligence
            <div class:"section"
                <h2 class:"section-title"; "🌿 Environmental Intelligence">
                
                <div class:"card"
                    <h3 class:"card-title"; "Air Quality Status">
                    <div class:"info-grid"
                        info_item("PM2.5 Average", format_number(environmental_analysis.environmental_analysis.air_quality_trends.avg_pm25, 1) ++ " µg/m³")
                        info_item("PM10 Average", format_number(environmental_analysis.environmental_analysis.air_quality_trends.avg_pm10, 1) ++ " µg/m³")
                        info_item("Trend", status_badge(environmental_analysis.environmental_analysis.air_quality_trends.pm25_trend))
                        info_item("Active Alerts", string(len(environmental_analysis.environmental_analysis.air_quality_trends.air_quality_alerts)))
                    >
                    if (len(environmental_analysis.environmental_analysis.air_quality_trends.air_quality_alerts) > 0) {
                        alert_box("alert-warning", (
                            <strong "⚠️ Air Quality Alerts Active">,
                            <br>,
                            (string(len(environmental_analysis.environmental_analysis.air_quality_trends.air_quality_alerts)) ++ " location(s) exceeding safe PM2.5 levels. Recommend limiting outdoor activities.")
                        ))
                    }
                >
                <div class:"card"
                    <h3 class:"card-title"; "Climate Comfort Analysis">
                    <div class:"info-grid"
                        info_item("Average Temperature", format_number(environmental_analysis.environmental_analysis.climate_analysis.avg_temperature, 1) ++ "°C")
                        info_item("Average Humidity", format_number(environmental_analysis.environmental_analysis.climate_analysis.avg_humidity, 1) ++ "%")
                        info_item("Comfort Score", format_number(environmental_analysis.environmental_analysis.climate_analysis.comfort_score * 100.0, 1) ++ "%")
                        info_item("Heat Warnings", string(len(environmental_analysis.environmental_analysis.climate_analysis.heat_index_alerts)))
                    >
                    progress_bar(environmental_analysis.environmental_analysis.climate_analysis.comfort_score, 1.0, "success")
                >

                <div class:"card"
                    <h3 class:"card-title"; "Noise Pollution Monitoring">
                    <div class:"info-grid"
                        info_item("Citywide Average", format_number(environmental_analysis.environmental_analysis.noise_analysis.avg_noise_level, 1) ++ " dB")
                        info_item("Violations", string(len(environmental_analysis.environmental_analysis.noise_analysis.noise_violations)))
                        info_item("Quiet Hours Compliance", format_number(environmental_analysis.environmental_analysis.noise_analysis.quiet_hours_compliance, 1) ++ "%")
                    >
                >
            >

            // Traffic & Mobility Intelligence
            <div class:"section"
                <h2 class:"section-title"; "🚗 Traffic & Mobility Intelligence">
                
                <div class:"card"
                    <h3 class:"card-title"; "Traffic Flow Performance">
                    <div class:"info-grid"
                        info_item("Citywide Avg Speed", format_number(traffic_analysis.traffic_summary.citywide_avg_speed, 1) ++ " km/h")
                        info_item("Total Vehicle Count", string(traffic_analysis.traffic_summary.overall_flow_rate))
                        info_item("Congestion Status", status_badge(traffic_analysis.traffic_summary.congestion_status))
                    >
                    
                    <h4 style:"margin-top: 20px; margin-bottom: 10px; color: #4299e1;"; "Congestion Distribution">
                    <div style:"margin: 10px 0;"
                        <div style:"margin-bottom: 10px;"
                            <span style:"display: inline-block; width: 150px;"; "Low Congestion:">
                            progress_bar(float_val(traffic_analysis.performance_metrics.flow_analysis.congestion_distribution.low_congestion), 
                                        float_val(len(sample_traffic_data)), "success")
                        >
                        <div style:"margin-bottom: 10px;"
                            <span style:"display: inline-block; width: 150px;"; "Moderate:">
                            progress_bar(float_val(traffic_analysis.performance_metrics.flow_analysis.congestion_distribution.moderate_congestion), 
                                        float_val(len(sample_traffic_data)), "warning")
                        >
                        <div style:"margin-bottom: 10px;"
                            <span style:"display: inline-block; width: 150px;"; "High Congestion:">
                            progress_bar(float_val(traffic_analysis.performance_metrics.flow_analysis.congestion_distribution.high_congestion), 
                                        float_val(len(sample_traffic_data)), "danger")
                        >
                    >
                >

                <div class:"card"
                    <h3 class:"card-title"; "Speed Analysis by Zone">
                    <table
                        <tr
                            <th "Zone">
                            <th "Average Speed">
                            <th "Status">
                        >
                        <tr
                            <td "Downtown">
                            <td (format_number(traffic_analysis.performance_metrics.flow_analysis.speed_by_zone.downtown_avg, 1) ++ " km/h")>
                            <td 
                                if (traffic_analysis.performance_metrics.flow_analysis.speed_by_zone.downtown_avg < 15.0) {
                                    <span class:"badge badge-danger"; "Congested">
                                } else {
                                    <span class:"badge badge-success"; "Normal">
                                }
                            >
                        >
                        <tr
                            <td "Residential">
                            <td (format_number(traffic_analysis.performance_metrics.flow_analysis.speed_by_zone.residential_avg, 1) ++ " km/h")>
                            <td <span class:"badge badge-success"; "Normal">>
                        >
                        <tr
                            <td "Industrial">
                            <td (format_number(traffic_analysis.performance_metrics.flow_analysis.speed_by_zone.industrial_avg, 1) ++ " km/h")>
                            <td 
                                if (traffic_analysis.performance_metrics.flow_analysis.speed_by_zone.industrial_avg < 20.0) {
                                    <span class:"badge badge-warning"; "Moderate">
                                } else {
                                    <span class:"badge badge-success"; "Normal">
                                }
                            >
                        >
                    >
                >
                if (len(traffic_analysis.optimization_strategies.signal_timing_adjustments) > 0) {
                    alert_box("alert-info", (
                        <strong "🚦 Optimization Opportunity">,
                        <br>,
                        (string(len(traffic_analysis.optimization_strategies.signal_timing_adjustments)) ++ " intersection(s) identified for signal timing optimization to reduce congestion.")
                    ))
                }
            >
            // Energy Intelligence
            <div class:"section"
                <h2 class:"section-title"; "⚡ Energy & Grid Intelligence">
                <div class:"card"
                    <h3 class:"card-title"; "Consumption Overview">
                    <div class:"info-grid"
                        info_item("Total Consumption", format_number(energy_analysis.energy_overview.total_consumption_kwh, 1) ++ " kWh")
                        info_item("Renewable Integration", format_number(energy_analysis.energy_overview.avg_renewable_integration, 1) ++ "%")
                        info_item("Grid Stability", format_number(energy_analysis.energy_overview.grid_reliability_score * 100.0, 1) ++ "%")
                    >
                    
                    <h4 style:"margin-top: 20px; margin-bottom: 10px; color: #4299e1;"; "Renewable Energy Progress">
                    progress_bar(energy_analysis.energy_overview.avg_renewable_integration, 100.0, "info")
                >

                <div class:"card"
                    <h3 class:"card-title"; "Consumption by Building Type">
                    <table
                        <tr
                            <th "Building Type">
                            <th "Consumption (kWh)">
                            <th "Percentage">
                        >
                        <tr
                            <td "Residential">
                            <td format_number(energy_analysis.consumption_insights.consumption_by_type.residential_total, 1)>
                            <td (format_number((energy_analysis.consumption_insights.consumption_by_type.residential_total / 
                                               energy_analysis.energy_overview.total_consumption_kwh) * 100.0, 1) ++ "%")>
                        >
                        <tr
                            <td "Commercial">
                            <td format_number(energy_analysis.consumption_insights.consumption_by_type.commercial_total, 1)>
                            <td (format_number((energy_analysis.consumption_insights.consumption_by_type.commercial_total / 
                                               energy_analysis.energy_overview.total_consumption_kwh) * 100.0, 1) ++ "%")>
                        >
                        <tr
                            <td "Industrial">
                            <td format_number(energy_analysis.consumption_insights.consumption_by_type.industrial_total, 1)>
                            <td (format_number((energy_analysis.consumption_insights.consumption_by_type.industrial_total / 
                                               energy_analysis.energy_overview.total_consumption_kwh) * 100.0, 1) ++ "%")>
                        >
                    >
                >

                <div class:"card"
                    <h3 class:"card-title"; "Grid Dependency Distribution">
                    <div style:"margin: 15px 0;"
                        <div style:"margin-bottom: 10px;"
                            <span style:"display: inline-block; width: 150px;"; "High Dependency:">
                            progress_bar(float_val(energy_analysis.consumption_insights.renewable_analysis.grid_dependency.high_dependency), 
                                        float_val(len(sample_energy_data)), "danger")
                        >
                        <div style:"margin-bottom: 10px;"
                            <span style:"display: inline-block; width: 150px;"; "Medium:">
                            progress_bar(float_val(energy_analysis.consumption_insights.renewable_analysis.grid_dependency.medium_dependency), 
                                        float_val(len(sample_energy_data)), "warning")
                        >
                        <div style:"margin-bottom: 10px;"
                            <span style:"display: inline-block; width: 150px;"; "Low Dependency:">
                            progress_bar(float_val(energy_analysis.consumption_insights.renewable_analysis.grid_dependency.low_dependency), 
                                        float_val(len(sample_energy_data)), "success")
                        >
                    >
                >
            >

            // Strategic Recommendations
            <div class:"recommendations"
                <h3 "🎯 Strategic Recommendations">
                <h4 style:"margin-top: 20px; margin-bottom: 10px;"; "Immediate Priorities">
                <ul
                    if (len(environmental_analysis.environmental_analysis.air_quality_trends.air_quality_alerts) > 0) {
                        <li "⚠️ Air quality alerts active - implement traffic restrictions in affected zones"
                            <span class:"priority-badge priority-high"; "High Priority">
                        >
                    }
                    if (traffic_analysis.traffic_summary.congestion_status == "high_congestion_citywide") {
                        <li "🚦 Activate dynamic traffic management protocols citywide"
                            <span class:"priority-badge priority-high"; "High Priority">
                        >
                    }
                    if (len(energy_analysis.grid_performance.stability_issues) > 0) {
                        <li "⚡ Grid stability issues detected - deploy maintenance teams"
                            <span class:"priority-badge priority-high"; "High Priority">
                        >
                    }
                >
                
                <h4 style:"margin-top: 25px; margin-bottom: 10px;"; "Strategic Initiatives">
                <ul
                    <li "📡 Expand environmental sensor network in industrial zones for better coverage"
                        <span class:"priority-badge priority-strategic"; "Strategic">
                    >
                    <li "🤖 Implement AI-powered traffic signal optimization to reduce congestion by 20-30%"
                        <span class:"priority-badge priority-strategic"; "Strategic">
                    >
                    <li "♻️ Accelerate renewable energy adoption programs - target 60% renewable by 2025"
                        <span class:"priority-badge priority-strategic"; "Strategic">
                    >
                    <li "📱 Deploy citizen engagement platform for real-time feedback and community insights"
                        <span class:"priority-badge priority-medium"; "Medium Priority">
                    >
                    <li "🔋 Establish microgrid pilots in high renewable energy areas"
                        <span class:"priority-badge priority-strategic"; "Strategic">
                    >
                >
                
                <h4 style:"margin-top: 25px; margin-bottom: 10px;"; "Investment Priorities">
                <div style:"background: #f7fafc; padding: 20px; border-radius: 8px; margin-top: 15px;"
                    <table style:"margin: 0;"
                        <tr
                            <th style:"background: #4a5568;"; "Category">
                            <th style:"background: #4a5568;"; "Initiative">
                            <th style:"background: #4a5568;"; "Investment">
                            <th style:"background: #4a5568;"; "ROI Timeline">
                        >
                        <tr
                            <td <strong "🌿 Environmental">>
                            <td "Air quality monitoring expansion">
                            <td "$2.5M">
                            <td "18-24 months">
                        >
                        <tr
                            <td <strong "🚗 Mobility">>
                            <td "Smart intersection upgrades (15 locations)">
                            <td "$8.0M">
                            <td "18-24 months">
                        >
                        <tr
                            <td <strong "⚡ Energy">>
                            <td "Grid modernization & battery storage">
                            <td "$12.0M">
                            <td "24-36 months">
                        >
                        <tr style:"background: #e6fffa; font-weight: 600;"
                            <td colspan:"2"; <strong "Total Investment Required">>
                            <td <strong "$22.5M">>
                            <td <strong "2-3 years">>
                        >
                    >
                >
            >
        >
        
        <div class:"footer"
            <p <strong "Smart City IoT Analytics Platform">>
            <p "Powered by Lambda Script Engine | Real-time Data Processing & Urban Intelligence">
            <p style:"margin-top: 10px; font-size: 0.85em;"
                ("Data sources: " ++ string(environmental_analysis.data_quality.total_readings or 0) ++ " environmental sensors, " ++ 
                string(traffic_analysis.traffic_summary.monitoring_coverage or 0) ++ " traffic intersections, " ++
                string(energy_analysis.energy_overview.monitored_meters or 0) ++ " energy meters")
            >
        >
    >
>
>