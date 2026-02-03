// Healthcare Analytics and Patient Management System
// Demonstrates medical data processing, risk assessment, and clinical decision support

import 'statistics', 'datetime'

// Patient risk stratification and health analytics
pub fn assess_patient_risk(patient_data: {id: string, demographics: {age: int, gender: string, bmi: float}, vitals: [{date: datetime, systolic: int, diastolic: int, heart_rate: int, temperature: float}], conditions: [string], medications: [string], lab_results: [{test: string, value: float, reference_range: {min: float, max: float}, date: datetime}]}) {
    
    // Demographic risk factors
    let demographic_risk = {
        age_risk: if (patient_data.demographics.age >= 65) 2
                 else if (patient_data.demographics.age >= 45) 1
                 else 0,
        bmi_risk: if (patient_data.demographics.bmi >= 30.0) 2
                 else if (patient_data.demographics.bmi >= 25.0) 1
                 else 0,
        gender_risk: if (patient_data.demographics.gender == "male" and patient_data.demographics.age > 45) 1 else 0
    };
    
    // Vital signs analysis
    let vitals_analysis = if (len(patient_data.vitals) > 0) {
        let recent_vitals = patient_data.vitals[len(patient_data.vitals)-1];  // most recent
        let systolic_readings = for (vital in patient_data.vitals) vital.systolic;
        let diastolic_readings = for (vital in patient_data.vitals) vital.diastolic;
        let hr_readings = for (vital in patient_data.vitals) vital.heart_rate;
        
        {
            avg_systolic: avg(for (reading in systolic_readings) float(reading)),
            avg_diastolic: avg(for (reading in diastolic_readings) float(reading)),
            avg_heart_rate: avg(for (reading in hr_readings) float(reading)),
            blood_pressure_risk: {
                let avg_sys = avg(for (reading in systolic_readings) float(reading));
                let avg_dia = avg(for (reading in diastolic_readings) float(reading));
                if (avg_sys >= 140.0 or avg_dia >= 90.0) 3
                else if (avg_sys >= 130.0 or avg_dia >= 80.0) 2
                else if (avg_sys >= 120.0) 1
                else 0
            },
            heart_rate_risk: {
                let avg_hr = avg(for (reading in hr_readings) float(reading));
                if (avg_hr > 100.0 or avg_hr < 60.0) 2
                else if (avg_hr > 90.0 or avg_hr < 65.0) 1
                else 0
            },
            vitals_trend: {
                let recent_avg_sys = if (len(systolic_readings) >= 3) 
                    avg(slice(systolic_readings, len(systolic_readings)-3, len(systolic_readings))) 
                    else avg(for (reading in systolic_readings) float(reading));
                let earlier_avg_sys = if (len(systolic_readings) >= 6) 
                    avg(slice(systolic_readings, 0, 3)) 
                    else avg(for (reading in systolic_readings) float(reading));
                if (recent_avg_sys > earlier_avg_sys + 10.0) "worsening"
                else if (recent_avg_sys < earlier_avg_sys - 10.0) "improving"
                else "stable"
            }
        }
    } else {
        {
            avg_systolic: 0.0,
            avg_diastolic: 0.0,
            avg_heart_rate: 0.0,
            blood_pressure_risk: 0,
            heart_rate_risk: 0,
            vitals_trend: "no_data"
        }
    };
    
    // Chronic conditions risk assessment
    let condition_risk = {
        high_risk_conditions: for (condition in patient_data.conditions) 
            if (condition == "diabetes" or condition == "hypertension" or condition == "heart_disease" or condition == "copd") condition else null,
        condition_count: len(patient_data.conditions),
        comorbidity_risk: if (len(patient_data.conditions) >= 3) 3
                         else if (len(patient_data.conditions) >= 2) 2
                         else if (len(patient_data.conditions) >= 1) 1
                         else 0
    };
    
    // Lab results analysis
    let lab_analysis = if (len(patient_data.lab_results) > 0) {
        let abnormal_results = for (lab in patient_data.lab_results) 
            if (lab.value < lab.reference_range.min or lab.value > lab.reference_range.max) lab else null;
        
        let critical_labs = for (lab in abnormal_results) {
            let deviation_percentage = if (lab.value > lab.reference_range.max) 
                ((lab.value - lab.reference_range.max) / lab.reference_range.max) * 100.0
                else ((lab.reference_range.min - lab.value) / lab.reference_range.min) * 100.0;
            
            if (deviation_percentage > 50.0) {
                {test: lab.test, severity: "critical", deviation: deviation_percentage}
            } else if (deviation_percentage > 20.0) {
                {test: lab.test, severity: "moderate", deviation: deviation_percentage}
            } else {
                {test: lab.test, severity: "mild", deviation: deviation_percentage}
            }
        };
        
        {
            total_labs: len(patient_data.lab_results),
            abnormal_count: len(abnormal_results),
            critical_results: for (lab in critical_labs) if (lab.severity == "critical") lab else null,
            lab_risk_score: len(for (lab in critical_labs) if (lab.severity == "critical") lab else null) * 3 +
                           len(for (lab in critical_labs) if (lab.severity == "moderate") lab else null) * 2 +
                           len(for (lab in critical_labs) if (lab.severity == "mild") lab else null) * 1
        }
    } else {
        {
            total_labs: 0,
            abnormal_count: 0,
            critical_results: [],
            lab_risk_score: 0
        }
    };
    
    // Medication interaction and polypharmacy risk
    let medication_risk = {
        medication_count: len(patient_data.medications),
        polypharmacy_risk: if (len(patient_data.medications) >= 10) 3
                          else if (len(patient_data.medications) >= 5) 2
                          else if (len(patient_data.medications) >= 3) 1
                          else 0,
        high_risk_medications: for (med in patient_data.medications) 
            if (contains(med, "warfarin") or contains(med, "insulin") or contains(med, "digoxin")) med else null
    };
    
    // Calculate composite risk score
    let total_risk_score = demographic_risk.age_risk + demographic_risk.bmi_risk + demographic_risk.gender_risk +
                          vitals_analysis.blood_pressure_risk + vitals_analysis.heart_rate_risk +
                          condition_risk.comorbidity_risk + lab_analysis.lab_risk_score + medication_risk.polypharmacy_risk;
    
    let risk_category = if (total_risk_score >= 15) "very_high"
                       else if (total_risk_score >= 10) "high"
                       else if (total_risk_score >= 5) "moderate"
                       else "low";
    
    {
        patient_id: patient_data.id,
        risk_assessment: {
            total_score: total_risk_score,
            category: risk_category,
            component_scores: {
                demographic: demographic_risk.age_risk + demographic_risk.bmi_risk + demographic_risk.gender_risk,
                vitals: vitals_analysis.blood_pressure_risk + vitals_analysis.heart_rate_risk,
                conditions: condition_risk.comorbidity_risk,
                laboratory: lab_analysis.lab_risk_score,
                medications: medication_risk.polypharmacy_risk
            }
        },
        clinical_indicators: {
            demographics: demographic_risk,
            vitals: vitals_analysis,
            conditions: condition_risk,
            laboratory: lab_analysis,
            medications: medication_risk
        },
        recommendations: generate_clinical_recommendations(risk_category, vitals_analysis, condition_risk, lab_analysis, medication_risk)
    }
}

