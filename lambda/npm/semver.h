#ifndef NPM_SEMVER_H
#define NPM_SEMVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Semver version: MAJOR.MINOR.PATCH[-prerelease][+build]
// ---------------------------------------------------------------------------

typedef struct {
    int major;
    int minor;
    int patch;
    char prerelease[64];   // e.g. "alpha.1", "beta.2", "rc.1"
    char build[64];        // e.g. "build.123" (ignored in comparisons)
    bool valid;
} SemVer;

// Parse a version string like "1.2.3", "1.2.3-beta.1", "1.2.3+build.42"
SemVer semver_parse(const char* version_str);

// Compare two versions. Returns <0, 0, >0 (like strcmp).
// Build metadata is ignored per spec.
int semver_compare(const SemVer* a, const SemVer* b);

// Format version to string. Caller must provide buffer of at least 128 bytes.
void semver_format(const SemVer* v, char* buf, int buf_size);

// ---------------------------------------------------------------------------
// Semver range: supports npm range syntax
// ---------------------------------------------------------------------------

// Comparator operator
typedef enum { CMP_EQ, CMP_LT, CMP_LE, CMP_GT, CMP_GE } SemVerCmpOp;

// Comparator: a single constraint like >=1.2.3 or <2.0.0
typedef struct {
    SemVerCmpOp op;
    SemVer version;
} SemVerComparator;

// ComparatorSet: AND-joined set of comparators (e.g. ">=1.2.3 <2.0.0")
#define SEMVER_MAX_COMPARATORS 8
typedef struct {
    SemVerComparator comparators[SEMVER_MAX_COMPARATORS];
    int count;
} SemVerComparatorSet;

// Range: OR-joined sets of comparator sets (e.g. ">=1.2.3 <2.0.0 || >=3.0.0")
#define SEMVER_MAX_SETS 8
typedef struct {
    SemVerComparatorSet sets[SEMVER_MAX_SETS];
    int set_count;
    bool valid;
} SemVerRange;

// Parse a range string. Supports:
//   exact:    "1.2.3"
//   range:    ">=1.2.3 <2.0.0"
//   tilde:    "~1.2.3" → >=1.2.3 <1.3.0
//   caret:    "^1.2.3" → >=1.2.3 <2.0.0
//   hyphen:   "1.2.3 - 2.3.4" → >=1.2.3 <=2.3.4
//   x-range:  "1.2.x", "1.*", "*"
//   or:       ">=1.2.3 || >=2.0.0"
SemVerRange semver_range_parse(const char* range_str);

// Check if a version satisfies a range.
bool semver_satisfies(const SemVer* version, const SemVerRange* range);

// Find the maximum version from an array that satisfies a range.
// Returns -1 if none satisfy.
int semver_max_satisfying(const SemVer* versions, int count, const SemVerRange* range);

#ifdef __cplusplus
}
#endif

#endif // NPM_SEMVER_H
