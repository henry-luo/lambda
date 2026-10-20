// S9.2.2: shared arrangement math returns displacements, leaving document edits to callers.
pub let modes = ["left","center","right","top","middle","bottom","distribute-x","distribute-y"]
pub fn horizontal(mode) => mode in ["left","center","right","distribute-x"]
pub fn offsets(boxes,reference,mode) {
    let axis = if (horizontal(mode)) "x" else "y"
    let size = if (horizontal(mode)) "width" else "height"
    let ordered = sort(boxes,(box) => box[axis])
    let distributing = starts_with(mode,"distribute-")
    let gap = if (len(boxes)>1) (reference[size]-sum([for (box in boxes) box[size]]))/(len(boxes)-1) else 0;
    map([for (i,box in ordered) (
        let desired = if (distributing) reference[axis]+sum([for (j in 0 to i-1) ordered[j][size]])+gap*i
            else reference[axis]+(if (mode in ["right","bottom"]) reference[size]-box[size]
                else if (mode in ["center","middle"]) (reference[size]-box[size])/2 else 0),
        *[box.id,desired-box[axis]])])
}
