#pragma once

// Install a one-shot filter on the krnl logger so repeated
// Keep the first known optional-file miss, plus sparse repeat counts.
void InstallLangOverrideLogFilter();
void ReportSuppressedLangOverrideWarnings();
