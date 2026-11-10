#pragma once

#include "browser_extension_origin.h"
#include <QJsonArray>
#include <QJsonObject>

namespace reword {
inline constexpr qint64 MaxBrowserManifestBytes = 8192;

inline QJsonObject browserManifestIdentity() {
    return {{"name", BrowserHostName}, {"description", "Reword offline browser correction"},
            {"type", "stdio"}, {"allowed_origins", QJsonArray{BrowserExtensionOrigin}}};
}
}
