#pragma once
#include "scale.hpp"
#include "../lib/ownership.hpp"
#include <stdint.h>
#include <stddef.h>

// Source identities and exact metrics belong to producers; the compositor needs no DOM.
struct TypesetRecord { uint64_t provider; }; // producer-specific records extend this typed prefix
enum TypesetOffsetUnit : uint8_t { TYPESET_UTF8_BYTES, TYPESET_CODEPOINTS, TYPESET_PROVIDER_OFFSETS };
struct TypesetSource {
    uint64_t provider, generation, node;
    TypesetOffsetUnit offset_unit;
    lam::Up<const TypesetRecord> native;
};
struct TypesetSourceNamespace { uint64_t provider, generation; };
struct TypesetSourceScope { const TypesetSourceNamespace* namespaces; size_t count; };
bool typeset_source_in_scope(const TypesetSource& source, uint64_t provider, uint64_t generation,
    const TypesetSourceScope& scope);
bool typeset_source_same_identity(const TypesetSource& left, const TypesetSource& right);

struct TypesetMetrics {
    float advance, height, depth, baseline;
    RdtLogicalRect ink;
    lam::Up<const TypesetRecord> exact; // future scaled-point arithmetic stays in producer/policy storage
};

struct TypesetGlue {
    float natural, stretch, shrink;
    uint8_t stretch_order, shrink_order;
    bool discard_start, discard_end;
};

enum TypesetBreakLegality : uint8_t { TYPESET_BREAK_ALLOWED, TYPESET_BREAK_FORBIDDEN, TYPESET_BREAK_FORCED };
enum TypesetBreakScope : uint8_t { TYPESET_BREAK_LINE, TYPESET_BREAK_COLUMN, TYPESET_BREAK_PAGE };
struct TypesetBreak {
    TypesetBreakLegality legality;
    TypesetBreakScope scope;
    int32_t penalty;
    uint32_t reason;
};

enum TypesetItemKind : uint8_t { TYPESET_BOX, TYPESET_GLUE, TYPESET_PENALTY };
struct TypesetItem {
    TypesetItemKind kind;
    TypesetSource source;
    size_t start, length;
    TypesetMetrics metrics;
    TypesetGlue glue;
    TypesetBreak boundary;
    lam::Up<const TypesetRecord> paint;
    TypesetBreak before;
    bool has_before; // optional boundary before this item; zero-initialized producers retain existing behavior
};

struct TypesetResume {
    uint64_t provider, generation, serial;
    uint64_t state[4]; // producer-owned stable IDs, never pointers into rewound trial scratch
};

struct TypesetPacking {
    float ratio, residual;
    uint8_t order;
    bool shrinking;
};

struct TypesetLineCandidate {
    size_t first, next, paint_first, paint_end;
    float width, height, depth;
    float baseline; // optional producer strut; zero preserves legacy line-height packing
    double cost;
    int32_t penalty;
    TypesetPacking packing;
    bool forced, overflow;
    lam::Up<const TypesetRecord> solution;
};

struct TypesetParagraph;
typedef size_t (*TypesetLineAlternativesFn)(const TypesetParagraph* paragraph, size_t first,
    float width, TypesetLineCandidate* candidates, size_t capacity, void* context);
typedef size_t (*TypesetLineChooseFn)(const TypesetLineCandidate* candidates, size_t count, void* context);
typedef bool (*TypesetItemMeasureFn)(const TypesetParagraph* paragraph, size_t index,
    float width, TypesetMetrics* metrics, void* context);
struct TypesetParagraph {
    const TypesetItem* items;
    size_t count;
    float minimum_line_height;
    TypesetLineAlternativesFn alternatives;
    TypesetLineChooseFn choose;
    void* context;
    TypesetItemMeasureFn measure; // pure width-specific metrics; source items stay immutable through retries
    float minimum_baseline;
    bool baseline_aware; // zero is a valid producer baseline; older positive struts remain compatible
    size_t max_alternatives; // optional producer bound; zero uses the item count
};

enum TypesetStatus : uint8_t {
    TYPESET_OK, TYPESET_INVALID, TYPESET_NO_PROGRESS, TYPESET_UNPLACEABLE,
    TYPESET_BUDGET_EXHAUSTED, TYPESET_STALE, TYPESET_OUT_OF_MEMORY,
    TYPESET_DONE,
};

TypesetPacking typeset_pack_glue(const TypesetItem* items, size_t first, size_t end,
                                float natural, float target);
float typeset_glue_advance(const TypesetGlue* glue, const TypesetPacking* packing);
size_t typeset_line_alternatives(const TypesetParagraph* paragraph, size_t first,
    float width, TypesetLineCandidate* candidates, size_t capacity, void* context);
