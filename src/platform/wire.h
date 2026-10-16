#pragma once
#include "adapter.h"
#include <QJsonObject>
namespace reword {
inline QJsonObject encodeField(const Field &f) {
    return {{"app", f.app},
            {"id", f.id},
            {"text", f.text},
            {"caret", f.caret},
            {"selectionStart", f.selectionStart},
            {"selectionEnd", f.selectionEnd},
            {"known", f.known},
            {"password", f.password},
            {"editable", f.editable},
            {"focused", f.focused},
            {"locked", f.locked},
            {"plain", f.plain}};
}
inline Field decodeField(const QJsonObject &o) {
    Field f;
    f.app = o["app"].toString();
    f.id = o["id"].toString();
    f.text = o["text"].toString();
    f.caret = o["caret"].toInt(-1);
    f.selectionStart = o["selectionStart"].toInt(-1);
    f.selectionEnd = o["selectionEnd"].toInt(-1);
    f.known = o["known"].toBool();
    f.password = o["password"].toBool(true);
    f.editable = o["editable"].toBool();
    f.focused = o["focused"].toBool();
    f.locked = o["locked"].toBool(true);
    f.plain = o["plain"].toBool();
    return f;
}
} // namespace reword
