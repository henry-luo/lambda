import c: .common
import chapters: .chapter

pub fn at(book,chapter,offset) map^ {
    let valid = if (not (offset is int) or offset < 0 or offset > len(chapter.text))
        raise c.fail("invalid-location","Text offset is outside the chapter",chapter.href) else true
    let segment = c.first([for (segment in chapter.source_map where
        offset >= segment.offset and offset < segment.offset+len(segment.text)) segment]);
    {version:1,publication_fingerprint:book.source_identity,rendition:book.rendition,
        spine_occurrence:chapter.occurrence,href:chapter.href,node:segment.node,
        source_text_position:offset,quote:slice(chapter.text,offset,min(len(chapter.text),offset+32))}
}
pub fn resolve(book,locator) map^ {
    if (not (locator is map) or locator.version != 1 or locator.publication_fingerprint != book.source_identity or locator.rendition != book.rendition or
        not (locator.spine_occurrence is int) or locator.spine_occurrence < 0 or
        locator.spine_occurrence >= len(book.reading_order) or
        book.reading_order[locator.spine_occurrence].href != locator.href)
        raise c.fail("invalid-location","Locator does not identify this publication snapshot")
    else {
        let chapter = chapters.load(book,locator.spine_occurrence)^
        let result = at(book,chapter,locator.source_text_position)^;
        if (result.quote != locator.quote or result.node != locator.node)
            raise c.fail("invalid-location","The bookmarked passage could not be verified",locator.href)
        else result
    }
}
// a word character for whole-word matching: a digit, an underscore, or a cased letter
fn word_char(ch) => ch != "" and (ch == "_" or (ch >= "0" and ch <= "9") or lower(ch) != upper(ch))
fn whole_word_at(text,offset,length) => not word_char(if (offset > 0) slice(text,offset-1,offset) else "") and
    not word_char(slice(text,offset+length,min(len(text),offset+length+1)))
// excerpts read as one line: line breaks become spaces and space runs collapse
fn spaces(text) => if (contains(text,"  ")) spaces(replace(text,"  "," ")) else text
fn flat(text) => spaces(replace(replace(replace(text,"\n"," "),"\r"," "),"\t"," "))
// The match with up to ~40 characters before and ~70 after, cut at word
// boundaries so neither side opens or closes mid-word.
fn excerpt(text,offset,length) {
    let from = max(0,offset-40)
    let to = min(len(text),offset+length+70)
    let before = flat(slice(text,from,offset))
    let after = flat(slice(text,offset+length,to))
    let lead = if (from == 0 or not contains(before," ")) before else "…" ++ join(drop(split(before," "),1)," ")
    let pieces = split(after," ")
    let tail = if (to == len(text) or len(pieces) < 2) after else join(take(pieces,len(pieces)-1)," ") ++ "…";
    {before:lead,match:flat(slice(text,offset,offset+length)),after:tail}
}
// Case-insensitive search compares lowercase text; a chapter whose lowercase
// form changes length (e.g. U+0130) is searched exactly so offsets stay valid.
fn matches(book,chapter,query,opts,remaining) array^ {
    let folded = if (opts.match_case) null else lower(chapter.text)
    let exact = opts.match_case or len(folded) != len(chapter.text)
    let hits = find(if (exact) chapter.text else folded,if (exact) query else lower(query))
    let kept = take([for (hit in hits where not opts.whole_word or whole_word_at(chapter.text,hit.index,len(query))) hit.index],remaining);
    [for (offset in kept) {*:excerpt(chapter.text,offset,len(query)),locator:at(book,chapter,offset)^,length:len(query),
        chapter:chapter.occurrence,snippet:slice(chapter.text,max(0,offset-24),min(len(chapter.text),offset+len(query)+48))}]
}
fn search_range(book,query,opts,start,end,remaining) array^ {
    if (remaining <= 0 or start >= end) []
    else if (end-start == 1) matches(book,chapters.load(book,start)^,query,opts,remaining)^
    else {
        let middle = start+int(floor((end-start)/2))
        let left = search_range(book,query,opts,start,middle,remaining)^;
        left ++ search_range(book,query,opts,middle,end,remaining-len(left))^
    }
}
// match_case defaults to exact matching; chapter limits the search to one spine occurrence.
pub fn search(book,query,options = null) array^ {
    let opts = c.checked_options(options,["limit","match_case","whole_word","chapter"])^;
    if (not (query is string) or trim(query) == "") [] else {
        let limit = c.get(opts,"limit",100)
        let chapter = c.get(opts,"chapter")
        let valid = if (not (limit is int) or limit < 1 or limit > 1000)
            raise c.fail("invalid-options","Search limit must be 1–1000")
            else if (chapter != null and (not (chapter is int) or chapter < 0 or chapter >= len(book.reading_order)))
            raise c.fail("invalid-options","Search chapter must be a spine index") else true
        let flags = {match_case:c.get(opts,"match_case",true) != false,whole_word:c.get(opts,"whole_word",false) == true};
        if (chapter != null) search_range(book,query,flags,chapter,chapter+1,limit)^
        else search_range(book,query,flags,0,len(book.reading_order),limit)^
    }
}
