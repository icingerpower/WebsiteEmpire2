#ifndef PAGEATTRIBUTESLANGIDIOM_H
#define PAGEATTRIBUTESLANGIDIOM_H

#include "../AbstractPageAttributes.h"

// Page attributes for a language idiom or fixed phrase (e.g. "it's raining cats and dogs").
// Each record belongs to a single language and carries one or more tags describing its
// theme/situation.  Used as a reference target from LangWord (words can list the idioms
// they appear in, allowing "also learn these phrases" on a word page).
class PageAttributesLangIdiom : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the phrase text — used as the reference target
    // in LangWord ReferenceSpec declarations.
    static const QString ID_PHRASE;

    static const QString ID_LANG_CODE;
    static const QString ID_TAGS;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESLANGIDIOM_H
