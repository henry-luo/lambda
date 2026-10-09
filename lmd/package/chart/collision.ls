// Shared measured-box selection for guides and annotations in one plot coordinate space.
pub fn enabled(policy) => policy != null and policy != false and policy != "none"

pub fn gap(value) => if (value != null) max([0.0, float(value)]) else 2.0

pub fn translate(box, x, y) => {left: box.left + x, right: box.right + x,
    top: box.top + y, bottom: box.bottom + y}

pub fn overlaps(a, b, separation = 0.0) => a != null and b != null and
    a.left < b.right + separation and b.left < a.right + separation and
    a.top < b.bottom + separation and b.top < a.bottom + separation

fn projection(box, direction) {
    let corners = [for (x in [box.left, box.right], y in [box.top, box.bottom]) x * direction[0] + y * direction[1]];
    {lo: min(corners), hi: max(corners)}
}

// move a measured box beyond all obstacles along a unit outward direction.
pub fn outward_shift(box, obstacles, direction, gap = 0.0) {
    let nearest = projection(box, direction).lo;
    max([0.0, for (obstacle in obstacles) projection(obstacle, direction).hi + gap - nearest])
}

fn select_round(pending, occupied, index) {
    if (index >= len(pending)) [] else {
        let candidate = pending[index];
        let blocked = len([for (prior in occupied where overlaps(candidate.bounds, prior.bounds,
            max([candidate.gap, if (prior.gap != null) prior.gap else 0.0]))) prior]) > 0;
        if (blocked) select_round(pending, occupied, index + 1)
        else [candidate, *select_round(pending, [*occupied, candidate], index + 1)]
    }
}

// Explicitly retained labels are obstacles regardless of their order in the candidate list.
pub fn select(candidates, obstacles = []) {
    let fixed = candidates |: not ~.optional;
    [*fixed, *select_round(candidates |: ~.optional, [*obstacles, *fixed], 0)]
}
