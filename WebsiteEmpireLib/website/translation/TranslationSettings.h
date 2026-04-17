#ifndef TRANSLATIONSETTINGS_H
#define TRANSLATIONSETTINGS_H

#include <QDir>
#include <QStringList>

/**
 * Configuration for the translation pipeline.
 *
 * Loaded from translation_settings.json in the working directory.
 * The file must exist and contain at least one target language for
 * isConfigured() to return true.
 *
 * Expected JSON format:
 * @code
 * {
 *   "targetLangs":       ["de", "it", "es"],
 *   "limitPerRun":       200,
 *   "priorityPageTypes": ["article"]
 * }
 * @endcode
 *
 * targetLangs       — BCP-47 codes to translate source pages into
 * limitPerRun       — max jobs dispatched per --translate run; 0 = unlimited
 * priorityPageTypes — page type ids processed before all others in a run
 */
class TranslationSettings
{
public:
    static constexpr const char *FILENAME = "translation_settings.json";

    TranslationSettings() = default;
    explicit TranslationSettings(const QDir &workingDir);

    /** Returns true when the config file exists and targetLangs is non-empty. */
    bool isConfigured() const;

    QStringList targetLangs;        ///< BCP-47 codes to translate into
    int         limitPerRun  = 0;   ///< max jobs per run; 0 = unlimited
    QStringList priorityPageTypes;  ///< page type ids processed first
};

#endif // TRANSLATIONSETTINGS_H
