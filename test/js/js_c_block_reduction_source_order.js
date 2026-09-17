// A contextual `of` arrow parameter must remain in its enclosing declaration.
(function () {
    let githubApiUrl = "https://api.github.com/repos/scala/docs.scala-lang/commits";
    let commitsUrl = "https://api.github.com/repos/scala/scala-lang/commits";
    let identiconsUrl = "https://github.com/identicons";
    let rootFiles = ["getting-started", "learn", "glossary"];
    let overviewsFolders = ["FAQ", "tutorials"];
    let thisPageUrl = "tutorials/FAQ";
    thisPageUrl = thisPageUrl.substring(0, thisPageUrl.length);
    let isRootFile = rootFiles.some(rf => thisPageUrl.startsWith(rf));
    let isInOverviewsFolder = overviewsFolders.some(of => thisPageUrl.startsWith(of));
    if (isRootFile) {
        thisPageUrl = thisPageUrl + ".md";
    } else if (thisPageUrl.indexOf("tutorials/FAQ") === 0) {
        thisPageUrl = "_overviews/" + thisPageUrl.substring("tutorials/".length) + ".md";
    } else if (isInOverviewsFolder) {
        thisPageUrl = "_overviews/" + thisPageUrl + ".md";
    } else {
        thisPageUrl = "_" + thisPageUrl + ".md";
    }
    let url = githubApiUrl + "?path=" + thisPageUrl;
    let fallback = commitsUrl + identiconsUrl.length;
    console.log(thisPageUrl + "|" + (url.length > fallback.length));
})();
