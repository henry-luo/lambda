// A `raise` in one match arm ends only that arm's path: the match still
// yields its value whenever another arm is chosen. The JIT leaked the raising
// arm's exit out of the match, so an enclosing `if` join read the whole match
// as an exited branch and produced null (or an error under an `int^` return).

fn pick(n) any^ => if (n <= 0) 0 else match n {
    case 1: 10
    default: raise error("no rule")
    }

fn pick_int(n) int^ => if (n <= 0) 0 else match n {
    case 1: 10
    default: raise error("no rule")
    }

// the match as the whole body, with a native return lane
fn tail_int(n) int^ => match n {
    case 1: 10
    default: raise error("no rule")
    }

// every arm exits: the match itself never reaches its join
fn both_raise(n) any^ => if (n <= 0) 0 else match n {
    case 1: raise error("a")
    default: raise error("b")
    }

// the shape a definitional interpreter takes: a fuel guard, then rule dispatch
// whose default arm is stuckness, recursing through `^`
fn ev(t, fuel) any^ =>
    if (fuel <= 0) raise error("out of fuel")
    else match name(t) {
    case 'lit': t.v
    case 'add': ev(t[0], fuel - 1)^ + ev(t[1], fuel - 1)^
    default: raise error("stuck")
    }

[pick(1) ^ { -1 }, pick(2) ^ { -1 }, pick(0) ^ { -1 }];
[pick_int(1) ^ { -1 }, pick_int(2) ^ { -1 }, pick_int(0) ^ { -1 }];
[tail_int(1) ^ { -1 }, tail_int(2) ^ { -1 }];
[both_raise(1) ^ { -1 }, both_raise(0) ^ { -1 }];
[ev(<add <lit v:3> <add <lit v:4> <lit v:5>>>, 10) ^ { -1 },
 ev(<add <lit v:3> <nosuch>>, 10) ^ { -1 },
 ev(<add <lit v:3> <lit v:4>>, 1) ^ { -1 }]
