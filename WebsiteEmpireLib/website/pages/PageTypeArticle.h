#ifndef PAGETYPEARTICLE_H
#define PAGETYPEARTICLE_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocText.h"

/**
 * A page type composed of a single text bloc.
 *
 * The bloc list is stored as a member so getPageBlocs() can return a
 * const reference with zero allocation on every call.
 */
class PageTypeArticle : public AbstractPageType
{
public:
    PageTypeArticle();

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

private:
    PageBlocText m_textBloc;
    QList<const AbstractPageBloc *> m_blocs;
};

#endif // PAGETYPEARTICLE_H
