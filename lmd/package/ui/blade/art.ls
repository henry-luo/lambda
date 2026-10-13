// original vector artwork: bevel facets and engraved outlines remain sharp at any scale.
import c: lambda.ui.core.component

fn path(d, fill, stroke = "#241913", width = 2) => <path d:d,fill:fill,stroke:stroke,["stroke-width"]:width>
fn circle(x,y,r,fill,stroke = "#241913",width = 2) => <circle cx:x,cy:y,r:r,fill:fill,stroke:stroke,["stroke-width"]:width>
fn potion(color) => [
    path("M37 13 L59 13 L57 29 Q56 34 65 42 C85 60 75 84 48 85 C21 84 11 60 31 42 Q40 34 39 29 Z","#b7d9dc"),
    path("M27 56 Q48 49 69 56 C77 73 64 81 48 81 C31 80 20 71 27 56 Z",color),
    path("M38 11 L58 11 L57 23 L39 23 Z","#ac8154"),
    path("M41 13 L46 13 L46 21 L41 21 Z","#e4bd7e","none"),
    path("M32 49 Q24 62 31 70 M40 30 Q43 40 35 44","none","#ffffff",4),
    <ellipse cx:48,cy:58,rx:20,ry:4,fill:"#ffffff",opacity:0.28>,
    circle(57,69,3,"#ffffff88","none"),circle(44,74,2,"#ffffff88","none")]
fn sword() => [
    path("M28 69 L69 17 L85 10 L81 28 L39 78 Z","#d7e2e7"),
    path("M35 72 L80 17 L81 28 L39 78 Z","#637d8c","none"),
    path("M28 69 L69 17 L80 13 L35 72 Z","#f2f7f7","none"),
    path("M22 60 L44 78 L40 84 L16 65 Z","#ce9a4a"),
    path("M23 68 L31 76 L18 90 L10 82 Z","#513525"),
    path("M21 74 L27 79 M17 79 L23 84","none","#d5b06e",2),
    circle(12,87,6,"#d0a65d"),circle(29,70,4,"#d74438")]
fn shield() => [
    path("M48 9 Q67 21 82 20 L77 58 Q72 77 48 90 Q24 77 19 58 L14 20 Q31 21 48 9 Z","#bda577"),
    path("M48 17 Q65 27 74 27 L70 57 Q66 73 48 82 Q29 72 26 57 L22 27 Q34 26 48 17 Z","#668596"),
    path("M48 20 L48 78 Q30 68 28 54 L25 30 Z","#bed0d2","none"),
    path("M48 27 L53 41 L67 45 L56 54 L58 69 L48 61 L37 69 L40 54 L29 45 L44 41 Z","#e0bb6c"),
    path("M48 32 L48 56 L39 62 L43 52 L35 46 L46 44 Z","#fff0b9","none"),
    *[for (p in [[20,27],[76,27],[25,57],[71,57],[48,84]]) circle(p[0],p[1],2,"#f8e5ad","none")]]
fn helmet() => [
    path("M27 43 Q22 28 12 24 Q16 42 30 51 M69 43 Q74 28 84 24 Q80 42 66 51","#bdc5c5"),
    path("M25 40 Q26 15 48 12 Q70 15 71 40 L74 75 L58 83 L51 64 L45 64 L38 83 L22 75 Z","#889ca6"),
    path("M48 16 L48 58 L35 76 L28 72 L31 38 Q31 23 48 16 Z","#dae4e5","none"),
    path("M48 16 L53 30 L53 56 L48 62 L43 56 L43 30 Z","#d6b874"),
    path("M29 43 L42 47 L40 55 L28 51 Z M67 43 L54 47 L56 55 L68 51 Z","#171a20"),
    path("M28 58 L38 61 M26 64 L36 68 M68 58 L58 61 M70 64 L60 68","none","#4b5c65",2)]
