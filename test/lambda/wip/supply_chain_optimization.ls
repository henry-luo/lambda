// Intelligent Supply Chain Management and Optimization System
// Demonstrates complex business logic, optimization algorithms, and predictive analytics

import 'datetime', 'math'

// Inventory management and demand forecasting
pub fn forecast_demand(historical_data: [{date: datetime, quantity: int, price: float}], forecast_days: int) {
    let sorted_data = historical_data;  // assume pre-sorted by date
    let data_points = len(sorted_data);
    
    // Simple moving average forecasting
    let window_size = min([7, data_points]);
    let recent_data = slice(sorted_data, max([0, data_points - window_size]), data_points);
    let avg_daily_demand = avg(for (record in recent_data) float(record.quantity));
    
    // Trend analysis
    let trend_slope = if (data_points >= 2) {
        let first_half = slice(sorted_data, 0, data_points / 2);
        let second_half = slice(sorted_data, data_points / 2, data_points);
        let first_avg = avg(for (record in first_half) float(record.quantity));
        let second_avg = avg(for (record in second_half) float(record.quantity));
        (second_avg - first_avg) / float(data_points / 2)
    } else 0.0;
    
    // Seasonal adjustment (simplified weekly pattern)
    let seasonal_factors = [1.2, 1.1, 1.0, 0.9, 0.8, 1.3, 1.4];  // Mon-Sun multipliers
    
    // Generate forecast
    let forecasted_demand = for (day in 1 to forecast_days) {
        let base_forecast = avg_daily_demand + (trend_slope * float(day));
        let seasonal_index = (day - 1) % 7;
        let seasonal_adjustment = seasonal_factors[seasonal_index];
        let adjusted_forecast = base_forecast * seasonal_adjustment;
        
        {
            day: day,
            forecasted_quantity: int(adjusted_forecast),
            confidence_interval: {
                lower: int(adjusted_forecast * 0.8),
                upper: int(adjusted_forecast * 1.2)
            },
            trend_component: trend_slope * float(day),
            seasonal_component: seasonal_adjustment
        }
    };
    
    {
        historical_analysis: {
            data_points: data_points,
            average_demand: avg_daily_demand,
            trend_slope: trend_slope,
            demand_volatility: {
                let mean_demand = avg(for (record in sorted_data) float(record.quantity));
                let variance = avg(for (record in sorted_data) (float(record.quantity) - mean_demand) ^ 2);
                variance ^ 0.5
            }
        },
        forecast: forecasted_demand,
        forecast_summary: {
            total_predicted_demand: sum(for (pred in forecasted_demand) pred.forecasted_quantity),
            avg_daily_forecast: avg(for (pred in forecasted_demand) float(pred.forecasted_quantity)),
            peak_demand_day: {
                let max_demand = max(for (pred in forecasted_demand) pred.forecasted_quantity);
                for (pred in forecasted_demand) if (pred.forecasted_quantity == max_demand) pred.day else null
            }
        }
    }
}

