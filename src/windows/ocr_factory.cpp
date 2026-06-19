#include "ocr_recognizer.h"
#include "system_ocr_recognizer.h"

namespace talkinput
{

OcrRecognizer *createOcrRecognizer(QObject *parent)
{
    return new SystemOcrRecognizer(parent);
}

} // namespace talkinput
