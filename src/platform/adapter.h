#pragma once
#include "core/core.h"
#include <functional>
#include <memory>
namespace reword {
using WriteAuthorization = std::function<bool()>;
inline constexpr int WriteAuthorizationBudgetMs = 50;

struct WriteResult {
    bool accepted = false; // The provider acknowledged the edit.
    Field after;          // Observed at the original target, if still readable.
    explicit operator bool() const { return accepted; }
};

struct InputMethodTarget {
    QString app, field;
    quint32 window = 0;
};

class Adapter {
  public:
    virtual ~Adapter() = default;
    virtual Field inspect(bool includeText) = 0;
    // Read only the metadata-authorized target. Implementations must compare
    // app/field identity before requesting text; unsupported adapters refuse.
    virtual Field readExpected(const Field &) {
        return {};
    }
    // Metadata witness for a separate X11 input method. Never reads text.
    virtual InputMethodTarget inputMethodTarget() { return {}; }
    virtual bool changed() {
        return true;
    }
    // Ask after native target validation, immediately before submitting the
    // edit. A missing or denied authorization must leave the target untouched.
    // The callback can be invoked again after a provider recheck.
    virtual WriteResult replace(const Field &expected, const QString &newText, int newCaret,
                                const WriteAuthorization &authorize) = 0;
    virtual QString description() const = 0;
    // "pending" means a native shortcut still owns the keyboard/focus grace.
    // Callers must avoid text observation until an action or an empty result.
    virtual QString takeAction() {
        return {};
    }
    virtual bool shortcuts(const QString &, const QString &, const QString &) {
        return false;
    }
};
std::unique_ptr<Adapter> createAdapter();
#ifndef _WIN32
std::unique_ptr<Adapter> createNativeAdapter();
#endif
bool browserApp(const QString &id);
bool restrictedApp(const QString &id);
} // namespace reword
