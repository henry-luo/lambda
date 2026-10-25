// A machine is a downward graph; routing is independent of marble color.
pub let COLORS = ["Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Steel", "Black"]
pub let INKS = ["#c95143", "#dc8a38", "#d8b647", "#638c68", "#4e81a5", "#9270a1", "#879298", "#34373b"]
fn part(kind, x, y, exits, label, link = -1) =>
    {kind:kind, x:x, y:y, exits:exits, label:label, link:link, color:-1, need:0}
fn inlet(x, next, label = "A") => part("funnel", x, 64, [next], label)
fn fork(x, y, a, b, label) => part("switch", x, y, [a,b], label)
fn catch(x, y, next) => part("gate", x, y, [next], "CATCH")
fn relay(x, y, next, link) => part("relay", x, y, [next], "LINK", link)
fn bin(x, color, need = 1) => {*:part("bin", x, 430, [], COLORS[color]), color:color, need:need}
fn puzzle(title, note, nodes, funnels, par, initial = []) =>
    {title:title, note:note, nodes:nodes, funnels:funnels, par:par, initial:initial}

// Reuse the four-pocket machine with different quotas and starting levers.
fn four(title, note, colors, quota, initial = []) => puzzle(title, note, [
    inlet(360,1), fork(360,172,2,3,"I"),
    fork(204,292,4,5,"II"), fork(516,292,6,7,"III"),
    bin(108,colors[0],quota), bin(276,colors[1],quota),
    bin(444,colors[2],quota), bin(612,colors[3],quota)
], [0], quota * 4, initial)

pub let LEVELS = [
    puzzle("The first diversion", "Every passing marble flips the brass lever. Read the lit rail, choose a color, then drop.", [
        inlet(360,1), fork(360,228,2,3,"I"), bin(200,0), bin(520,4)
    ], [0], 2),
    four("A fork in the thought", "A switch remembers its last visitor. Follow the whole route before choosing your next marble.",
        [0,2,4,3], 1),
    puzzle("A distant connection", "The diamond on the left flips switch II on the right. A marble can change a rail it never visits.", [
        inlet(204,2,"A"), inlet(516,3,"B"), relay(204,174,4,3),
        fork(516,270,7,8,"II"), fork(204,290,5,6,"I"),
        bin(108,1), bin(276,5), bin(444,0), bin(612,4)
    ], [0,1], 4),
    puzzle("A small sacrifice", "The closed catch keeps its first marble and opens permanently. A cheap steel ball will do the job.", [
        inlet(360,1), catch(360,172,2), fork(360,290,3,4,"I"), bin(200,0), bin(520,3)
    ], [0], 3),
    four("An encore in color", "Each pocket wants two marbles. The machine cycles, but a filled pocket still lets a marble go to waste.",
        [1,5,2,4], 2),
    puzzle("The reluctant branch", "One branch has a catch. The black marble takes the color of any unfinished pocket; save it for a useful moment.", [
        inlet(360,1), fork(360,150,2,3,"I"), catch(204,244,4),
        fork(516,288,7,8,"III"), fork(204,320,5,6,"II"),
        bin(108,0), bin(276,1), bin(444,4), bin(612,5)
    ], [0], 5),
    puzzle("Two sides of the story", "Six colors, two inlets, one linked lever. The left inlet changes the right; its catch must open first.", [
        inlet(252,2,"A"), inlet(570,3,"B"), relay(252,160,5,4), catch(570,160,4),
        fork(570,294,12,13,"IV"), fork(252,242,6,7,"I"),
        fork(144,330,8,9,"II"), fork(360,330,10,11,"III"),
        bin(70,0), bin(186,1), bin(302,2), bin(418,3), bin(534,4), bin(650,5)
    ], [0,1], 7),
    puzzle("The chromatic engine", "Two of every color. Open the catch, then use steel to cycle past full pockets while the deeper branches fill.", [
        inlet(360,1), catch(360,133,2), fork(360,211,3,4,"I"),
        fork(164,316,7,8,"II"), fork(504,277,5,6,"III"),
        fork(396,350,9,10,"IV"), fork(612,350,11,12,"V"),
        bin(70,0,2), bin(186,1,2), bin(302,2,2), bin(418,3,2), bin(534,4,2), bin(650,5,2)
    ], [0], 17)
]
