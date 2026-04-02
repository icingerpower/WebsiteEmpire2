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
 * The category bloc is first so getAttributes() returns the page's selected
 * categories before any text-bloc attributes.
 *
 * PageBlocCategory is a QObject and is therefore heap-allocated; the
 * QScopedPointer owns it.  The destructor is declared here and defined in the
 * .cpp so that QScopedPointer can see the full PageBlocCategory type at the
 * point of deletion without pulling it into this header.
 *
 * The bloc list is stored as a member so getPageBlocs() can return a
 * const reference with zero allocation on every call.
 */
class PageTypeArticle : public AbstractPageType
{
public:
    explicit PageTypeArticle(CategoryTable &categoryTable);
    ~PageTypeArticle() override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

private:
    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_textBloc;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEARTICLE_H
