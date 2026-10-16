#pragma once
#include "core/core.h"
#include <memory>
namespace reword {
class Adapter {
  public:
    virtual ~Adapter() = default;
    virtual Field inspect(bool includeText) = 0;
    virtual bool changed() {
        return true;
    }
    virtual bool replace(const Field &expected, const QString &newText, int newCaret) = 0;
    virtual QString description() const = 0;
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
