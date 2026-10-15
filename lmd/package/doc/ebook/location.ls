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
fn matches(book,chapter,query,start,remaining) array^ {
    let relative = index_of(slice(chapter.text,start,len(chapter.text)),query);
    if (relative == null or remaining <= 0) [] else {
        let offset = start+relative;
        [{locator:at(book,chapter,offset)^,length:len(query),snippet:slice(chapter.text,max(0,offset-24),min(len(chapter.text),offset+len(query)+48))},
            *matches(book,chapter,query,offset+max(1,len(query)),remaining-1)^]
    }
}
fn search_range(book,query,start,end,remaining) array^ {
    if (remaining <= 0 or start >= end) []
    else if (end-start == 1) matches(book,chapters.load(book,start)^,query,0,remaining)^
    else {
        let middle = start+int(floor((end-start)/2))
        let left = search_range(book,query,start,middle,remaining)^;
        left ++ search_range(book,query,middle,end,remaining-len(left))^
    }
}
pub fn search(book,query,options = null) array^ {
    let opts = c.checked_options(options,["limit"])^;
    if (not (query is string) or trim(query) == "") [] else {
        let limit = c.get(opts,"limit",100)
        let valid = if (not (limit is int) or limit < 1 or limit > 1000)
            raise c.fail("invalid-options","Search limit must be 1–1000") else true;
        search_range(book,query,0,len(book.reading_order),limit)^
    }
}