fn armor() => [
    path("M33 17 L42 22 L54 22 L63 17 L85 31 L76 47 L67 40 L64 70 L72 81 L24 81 L32 70 L29 40 L20 47 L11 31 Z","#6b8ca2"),
    path("M33 23 L43 30 L48 33 L48 73 L31 73 L36 55 Z","#c4d9e0","none"),
    path("M63 23 L53 30 L48 33 L48 73 L65 73 L60 55 Z","#4f7187","none"),
    path("M33 17 L42 22 L54 22 L63 17 L60 30 L48 37 L36 30 Z","#dab77b"),
    path("M19 29 L32 23 L29 40 L20 43 L15 33 Z M77 29 L64 23 L67 40 L76 43 L81 33 Z","#b6ced9"),
    path("M32 70 L64 70 L67 77 L29 77 Z","#d7b36d"),circle(48,73,4,"#50adc4"),
    path("M36 44 L44 49 M60 44 L52 49","none","#f3e4b2",2)]
fn boots() => [
    path("M23 18 L45 21 L41 56 L47 71 L44 82 L14 82 Q10 72 21 65 Z","#6b4934"),
    path("M53 18 L76 21 L72 56 L86 69 Q91 76 84 82 L53 82 L48 72 L53 61 Z","#a47b4d"),
    path("M24 20 L43 23 L42 31 L23 28 Z M54 20 L74 23 L73 31 L53 28 Z","#ddba75"),
    path("M24 34 L33 36 L29 61 L20 71 L16 71 L25 61 Z M56 35 L63 37 L59 62 L56 71 L53 68 Z","#c19762","none"),
    path("M25 42 L40 45 M24 52 L39 55 M55 42 L72 45 M54 52 L71 55","none","#2f2720",3),
    path("M14 78 L44 78 M53 78 L85 78","none","#241b17",5)]
fn bow() => [
    path("M25 12 C83 17 89 67 40 86 C68 58 63 32 25 12 Z","#b78245"),
    path("M27 15 C72 28 76 61 43 82","none","#f5d796",3),
    path("M25 12 L40 86","none","#d9d8bc",1.5),
    path("M14 73 L72 28","none","#e2ca8f",3),
    path("M70 22 L83 20 L77 33 Z","#c6d8db"),
    path("M15 69 L23 74 L17 82 L8 77 Z","#c45640"),
    path("M62 42 L67 48 L64 58 L59 53 Z","#63402b")]
fn scroll() => [
    path("M29 19 L76 19 Q88 21 81 34 L72 34 L63 75 L20 75 Z","#e6cea0"),
    path("M29 19 Q16 12 15 24 Q13 33 26 34 L20 66 Q6 62 9 75 Q11 85 23 82 L63 82 Q73 80 69 69 L20 66","#ba975f"),
    path("M31 25 L72 25 L64 61 L24 61 Z","#f5e1b8","none"),
    path("M33 36 L61 36 M31 43 L57 43 M29 50 L47 50","none","#aa8050",2),
    circle(52,64,11,"#a94947"),path("M52 55 L56 61 L53 70 L48 63 Z","#f0bf68","none")]
fn ring() => [
    <ellipse cx:48,cy:57,rx:24,ry:29,fill:"none",stroke:"#725021",["stroke-width"]:13>,
    <ellipse cx:48,cy:55,rx:24,ry:29,fill:"none",stroke:"#e3b85e",["stroke-width"]:7>,
    path("M26 49 Q30 28 48 25 Q67 27 71 48","none","#fff0b3",3),
    path("M33 20 L47 11 L63 20 L58 39 L41 42 L30 31 Z","#77b7cd"),
    path("M47 11 L48 27 L33 20 Z M48 27 L58 39 L41 42 Z","#def8f7","none"),
    path("M48 27 L63 20 L58 39 Z","#397b9f","none"),
    path("M33 20 L47 11 L63 20 L58 39 L41 42 L30 31 Z","none","#e6bc6c",3)]
