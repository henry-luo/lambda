// Scheduling uses monotonic time; saved versions continue to use wall-clock timestamps.
pub pn milliseconds() float { return clock()*1000 }
