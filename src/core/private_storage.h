#pragma once
#include <QString>
namespace reword {
// Reject unsafe paths and stop initialization if account-only access cannot be established.
void validateStorageLocation(const QString &path);
void validateExistingStorageFile(const QString &path);
void restrictStoragePath(const QString &path, bool directory);
} // namespace reword