fn fire() => [
    path("M48 9 Q64 28 59 39 L71 28 Q90 55 75 74 Q63 89 44 86 Q12 80 18 56 Q20 42 34 32 L33 49 Q50 33 48 9 Z","#f18b25","#742920"),
    path("M49 27 Q55 44 47 54 Q61 49 65 43 Q76 66 62 77 Q40 92 27 68 Q25 56 35 49 L37 63 Q51 53 49 27 Z","#ffd262","none"),
    path("M48 53 Q52 65 59 66 Q61 80 48 82 Q34 78 39 68 Z","#fff4b7","none"),
    *[for (p in [[22,25],[77,49],[66,15]]) path("M" ++ string(p[0]) ++ " " ++ string(p[1]) ++ " l3 -6 l3 6 l-3 3 Z","#ffda76","none")]]
fn ice() => [
    path("M15 56 L25 30 L41 43 L48 10 L65 35 L69 54 L82 43 L83 69 L48 87 L20 77 Z","#4e9fb8"),
    path("M48 10 L48 81 L35 55 Z","#c1f0ee","none"),
    path("M48 10 L65 35 L48 81 Z","#74c9de","none"),
    path("M25 30 L35 55 L48 81 L20 77 Z","#a4e5ed","none"),
    path("M82 43 L64 67 L48 81 L83 69 Z","#bceff2","none"),
    path("M25 30 L35 55 L48 81 L65 35 M48 10 L48 81 M15 56 L48 81 L82 43","none","#ecffff",1.5)]
fn heal() => [
    circle(48,48,35,"#6c713c","#d8ba72",3),circle(48,48,30,"#8c9950","#b0c578",1),
    path("M38 20 L58 20 L58 38 L76 38 L76 58 L58 58 L58 76 L38 76 L38 58 L20 58 L20 38 L38 38 Z","#e6c575"),
    path("M42 24 L54 24 L54 42 L72 42 L72 49 L49 49 L49 72 L42 72 L42 54 L24 54 L24 42 L42 42 Z","#fff1b3","none"),
    path("M44 28 L48 28 M28 46 L36 46","none","#ffffff",2)]
fn hammer() => [
    path("M24 81 L53 41 L62 47 L34 88 Z","#8d592e"),
    path("M28 80 L54 45","none","#e3b269",3),
    path("M24 28 L41 11 L76 36 L61 56 Z","#a5b3ba"),
    path("M24 28 L41 11 L46 23 L31 36 Z","#edf4f1","none"),
    path("M31 36 L46 23 L70 40 L61 50 Z","#697e8e","none"),
    path("M25 28 L33 19 L68 45 L61 54 Z","none","#d6b678",4),
    circle(28,85,5,"#cba562")]
fn dash() => [
    path("M18 33 L37 33 M10 47 L32 47 M16 62 L30 62","none","#92e0eb",4),
    circle(64,21,8,"#b7edf1"),
    path("M53 30 L64 35 L74 31 L80 38 L63 45 L54 41 L50 54 L66 64 L59 84 L49 82 L55 68 L42 62 L33 82 L22 78 L35 50 L42 35 Z","#79cbdc"),
    path("M53 30 L59 35 L47 51 L38 53 L45 36 Z","#e4fcf8","none"),
    path("M58 46 L65 50 L75 46","none","#90ddeb",4)]
fn skull() => [
    path("M23 39 Q23 12 48 12 Q74 12 74 39 L68 59 L61 63 L59 81 L37 81 L35 63 L28 59 Z","#d7cfaf"),
    path("M28 39 Q28 18 48 17 L48 73 L39 74 L38 59 L31 55 Z","#f1e9cf","none"),
    path("M31 39 Q42 33 44 46 Q41 57 31 50 Z M65 39 Q54 33 52 46 Q55 57 65 50 Z","#332728"),
    path("M48 51 L54 61 L43 61 Z","#59463c"),
    path("M40 70 L40 79 M48 69 L48 80 M56 70 L56 79","none","#675649",2),
    path("M29 32 Q33 23 41 22","none","#ffffff",2)]