// Clinical decision support and recommendations
pub fn generate_clinical_recommendations(risk_category: string, vitals_analysis, condition_risk, lab_analysis, medication_risk) {
    let recommendations = [];
    
    // Risk-based recommendations
    let risk_rec = if (risk_category == "very_high") "Immediate clinical intervention required - schedule urgent appointment"
                  else if (risk_category == "high") "Close monitoring recommended - schedule follow-up within 2 weeks"
                  else if (risk_category == "moderate") "Regular monitoring - schedule follow-up within 1 month"
                  else "Continue routine care - annual follow-up appropriate";
    
    // Vitals-based recommendations
    let vitals_recs = [];
    let bp_rec = if (vitals_analysis.blood_pressure_risk >= 3) "Hypertension management - consider medication adjustment"
                else if (vitals_analysis.blood_pressure_risk >= 2) "Pre-hypertension - lifestyle modifications recommended"
                else null;
    
    let hr_rec = if (vitals_analysis.heart_rate_risk >= 2) "Abnormal heart rate detected - cardiology consultation recommended"
                else null;
    
    // Condition-based recommendations
    let condition_recs = [];
    let comorbidity_rec = if (condition_risk.comorbidity_risk >= 3) "Complex comorbidities - multidisciplinary care team recommended"
                         else if (condition_risk.comorbidity_risk >= 2) "Multiple conditions - care coordination important"
                         else null;
    
    // Lab-based recommendations
    let lab_recs = [];
    let lab_rec = if (lab_analysis.lab_risk_score >= 6) "Critical lab abnormalities - immediate physician review required"
                 else if (lab_analysis.lab_risk_score >= 3) "Significant lab abnormalities - follow-up testing recommended"
                 else null;
    
    // Medication-based recommendations
    let med_recs = [];
    let poly_rec = if (medication_risk.polypharmacy_risk >= 3) "High medication burden - medication review and optimization recommended"
                  else if (medication_risk.polypharmacy_risk >= 2) "Moderate polypharmacy - periodic medication review advised"
                  else null;
    
    let high_risk_med_rec = if (len(medication_risk.high_risk_medications) > 0) 
        "High-risk medications detected - enhanced monitoring protocols recommended" else null;
    
    // Compile all recommendations
    let all_recommendations = [risk_rec, bp_rec, hr_rec, comorbidity_rec, lab_rec, poly_rec, high_risk_med_rec];
    let final_recommendations = for (rec in all_recommendations) if (rec != null) rec else null;
    
    {
        priority_level: risk_category,
        immediate_actions: for (rec in final_recommendations) 
            if (contains(rec, "Immediate") or contains(rec, "urgent")) rec else null,
        follow_up_actions: for (rec in final_recommendations) 
            if (contains(rec, "follow-up") or contains(rec, "monitoring")) rec else null,
        lifestyle_recommendations: [
            if (vitals_analysis.blood_pressure_risk >= 2) "Implement DASH diet and regular exercise" else null,
            if (condition_risk.comorbidity_risk >= 2) "Disease-specific self-management education" else null,
            "Regular medication adherence monitoring"
        ],
        specialist_referrals: [
            if (vitals_analysis.heart_rate_risk >= 2) "Cardiology" else null,
            if (condition_risk.comorbidity_risk >= 3) "Case Management" else null,
            if (medication_risk.polypharmacy_risk >= 3) "Clinical Pharmacy" else null
        ]
    }
}

