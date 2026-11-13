#pragma once
class QWidget;
namespace wf {
enum class Theme { System, Light, Dark };
// True when the desktop asks for dark applications (Windows setting, Qt 6.5+ colour
// scheme, or the freedesktop portal on Linux, which Qt 6.4 does not read itself).
bool systemPrefersDark();
// Applies the warm light or dark palette and style sheet; System follows the desktop.
void applyAppearance(QWidget &, Theme = Theme::System);
// The user's choice, kept in the app's config folder.
Theme savedTheme();
void saveTheme(Theme);
} // namespace wf
