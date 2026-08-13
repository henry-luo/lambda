// D5.3.3: context-local URI and character caches own GC Items only through
// their registered exact root range. The forced-GC GTest runs this at every
// allocation so a stale cached Item is observed immediately.
for (let i = 0; i < 32; i++) {
  try {
    decodeURIComponent("%E0%00");
    throw new Error("decodeURIComponent accepted malformed input");
  } catch (error) {
    if (!(error instanceof URIError) || error.name !== "URIError" ||
        error.message !== "URI malformed") {
      throw new Error("decodeURIComponent lost its rooted URIError");
    }
  }

  try {
    decodeURI("%E0%00");
    throw new Error("decodeURI accepted malformed input");
  } catch (error) {
    if (!(error instanceof URIError) || error.name !== "URIError" ||
        error.message !== "URI malformed") {
      throw new Error("decodeURI lost its rooted URIError");
    }
  }

  const ascii = String.fromCharCode(65 + (i % 26));
  if (ascii.length !== 1 || ascii.charCodeAt(0) !== 65 + (i % 26)) {
    throw new Error("ASCII character cache returned a stale Item");
  }
  if (String.fromCharCode(0xD83D, 0xDE00) !== "😀") {
    throw new Error("four-byte character cache returned a stale Item");
  }
}

console.log("global-string-cache-roots-ok");
