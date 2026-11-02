#pragma once
struct DomDocument;

// document-realm monotonic milliseconds; returns true for a virtual clock.
// a missing document realm uses the thread's event loop, as the window host does.
bool radiant_document_clock(DomDocument* document, double* now_ms);
