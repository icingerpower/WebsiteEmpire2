#ifndef RASTERCLIPROTOCOL_H
#define RASTERCLIPROTOCOL_H

#include <QString>

class QByteArray;

// Antigravity can exit 0 and report result.status=SUCCESS after generate_image
// failed. Tool events, not the agent's final prose, determine image success.
// Only used for raster generation/review; article and SVG protocols are unchanged.
class RasterCliProtocol
{
public:
    // Returns final text, or __ERROR__ with the underlying tool error. Image
    // generation requires an observed successful generate_image call. An error
    // remains fatal even if the agent subsequently creates a substitute file.
    // Reviews require an observed successful view_file call, rather than a
    // verdict inferred from the prompt without inspecting the image.
    static QString decodeAntigravity(const QByteArray &output, bool requireImage,
                                    const QString &expectedImagePath = {});
};

#endif
