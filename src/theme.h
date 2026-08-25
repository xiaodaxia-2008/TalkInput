#pragma once

#include <QIcon>
#include <QString>

#include <string>

namespace zenny
{

enum class ThemeMode
{
    System,
    Light,
    Dark
};

ThemeMode themeModeFromString(const std::string &s);
std::string themeModeToString(ThemeMode mode);

/// Resolves System to Light/Dark using the current OS color scheme.
bool isDarkTheme(ThemeMode mode);

/// Applies the palette and stylesheet for @p mode and returns whether the
/// resulting effective theme is dark.
bool applyTheme(ThemeMode mode);

/// Loads the Feather-style SVG (stroke "#333") recolored for the current
/// theme. Used for navigation icons so they remain visible in dark mode.
QIcon themedNavIcon(const QString &svgPath, bool dark);

} // namespace zenny
