#include "ocr_recognizer.h"
#include "system_ocr_service.h"

namespace talkinput
{

OcrRecognizer *createOcrRecognizer(QObject *parent)
{
    return new SystemOcrService(parent);
}

} // namespace talkinput
