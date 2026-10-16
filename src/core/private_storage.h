#pragma once
#include <QString>
namespace reword {
// Reject unsafe paths and stop initialization if account-only access cannot be established.
void restrictStoragePath(const QString &path, bool directory);
} // namespace reword