size_t typeset_choose_furthest_line(const TypesetLineCandidate* candidates, size_t count, void* context);
size_t typeset_choose_lowest_cost(const TypesetLineCandidate* candidates, size_t count, void* context);
TypesetStatus typeset_next_line(const TypesetParagraph* paragraph, size_t first,
    float width, TypesetLineCandidate* scratch, size_t capacity, TypesetLineCandidate* result);

// Nested formatting contexts supply replayable contributions and exact checkpoints.
enum TypesetContributionKind : uint8_t {
    TYPESET_CONTRIBUTION_BOX, TYPESET_CONTRIBUTION_PARAGRAPH,
    TYPESET_CONTRIBUTION_GLUE, TYPESET_CONTRIBUTION_BOUNDARY,
    TYPESET_CONTRIBUTION_INSERTION, TYPESET_CONTRIBUTION_FLOAT,
    TYPESET_CONTRIBUTION_MARK, TYPESET_CONTRIBUTION_TARGET,
    TYPESET_CONTRIBUTION_FLUSH_DEFERRED,
    TYPESET_CONTRIBUTION_NESTED,
};
enum TypesetRegionKind : uint8_t { TYPESET_REGION_BODY, TYPESET_REGION_NOTE, TYPESET_REGION_FLOAT, TYPESET_REGION_MARGIN };
enum TypesetRegionEdge : uint8_t { TYPESET_REGION_START, TYPESET_REGION_END };
struct TypesetFlowProvider;
struct TypesetMark;
struct TypesetTarget;
struct TypesetRegionMaterial;
struct TypesetContribution {
    TypesetContributionKind kind;
    TypesetSource source;
    TypesetMetrics metrics;
    TypesetGlue glue;
    TypesetBreak boundary;
    const TypesetParagraph* paragraph;
    const TypesetFlowProvider* nested;
    TypesetRegionKind region;
    uint64_t identity;
    const void* payload;
    union {
        const TypesetMark* mark;
        const TypesetTarget* target;
        const TypesetRegionMaterial* region_material;
    };
    TypesetRegionEdge region_edge;
};
struct TypesetFlowProvider {
    uint64_t identity, generation;
    void* context;
    TypesetStatus (*next)(void* context, const TypesetResume* cursor,
                          TypesetContribution* contribution, TypesetResume* next);
    TypesetStatus (*checkpoint)(void* context, const TypesetResume* cursor, TypesetResume* saved);
    TypesetStatus (*restore)(void* context, const TypesetResume* saved, TypesetResume* restored);
};

enum TypesetAssemblyAction : uint8_t { TYPESET_ASSEMBLY_FINALIZE, TYPESET_ASSEMBLY_HOLD, TYPESET_ASSEMBLY_REINSERT };
enum TypesetPageKind : uint8_t { TYPESET_PAGE_FLOW, TYPESET_PAGE_REGION, TYPESET_PAGE_BLANK, TYPESET_PAGE_FIXED, TYPESET_PAGE_EMPTY };
struct TypesetRegionPlacement;
struct TypesetRegionSelection {
    TypesetRegionKind kind;
    TypesetRegionEdge edge;
    const TypesetRegionPlacement* placements;
    size_t count;
};
struct TypesetFragmentainerCandidate {
    RdtLogicalRect rect;
    TypesetResume start, end;
    float body_height, note_height, float_height;
    TypesetBreak boundary;
    const TypesetRegionSelection* auxiliary;
    size_t auxiliary_count;
};
struct TypesetPageCandidate {
    TypesetResume start, end;
    float body_height, note_height, float_height, available_height;
    TypesetBreak boundary;
    double cost;
    const void* trial;
    TypesetPageKind kind;
    uint32_t page_number;
    // multi-region metrics sum physical capacities; unused trailing regions have equal start/end cursors.
    const TypesetFragmentainerCandidate* fragmentainers;
    size_t fragmentainer_count;
};
struct TypesetPolicyCheckpoint { uint64_t state[4]; }; // policy-owned stable values, never rewound scratch pointers
struct TypesetPagePlan;
struct TypesetPagePolicy {
    void* context;
    size_t (*choose)(void* context, const TypesetPageCandidate* candidates, size_t count);
    TypesetAssemblyAction (*assemble)(void* context, const TypesetPageCandidate* candidate);
    TypesetStatus (*committed)(void* context, const TypesetPageCandidate* candidate);
    // provisional page effects participate in the compositor's nested and edition journals.
    TypesetStatus (*checkpoint)(void* context, TypesetPolicyCheckpoint* saved);
    TypesetStatus (*restore)(void* context, const TypesetPolicyCheckpoint* saved);
    // the held plan remains borrowed and immutable through this journaled policy transition.
    TypesetStatus (*transition)(void* context, TypesetAssemblyAction action, const TypesetPagePlan* plan);
};
TypesetStatus typeset_policy_checkpoint(const TypesetPagePolicy* policy, TypesetPolicyCheckpoint* saved);
TypesetStatus typeset_policy_restore(const TypesetPagePolicy* policy, const TypesetPolicyCheckpoint* saved);
TypesetStatus typeset_flow_checkpoint(const TypesetFlowProvider* provider,
                                      const TypesetResume* cursor, TypesetResume* saved);
