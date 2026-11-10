#pragma once

#include <QString>

namespace reword {
void registerBrowserHost(const QString &profile, const QString &executable, bool remove);
#ifdef _WIN32
// Removal with an executable is scoped to that installation.
void registerWindowsBrowserHost(const QString &browser, const QString &dataDirectory,
                                const QString &executable, bool remove);
#endif
}