// Population health analytics and trending
pub fn analyze_population_health(patient_cohort: []) {
    let total_patients = len(patient_cohort);
    
    // Risk distribution analysis
    let risk_distribution = {
        very_high_risk: len(for (patient in patient_cohort) 
            if (patient.risk_assessment.category == "very_high") patient else null),
        high_risk: len(for (patient in patient_cohort) 
            if (patient.risk_assessment.category == "high") patient else null),
        moderate_risk: len(for (patient in patient_cohort) 
            if (patient.risk_assessment.category == "moderate") patient else null),
        low_risk: len(for (patient in patient_cohort) 
            if (patient.risk_assessment.category == "low") patient else null)
    };
    
    // Clinical trends analysis
    let clinical_trends = {
        avg_total_risk_score: avg(for (patient in patient_cohort) float(patient.risk_assessment.total_score)),
        common_conditions: {
            // Simplified condition frequency analysis
            hypertension_prevalence: (float(len(for (patient in patient_cohort) 
                if (len(for (condition in patient.clinical_indicators.conditions.high_risk_conditions) 
                    if (condition == "hypertension") condition else null) > 0) patient else null)) / float(total_patients)) * 100.0,
            diabetes_prevalence: (float(len(for (patient in patient_cohort) 
                if (len(for (condition in patient.clinical_indicators.conditions.high_risk_conditions) 
                    if (condition == "diabetes") condition else null) > 0) patient else null)) / float(total_patients)) * 100.0
        },
        medication_trends: {
            avg_medication_count: avg(for (patient in patient_cohort) 
                float(patient.clinical_indicators.medications.medication_count)),
            polypharmacy_rate: (float(len(for (patient in patient_cohort) 
                if (patient.clinical_indicators.medications.polypharmacy_risk >= 2) patient else null)) / float(total_patients)) * 100.0
        }
    };
    
    // Quality metrics and outcomes
    let quality_metrics = {
        care_gaps: {
            high_risk_without_follow_up: len(for (patient in patient_cohort) 
                if (patient.risk_assessment.category == "very_high" or patient.risk_assessment.category == "high") patient else null),
            critical_labs_needing_follow_up: len(for (patient in patient_cohort) 
                if (len(patient.clinical_indicators.laboratory.critical_results) > 0) patient else null)
        },
        risk_stratification_effectiveness: {
            high_risk_identification_rate: (float(risk_distribution.very_high_risk + risk_distribution.high_risk) / float(total_patients)) * 100.0,
            intervention_needed_rate: (float(len(for (patient in patient_cohort) 
                if (len(patient.recommendations.immediate_actions) > 0) patient else null)) / float(total_patients)) * 100.0
        }
    };
    
    {
        population_summary: {
            total_patients: total_patients,
            risk_distribution: risk_distribution,
            avg_risk_score: clinical_trends.avg_total_risk_score
        },
        clinical_insights: clinical_trends,
        quality_measures: quality_metrics,
        population_recommendations: {
            immediate_interventions_needed: quality_metrics.care_gaps.high_risk_without_follow_up,
            population_health_priorities: [
                if (clinical_trends.common_conditions.hypertension_prevalence > 30.0) "Hypertension management program" else null,
                if (clinical_trends.common_conditions.diabetes_prevalence > 15.0) "Diabetes care coordination" else null,
                if (clinical_trends.medication_trends.polypharmacy_rate > 25.0) "Medication optimization initiative" else null
            ],
            resource_allocation: {
                care_management_capacity: risk_distribution.very_high_risk + risk_distribution.high_risk,
                clinical_pharmacy_needs: len(for (patient in patient_cohort) 
                    if (patient.clinical_indicators.medications.polypharmacy_risk >= 2) patient else null),
                specialist_referral_volume: len(for (patient in patient_cohort) 
                    if (len(patient.recommendations.specialist_referrals) > 0) patient else null)
            }
        }
    }
}