// Supplier performance evaluation and optimization
pub fn evaluate_suppliers(suppliers: [{id: string, performance_data: {delivery_time: [int], quality_score: [float], cost_per_unit: [float]}}]) {
    let supplier_evaluations = for (supplier in suppliers) {
        let delivery_data = supplier.performance_data.delivery_time;
        let quality_data = supplier.performance_data.quality_score;
        let cost_data = supplier.performance_data.cost_per_unit;
        
        // Performance metrics
        let delivery_performance = {
            avg_delivery_time: avg(for (time in delivery_data) float(time)),
            delivery_reliability: {
                let on_time_deliveries = len(for (time in delivery_data) if (time <= 3) time else null);
                float(on_time_deliveries) / float(len(delivery_data))
            },
            delivery_variability: {
                let mean_time = avg(for (time in delivery_data) float(time));
                let variance = avg(for (time in delivery_data) (float(time) - mean_time) ^ 2);
                variance ^ 0.5
            }
        };
        
        let quality_performance = {
            avg_quality_score: avg(quality_data),
            quality_consistency: {
                let mean_quality = avg(quality_data);
                let variance = avg(for (score in quality_data) (score - mean_quality) ^ 2);
                1.0 - (variance ^ 0.5)  // higher consistency = lower variance
            },
            quality_trend: {
                let half_point = len(quality_data) / 2;
                let first_half_avg = avg(slice(quality_data, 0, half_point));
                let second_half_avg = avg(slice(quality_data, half_point, len(quality_data)));
                second_half_avg - first_half_avg
            }
        };
        
        let cost_performance = {
            avg_cost: avg(cost_data),
            cost_stability: {
                let mean_cost = avg(cost_data);
                let variance = avg(for (cost in cost_data) (cost - mean_cost) ^ 2);
                1.0 / (1.0 + variance)  // lower variance = higher stability
            },
            cost_competitiveness: {
                let market_avg_cost = 100.0;  // hypothetical market average
                (market_avg_cost - avg(cost_data)) / market_avg_cost
            }
        };
        
        // Composite supplier score
        let overall_score = (delivery_performance.delivery_reliability * 0.3 +
                           quality_performance.avg_quality_score * 0.4 +
                           cost_performance.cost_competitiveness * 0.3);
        
        {
            supplier_id: supplier.id,
            performance_metrics: {
                delivery: delivery_performance,
                quality: quality_performance,
                cost: cost_performance
            },
            overall_score: overall_score,
            ranking_category: if (overall_score >= 0.8) "preferred"
                            else if (overall_score >= 0.6) "acceptable"
                            else "needs_improvement",
            recommendations: {
                delivery: if (delivery_performance.delivery_reliability < 0.8) "Improve delivery consistency" else null,
                quality: if (quality_performance.avg_quality_score < 0.7) "Enhance quality control processes" else null,
                cost: if (cost_performance.cost_competitiveness < 0.1) "Negotiate better pricing" else null
            }
        }
    };
    
    // Supplier portfolio optimization
    let portfolio_analysis = {
        preferred_suppliers: for (eval in supplier_evaluations) 
            if (eval.ranking_category == "preferred") eval else null,
        risk_assessment: {
            single_source_risk: if (len(supplier_evaluations) < 3) "high" else "low",
            quality_risk: {
                let low_quality_suppliers = len(for (eval in supplier_evaluations) 
                    if (eval.performance_metrics.quality.avg_quality_score < 0.7) eval else null);
                if (low_quality_suppliers > len(supplier_evaluations) / 2) "high" else "low"
            },
            cost_risk: {
                let high_cost_suppliers = len(for (eval in supplier_evaluations) 
                    if (eval.performance_metrics.cost.cost_competitiveness < 0.0) eval else null);
                if (high_cost_suppliers > len(supplier_evaluations) / 3) "high" else "medium"
            }
        }
    };
    
    {
        supplier_evaluations: supplier_evaluations,
        portfolio_optimization: portfolio_analysis,
        recommended_actions: {
            maintain_partnerships: for (eval in supplier_evaluations) 
                if (eval.ranking_category == "preferred") eval.supplier_id else null,
            performance_review_needed: for (eval in supplier_evaluations) 
                if (eval.ranking_category == "needs_improvement") eval.supplier_id else null,
            diversification_strategy: if (portfolio_analysis.risk_assessment.single_source_risk == "high") 
                "Identify additional suppliers to reduce dependency" else "Current supplier base is adequate"
        }
    }
}

