#ifndef PAGETYPEARTICLE_H
#define PAGETYPEARTICLE_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocText.h"

#include <QScopedPointer>

class CategoryTable;
class PageBlocCategory;

/**
 * A page type composed of a category bloc followed by a text bloc.
 *
 * Registered in the AbstractPageType registry under TYPE_ID = "article".
 *
 * The category bloc is first so getAttributes() returns the page's selected
 * categories before any text-bloc attributes.
 *
 * PageBlocCategory is a QObject and is therefore heap-allocated; the
 * QScopedPointer owns it.  The destructor is declared here and defined in the
 * .cpp so that QScopedPointer can see the full PageBlocCategory type at the
 * point of deletion without pulling it into this header.
 */
class PageTypeArticle : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "article";
    static constexpr const char *DISPLAY_NAME = "Article";

    explicit PageTypeArticle(CategoryTable &categoryTable);
    ~PageTypeArticle() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

private:
    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_textBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEARTICLE_H