// Sample patient data for testing healthcare analytics
let sample_patients = [
    {
        id: "PAT_001",
        demographics: {age: 68, gender: "male", bmi: 31.2},
        vitals: [
            {date: t'2024-08-01', systolic: 145, diastolic: 92, heart_rate: 78, temperature: 98.6},
            {date: t'2024-08-15', systolic: 142, diastolic: 88, heart_rate: 82, temperature: 98.4},
            {date: t'2024-09-01', systolic: 148, diastolic: 94, heart_rate: 85, temperature: 98.7}
        ],
        conditions: ["hypertension", "diabetes", "obesity"],
        medications: ["metformin", "lisinopril", "atorvastatin", "aspirin", "metoprolol"],
        lab_results: [
            {test: "HbA1c", value: 8.2, reference_range: {min: 4.0, max: 6.0}, date: t'2024-08-30'},
            {test: "LDL", value: 145.0, reference_range: {min: 0.0, max: 100.0}, date: t'2024-08-30'},
            {test: "eGFR", value: 55.0, reference_range: {min: 90.0, max: 120.0}, date: t'2024-08-30'}
        ]
    },
    {
        id: "PAT_002", 
        demographics: {age: 42, gender: "female", bmi: 23.8},
        vitals: [
            {date: t'2024-08-20', systolic: 118, diastolic: 75, heart_rate: 68, temperature: 98.2},
            {date: t'2024-09-03', systolic: 122, diastolic: 78, heart_rate: 72, temperature: 98.4}
        ],
        conditions: ["asthma"],
        medications: ["albuterol", "fluticasone"],
        lab_results: [
            {test: "CBC", value: 12.5, reference_range: {min: 12.0, max: 16.0}, date: t'2024-08-25'}
        ]
    },
    {
        id: "PAT_003",
        demographics: {age: 74, gender: "female", bmi: 28.5},
        vitals: [
            {date: t'2024-07-15', systolic: 165, diastolic: 98, heart_rate: 95, temperature: 99.1},
            {date: t'2024-08-01', systolic: 158, diastolic: 95, heart_rate: 88, temperature: 98.8},
            {date: t'2024-08-20', systolic: 152, diastolic: 90, heart_rate: 85, temperature: 98.5}
        ],
        conditions: ["hypertension", "heart_disease", "arthritis", "osteoporosis"],
        medications: ["amlodipine", "carvedilol", "warfarin", "calcium", "vitamin_d", "ibuprofen", "omeprazole"],
        lab_results: [
            {test: "INR", value: 3.2, reference_range: {min: 2.0, max: 3.0}, date: t'2024-08-18'},
            {test: "creatinine", value: 1.8, reference_range: {min: 0.6, max: 1.2}, date: t'2024-08-18'}
        ]
    }
];

