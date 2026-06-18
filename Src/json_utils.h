#pragma once

#include <QString>
#include <nlohmann/json.hpp>

inline void to_json(nlohmann::json &j, const QString &value)
{
    j = value.toStdString();
}

inline void from_json(const nlohmann::json &j, QString &value)
{
    if (j.is_string()) {
        value = QString::fromStdString(j.get<std::string>());
    }
    else {
        value.clear();
    }
}