fn gem() => [
    path("M27 18 L67 18 L83 40 L48 86 L13 40 Z","#7653a9"),
    path("M27 18 L48 35 L13 40 Z M67 18 L83 40 L48 35 Z","#c4a0eb","none"),
    path("M27 18 L67 18 L48 35 Z","#f3d9ff","none"),
    path("M13 40 L48 35 L48 86 Z","#b187d9","none"),
    path("M27 18 L67 18 L83 40 L48 86 L13 40 Z M13 40 L83 40 M27 18 L48 86 L67 18","none","#ead2ff",1.5)]
let icons = {sword:sword,shield:shield,helmet:helmet,armor:armor,boots:boots,bow:bow,scroll:scroll,ring:ring,
    fire:fire,ice:ice,heal:heal,hammer:hammer,dash:dash,skull:skull,gem:gem,
    ["potion-red"]:() => potion("#c74652"),["potion-blue"]:() => potion("#3a9bce"),["potion-green"]:() => potion("#6aaa55")}
pub fn names() array => [for (key,value in icons) string(key)]
pub fn icon(name, size = 64, label = null, attributes = {}) element =>
    <svg *:attributes,class:"blade-rpg-art" ++ (if (attributes.class == null) "" else " " ++ attributes.class),
        viewBox:"0 0 96 96",width:size,height:size,role:if (label == null) null else "img",
        ["aria-label"]:label,["aria-hidden"]:if (label == null) "true" else null,focusable:"false",
        ["stroke-linejoin"]:"round",["stroke-linecap"]:"round",*icons[string(name)]()>

pub fn corner(position) element => <svg class:"blade-corner blade-corner-" ++ position,viewBox:"0 0 64 64",
    ["aria-hidden"]:"true",focusable:"false",*[
    path("M3 61 L3 10 Q3 3 10 3 L61 3 L50 11 L18 11 L11 18 L11 50 Z","currentColor","#32251b",2),
    path("M5 51 L5 10 Q5 5 10 5 L51 5","none","#fae0a0",2),
    path("M14 47 Q35 48 27 29 Q21 16 35 17 Q45 18 41 29 Q38 36 32 32 M18 18 L36 9 M18 18 L9 36","none","currentColor",2),
    circle(10,10,3,"#f2d69b","#553c22",1),path("M14 14 L23 14 L14 23 Z","#645441")]>

pub fn meter_frame() element => <svg class:"blade-meter-frame",viewBox:"0 0 500 80",preserveAspectRatio:"none",
    ["aria-hidden"]:"true",focusable:"false",["stroke-linejoin"]:"round",*[
    path("M48 22 L222 22 L250 9 L278 22 L452 22 L466 40 L452 58 L278 58 L250 71 L222 58 L48 58 L34 40 Z","none","#211713",6),
    path("M49 25 L225 25 L250 14 L275 25 L451 25 L462 40 L451 55 L275 55 L250 66 L225 55 L49 55 L38 40 Z","none","currentColor",3),
    path("M52 29 L223 29 M277 29 L448 29 M52 51 L223 51 M277 51 L448 51","none","#fff0b4",1),
    *[for (side in [0,1]) <g transform:if (side == 0) "translate(0 0)" else "translate(500 0) scale(-1 1)",*[
        path("M42 23 C19 4 9 20 17 32 C23 42 35 29 24 27 C27 18 39 27 35 37 C28 56 5 45 9 34 M42 57 C24 74 8 59 20 50 M43 29 L7 18 L14 39 L2 45 L25 49 L42 53","none","currentColor",3),
        path("M40 23 Q24 11 20 19 M15 42 Q27 53 36 39","none","#ffe6a5",1.5)]>],
    path("M250 16 L258 25 L250 34 L242 25 Z","#ddbb70"),
    path("M250 19 L255 25 L250 30 L245 25 Z","#b54a48","#f6dc95",1)]>
