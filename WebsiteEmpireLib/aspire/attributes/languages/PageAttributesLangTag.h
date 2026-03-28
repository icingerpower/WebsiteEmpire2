#ifndef PAGEATTRIBUTESLANGTAG_H
#define PAGEATTRIBUTESLANGTAG_H

#include "../AbstractPageAttributes.h"

// Page attributes for a language learning tag (theme, grammar category, situation,
// word type such as pronoun, etc.).
// Used as a reference target from LangIdiom and LangWord to allow grouping and
// ordering content by learner interest (via Percentage).
class PageAttributesLangTag : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the tag name — used as the reference target
    // in LangIdiom and LangWord ReferenceSpec declarations.
    static const QString ID_NAME;

    // Estimated percentage of learners interested in this tag.
    // Used to sort tags from most to least important when generating pages.
    static const QString ID_PERCENTAGE;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESLANGTAG_H
