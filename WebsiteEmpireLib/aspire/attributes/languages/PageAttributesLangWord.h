#ifndef PAGEATTRIBUTESLANGWORD_H
#define PAGEATTRIBUTESLANGWORD_H

#include "../AbstractPageAttributes.h"

// Page attributes for a vocabulary word in a given language.
// Rank captures corpus frequency so that lesson pages can present the most
// useful words first.  Tags and Idioms allow the website generator to build
// contextual exercises (e.g. "words tagged 'food'" or "phrases that use
// this word").
class PageAttributesLangWord : public AbstractPageAttributes
{
    Q_OBJECT

public:
    // Stable attribute ID for the word text — used as the reference target
    // from other tables that may link to individual words.
    static const QString ID_WORD;

    // Corpus frequency rank (1 = most frequent).
    static const QString ID_RANK;

    static const QString ID_LANG_CODE;
    static const QString ID_TAGS;

    // Idioms in which this word appears.  Lets the website generator display
    // "also learn these phrases" alongside a word page.
    static const QString ID_IDIOMS;

    QString getId() const override;
    QString getName() const override;
    QString getDescription() const override;
    QSharedPointer<QList<Attribute>> getAttributes() const override;
};

#endif // PAGEATTRIBUTESLANGWORD_H
