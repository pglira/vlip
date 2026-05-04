#pragma once

#include "Project.hpp"
#include <QString>

namespace vlip {

class ProjectIO {
public:
    static bool save(const Project& p, const QString& path, QString* err = nullptr);
    static bool load(Project* p, const QString& path,
                     QStringList* warnings = nullptr,
                     QString* err = nullptr);
};

} // namespace vlip
