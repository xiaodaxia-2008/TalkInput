#pragma once

#include <QString>

namespace talkinput
{

/// Extract meaningful terms (proper nouns, file names, abbreviations, etc.)
/// from raw OCR recognition text, filtering out noise artifacts.
QString extractOcrWords(const QString &text);

} // namespace talkinput