// Execute comprehensive healthcare analytics
"=== Healthcare Analytics and Patient Management Results ==="

let patient_assessments = for (patient in sample_patients) assess_patient_risk(patient);
"Risk assessment completed for " + string(len(patient_assessments)) + " patients"

let population_analysis = analyze_population_health(patient_assessments);
"Population health analysis completed"

// Generate comprehensive healthcare intelligence report
{
    healthcare_summary: {
        patients_analyzed: len(patient_assessments),
        high_risk_patients: population_analysis.population_summary.risk_distribution.very_high_risk + 
                          population_analysis.population_summary.risk_distribution.high_risk,
        average_risk_score: population_analysis.population_summary.avg_risk_score,
        immediate_interventions_needed: population_analysis.quality_measures.care_gaps.high_risk_without_follow_up
    },
    individual_patient_insights: for (assessment in patient_assessments) {
        {
            patient_id: assessment.patient_id,
            risk_category: assessment.risk_assessment.category,
            total_risk_score: assessment.risk_assessment.total_score,
            key_risk_factors: {
                primary_concerns: for (component in ["demographic", "vitals", "conditions", "laboratory", "medications"]) 
                    if (assessment.risk_assessment.component_scores[component] >= 3) component else null,
                immediate_actions_needed: len(assessment.recommendations.immediate_actions),
                specialist_referrals_recommended: len(assessment.recommendations.specialist_referrals)
            }
        }
    },
    population_health_insights: {
        risk_stratification: {
            distribution: population_analysis.population_summary.risk_distribution,
            intervention_rate: population_analysis.quality_measures.risk_stratification_effectiveness.intervention_needed_rate
        },
        clinical_trends: {
            chronic_disease_burden: population_analysis.clinical_insights.common_conditions,
            medication_complexity: population_analysis.clinical_insights.medication_trends,
            care_coordination_needs: population_analysis.population_recommendations.resource_allocation
        }
    },
    quality_improvement_opportunities: {
        care_gaps_identified: population_analysis.quality_measures.care_gaps,
        population_priorities: population_analysis.population_recommendations.population_health_priorities,
        resource_optimization: {
            care_management_workload: population_analysis.population_recommendations.resource_allocation.care_management_capacity,
            pharmacy_intervention_opportunities: population_analysis.population_recommendations.resource_allocation.clinical_pharmacy_needs,
            specialist_capacity_planning: population_analysis.population_recommendations.resource_allocation.specialist_referral_volume
        }
    },
    clinical_decision_support: {
        high_priority_patients: for (assessment in patient_assessments) 
            if (assessment.risk_assessment.category == "very_high" or assessment.risk_assessment.category == "high") {
                {
                    patient_id: assessment.patient_id,
                    priority_level: assessment.risk_assessment.category,
                    immediate_actions: assessment.recommendations.immediate_actions,
                    follow_up_timeline: if (assessment.risk_assessment.category == "very_high") "Within 48 hours" 
                                       else "Within 2 weeks"
                }
            } else null,
        population_interventions: [
            "Implement blood pressure monitoring program for hypertensive patients",
            "Establish medication therapy management for polypharmacy patients", 
            "Create care coordination protocols for high-risk comorbid patients"
        ]
    }
}
