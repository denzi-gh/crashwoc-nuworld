#include "platform/crashpad.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

#include "client/crash_report_database.h"
#include "client/crashpad_client.h"

using crashpad::CrashReportDatabase;

bool nw_crashpad_start(const wchar_t *handler, const wchar_t *database, const char *version, const char *commit)
{
    static crashpad::CrashpadClient client;
    std::map<std::string, std::string> annotations = { { "version", version }, { "commit", commit } };
    return client.StartHandler(base::FilePath(handler), base::FilePath(database), base::FilePath(), std::string(),
                               std::string(), annotations, {}, false, false);
}

void nw_crashpad_prune(const wchar_t *database, unsigned keep)
{
    auto db = CrashReportDatabase::InitializeWithoutCreating(base::FilePath(database));
    std::vector<CrashReportDatabase::Report> reports, pending;
    if (!db || db->GetCompletedReports(&reports) != CrashReportDatabase::kNoError ||
        db->GetPendingReports(&pending) != CrashReportDatabase::kNoError) {
        return;
    }
    reports.insert(reports.end(), pending.begin(), pending.end());
    std::sort(reports.begin(), reports.end(),
              [](const auto &a, const auto &b) { return a.creation_time > b.creation_time; });
    for (size_t i = keep; i < reports.size(); i++) {
        db->DeleteReport(reports[i].uuid);
    }
}

void nw_crashpad_dump(void *exception)
{
    crashpad::CrashpadClient::DumpAndCrash(static_cast<EXCEPTION_POINTERS *>(exception));
}
