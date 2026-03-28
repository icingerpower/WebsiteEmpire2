#ifndef PAGEATTRIBUTESLANGCODE_H
#define PAGEATTRIBUTESLANGCODE_H

#include "../AbstractPageAttributes.h"

// Page attributes for a language code entry (e.g. BCP-47 "fr", "en", "de").
// Used as a reference target from LangIdiom and LangWord.
class PageAttributesLangCode : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the BCP-47 code — used as the reference target
    // in LangIdiom and LangWord ReferenceSpec declarations.
    static const QString ID_CODE;

    // English display name, e.g. "French".
    static const QString ID_NAME;

    // Name as written in the language itself, e.g. "Français".
    static const QString ID_NATIVE_NAME;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESLANGCODE_H