TypesetStatus typeset_flow_restore(const TypesetFlowProvider* provider,
                                   const TypesetResume* saved, TypesetResume* restored);
TypesetStatus typeset_flow_next(const TypesetFlowProvider* provider, const TypesetResume* cursor,
                                TypesetContribution* contribution, TypesetResume* next);
TypesetStatus typeset_page_select(const TypesetPagePolicy* policy,
    const TypesetPageCandidate* candidates, size_t count, size_t* selected, TypesetAssemblyAction* action);
TypesetStatus typeset_page_assemble(const TypesetPagePolicy* policy,
    const TypesetPageCandidate* candidate, TypesetAssemblyAction* action);

struct Pool;
struct hashmap;
struct TypesetPageConstraints {
    float available_height;
    size_t max_contributions, max_candidates;
};
// the producer measures nested contexts and auxiliary regions without publishing fragments.
struct TypesetPageProbe {
    void* context;
    TypesetStatus (*measure)(void* context, const TypesetContribution* contributions,
        size_t count, TypesetPageCandidate* candidate, bool* stop);
};
struct TypesetPagePlan {
    Pool* scratch; // borrowed; the caller retains scratch, provider and policy until commitment or abandonment
    const TypesetFlowProvider* provider; // null for an already selected physical-page plan; its host owns replay
    const TypesetPagePolicy* policy;
    TypesetPageCandidate candidate;
    TypesetContribution* contributions;
    size_t count;
    TypesetAssemblyAction action;
    bool complete, committed;
};
// every exit restores the provider's start checkpoint; commitment is a separate operation.
// held/reinserted plans remain provisional; the caller may continue or replace them without shipout.
TypesetStatus typeset_page_plan(const TypesetFlowProvider* provider, const TypesetResume* cursor,
    const TypesetPageConstraints* constraints, const TypesetPageProbe* probe,
    const TypesetPagePolicy* policy, Pool* scratch, TypesetPagePlan* result);
TypesetStatus typeset_page_commit(TypesetPagePlan* plan, TypesetResume* next);

struct TypesetAssemblyCheckpoint {
    TypesetPolicyCheckpoint policy;
    TypesetResume end;
    TypesetAssemblyAction action;
};
struct TypesetPageAssembly {
    Pool* pool;
    TypesetAssemblyCheckpoint* history;
    size_t count, capacity, transitions, limit;
};
// hold keeps the selected plan; reinsert asks the host to restore its input and replan without shipout.
TypesetStatus typeset_page_transition(TypesetPagePlan* plan, TypesetPageAssembly* assembly, bool* reinsert);

struct TypesetTarget {
    const char* name;
    TypesetSource source;
    uint32_t page_number;
    lam::Up<const TypesetRecord> value;
    uint32_t last_page_number;
    lam::Up<const TypesetRecord> binding; // compositor-owned reference metadata; value remains the producer payload
};
struct TypesetTargetStore {
    uint64_t provider, generation;
    Pool* pool;
    TypesetTarget* entries;
    size_t count, capacity, limit;
    hashmap* index;
    TypesetSourceScope sources;
};
// Binding payloads must outlive the provisional layout that produced them.
TypesetStatus typeset_target_append(TypesetTargetStore* store, const TypesetTarget* target);
const TypesetTarget* typeset_target_find(const TypesetTargetStore* store, const char* name);
void typeset_targets_dispose(TypesetTargetStore* store);

struct TypesetPassSignature { const char* bytes; size_t length; };
struct TypesetConvergence {
    Pool* pool;
    TypesetPassSignature* history;
    size_t count, capacity;
    uint32_t limit;
};
// Producers serialize stable bindings and layout decisions, never arena addresses.
TypesetStatus typeset_convergence_observe(TypesetConvergence* state,
    const char* bytes, size_t length, bool* settled);