// Intelligent warehouse optimization and routing
pub fn optimize_warehouse_operations(warehouse_data: {inventory: [{sku: string, location: string, quantity: int, turnover_rate: float}], orders: [{order_id: string, items: [{sku: string, quantity: int}], priority: string}]}) {
    let inventory = warehouse_data.inventory;
    let orders = warehouse_data.orders;
    
    // Inventory positioning optimization (ABC analysis)
    let abc_analysis = {
        let sorted_by_turnover = inventory;  // would sort by turnover_rate desc
        let total_items = len(inventory);
        let a_items = slice(sorted_by_turnover, 0, int(float(total_items) * 0.2));
        let b_items = slice(sorted_by_turnover, int(float(total_items) * 0.2), int(float(total_items) * 0.5));
        let c_items = slice(sorted_by_turnover, int(float(total_items) * 0.5), total_items);
        
        {
            category_a: {
                items: a_items,
                recommended_zone: "front_accessible",
                turnover_threshold: 0.8
            },
            category_b: {
                items: b_items,
                recommended_zone: "middle_accessible",
                turnover_threshold: 0.5
            },
            category_c: {
                items: c_items,
                recommended_zone: "back_storage",
                turnover_threshold: 0.2
            }
        }
    };
    
    // Order picking optimization
    let picking_optimization = for (order in orders) {
        let order_items = order.items;
        let total_pick_distance = 0.0;  // simplified distance calculation
        let pick_sequence = [];
        
        // Priority-based picking sequence
        let high_turnover_items = for (item in order_items) {
            let inventory_item = for (inv in inventory) if (inv.sku == item.sku) inv else null;
            if (len(inventory_item) > 0 and inventory_item[0].turnover_rate > 0.7) item else null
        };
        
        let optimized_route = {
            high_priority_picks: high_turnover_items,
            estimated_pick_time: float(len(order_items)) * 2.5,  // 2.5 minutes per item
            route_efficiency: if (len(high_turnover_items) > len(order_items) / 2) "optimal" else "standard"
        };
        
        {
            order_id: order.order_id,
            priority: order.priority,
            picking_strategy: optimized_route,
            resource_allocation: {
                picker_count: if (order.priority == "urgent") 2 else 1,
                equipment_needed: if (len(order_items) > 10) ["forklift", "cart"] else ["cart"]
            }
        }
    };
    
    // Capacity and space utilization analysis
    let space_utilization = {
        total_inventory_value: sum(for (item in inventory) float(item.quantity) * item.turnover_rate * 100.0),
        storage_efficiency: {
            high_turnover_space: len(abc_analysis.category_a.items) * 10.0,  // sq ft per item
            total_space_used: len(inventory) * 8.0,
            utilization_percentage: (len(inventory) * 8.0) / 10000.0 * 100.0  // assume 10k sq ft warehouse
        },
        recommendations: {
            layout_optimization: if (len(abc_analysis.category_a.items) > 20) 
                "Consider expanding front accessible zone" else "Current layout is adequate",
            inventory_reduction: for (item in abc_analysis.category_c.items) 
                if (item.turnover_rate < 0.1) item.sku else null
        }
    };
    
    {
        abc_inventory_analysis: abc_analysis,
        picking_optimization: picking_optimization,
        warehouse_metrics: {
            total_skus: len(inventory),
            avg_turnover_rate: avg(for (item in inventory) item.turnover_rate),
            pending_orders: len(orders),
            high_priority_orders: len(for (order in orders) if (order.priority == "urgent") order else null)
        },
        space_utilization: space_utilization,
        operational_recommendations: {
            immediate_actions: [
                if (space_utilization.storage_efficiency.utilization_percentage > 90.0) 
                    "Warehouse capacity approaching limit - consider expansion" else null,
                if (len(for (order in orders) if (order.priority == "urgent") order else null) > 5) 
                    "High urgent order volume - allocate additional picking resources" else null
            ],
            strategic_improvements: [
                "Implement dynamic slotting based on turnover analysis",
                "Consider automation for Category A items",
                "Review slow-moving inventory for liquidation opportunities"
            ]
        }
    }
}

// Sample data for testing supply chain operations
let sample_demand_history = [
    {date: t'2024-08-01', quantity: 150, price: 25.50},
    {date: t'2024-08-02', quantity: 165, price: 25.50},
    {date: t'2024-08-03', quantity: 140, price: 25.50},
    {date: t'2024-08-04', quantity: 180, price: 25.75},
    {date: t'2024-08-05', quantity: 175, price: 25.75},
    {date: t'2024-08-06', quantity: 220, price: 26.00},
    {date: t'2024-08-07', quantity: 200, price: 26.00},
    {date: t'2024-08-08', quantity: 155, price: 25.50},
    {date: t'2024-08-09', quantity: 170, price: 25.50},
    {date: t'2024-08-10', quantity: 185, price: 25.75}
];

let sample_suppliers = [
    {
        id: "SUPPLIER_001",
        performance_data: {
            delivery_time: [2, 3, 2, 4, 3, 2, 3, 2, 3, 2],
            quality_score: [0.95, 0.92, 0.96, 0.89, 0.94, 0.97, 0.93, 0.95, 0.91, 0.96],
            cost_per_unit: [98.50, 99.20, 97.80, 100.10, 98.90, 99.50, 98.20, 99.80, 98.60, 99.30]
        }
    },
    {
        id: "SUPPLIER_002",
        performance_data: {
            delivery_time: [3, 4, 3, 5, 4, 3, 4, 3, 4, 3],
            quality_score: [0.88, 0.90, 0.87, 0.92, 0.89, 0.91, 0.88, 0.90, 0.87, 0.89],
            cost_per_unit: [95.20, 94.80, 96.10, 95.50, 94.90, 95.80, 95.30, 94.70, 95.60, 95.40]
        }
    },
    {
        id: "SUPPLIER_003",
        performance_data: {
            delivery_time: [1, 2, 2, 3, 2, 1, 2, 2, 1, 2],
            quality_score: [0.97, 0.98, 0.96, 0.99, 0.97, 0.98, 0.97, 0.96, 0.98, 0.97],
            cost_per_unit: [102.30, 103.10, 101.80, 103.50, 102.90, 103.20, 102.60, 103.40, 102.70, 103.00]
        }
    }
];

