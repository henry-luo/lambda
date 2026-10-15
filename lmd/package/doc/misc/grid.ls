// Shared row-span occupancy; CALS import and source writers use the same column rules.
pub fn free_column(blocked,begin_col) => if (begin_col >= len(blocked) or blocked[begin_col] == 0) begin_col
    else free_column(blocked,begin_col+1)
pub fn reserve(blocked,begin_col,end_col,height) => [for (i in range(0,max(len(blocked),end_col+1),1))
    if (i >= begin_col and i <= end_col) height else if (i < len(blocked)) blocked[i] else 0]
pub fn advance(blocked) => [for (value in blocked) max(0,value-1)]
