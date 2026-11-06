#pragma once

struct RenderExportSession;
struct ViewPageSelection;

bool lambda_radiant_export_pdf(RenderExportSession* session, const char* filename,
                               const ViewPageSelection* selection);