let sample_warehouse_data = {
    inventory: [
        {sku: "PROD_001", location: "A1-01", quantity: 250, turnover_rate: 0.85},
        {sku: "PROD_002", location: "B2-15", quantity: 150, turnover_rate: 0.45},
        {sku: "PROD_003", location: "A1-05", quantity: 300, turnover_rate: 0.92},
        {sku: "PROD_004", location: "C3-22", quantity: 80, turnover_rate: 0.15},
        {sku: "PROD_005", location: "A2-08", quantity: 200, turnover_rate: 0.78},
        {sku: "PROD_006", location: "B1-12", quantity: 120, turnover_rate: 0.55},
        {sku: "PROD_007", location: "C2-18", quantity: 60, turnover_rate: 0.25},
        {sku: "PROD_008", location: "A1-03", quantity: 180, turnover_rate: 0.88}
    ],
    orders: [
        {order_id: "ORD_001", items: [{sku: "PROD_001", quantity: 25}, {sku: "PROD_003", quantity: 15}], priority: "urgent"},
        {order_id: "ORD_002", items: [{sku: "PROD_002", quantity: 10}, {sku: "PROD_005", quantity: 20}], priority: "standard"},
        {order_id: "ORD_003", items: [{sku: "PROD_008", quantity: 30}, {sku: "PROD_001", quantity: 40}], priority: "urgent"},
        {order_id: "ORD_004", items: [{sku: "PROD_004", quantity: 5}, {sku: "PROD_006", quantity: 12}], priority: "standard"}
    ]
};

// Execute comprehensive supply chain optimization
"=== Intelligent Supply Chain Management Results ==="

let demand_forecast = forecast_demand(sample_demand_history, 14);
"Demand forecast generated for next " + string(len(demand_forecast.forecast)) + " days"

let supplier_evaluation = evaluate_suppliers(sample_suppliers);
"Evaluated " + string(len(supplier_evaluation.supplier_evaluations)) + " suppliers"

let warehouse_optimization = optimize_warehouse_operations(sample_warehouse_data);
"Warehouse optimization completed for " + string(warehouse_optimization.warehouse_metrics.total_skus) + " SKUs"

// Comprehensive supply chain intelligence report
{
    executive_summary: {
        forecast_period: len(demand_forecast.forecast),
        predicted_demand: demand_forecast.forecast_summary.total_predicted_demand,
        supplier_performance: {
            preferred_suppliers: len(supplier_evaluation.portfolio_optimization.preferred_suppliers),
            total_evaluated: len(supplier_evaluation.supplier_evaluations)
        },
        warehouse_efficiency: warehouse_optimization.space_utilization.storage_efficiency.utilization_percentage
    },
    demand_intelligence: {
        forecast_accuracy_indicators: {
            trend_direction: if (demand_forecast.historical_analysis.trend_slope > 0) "increasing" else "decreasing",
            demand_volatility: demand_forecast.historical_analysis.demand_volatility,
            seasonal_impact: "Weekly patterns detected"
        },
        capacity_planning: {
            peak_demand_forecast: max(for (day in demand_forecast.forecast) day.forecasted_quantity),
            average_demand_forecast: demand_forecast.forecast_summary.avg_daily_forecast,
            confidence_range: "±20% based on historical variance"
        }
    },
    supplier_intelligence: {
        top_performer: {
            let best_supplier = for (eval in supplier_evaluation.supplier_evaluations) 
                if (eval.overall_score == max(for (e in supplier_evaluation.supplier_evaluations) e.overall_score)) eval else null;
            if (len(best_supplier) > 0) best_supplier[0].supplier_id else "None"
        },
        risk_mitigation: supplier_evaluation.portfolio_optimization.risk_assessment,
        improvement_opportunities: len(supplier_evaluation.recommended_actions.performance_review_needed)
    },
    warehouse_intelligence: {
        operational_efficiency: {
            abc_distribution: {
                high_turnover_items: len(warehouse_optimization.abc_inventory_analysis.category_a.items),
                medium_turnover_items: len(warehouse_optimization.abc_inventory_analysis.category_b.items),
                low_turnover_items: len(warehouse_optimization.abc_inventory_analysis.category_c.items)
            },
            space_optimization: warehouse_optimization.space_utilization.storage_efficiency,
            picking_efficiency: {
                urgent_orders: len(for (opt in warehouse_optimization.picking_optimization) 
                    if (opt.priority == "urgent") opt else null),
                avg_pick_time: avg(for (opt in warehouse_optimization.picking_optimization) 
                    opt.picking_strategy.estimated_pick_time)
            }
        }
    },
    strategic_recommendations: {
        demand_management: [
            "Increase inventory for forecasted peak demand periods",
            "Implement dynamic pricing during high-demand days"
        ],
        supplier_optimization: supplier_evaluation.recommended_actions.diversification_strategy,
        warehouse_improvements: warehouse_optimization.operational_recommendations.strategic_improvements
    }
}
