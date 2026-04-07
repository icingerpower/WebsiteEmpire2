#ifndef PAGETYPEHOME_H
#define PAGETYPEHOME_H

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/PageBlocText.h"
#include "website/pages/blocs/PageBlocImageLinks.h"

#include <QScopedPointer>

class CategoryTable;
class PageBlocCategory;

/**
 * Page type for the site home page.
 *
 * Blocs (in order):
 *   0 — PageBlocText       (intro text / hero copy)
 *   1 — PageBlocImageLinks (image link grid)
 *   2 — PageBlocCategory   (category tagging)
 *   3 — PageBlocText       (body text / secondary content)
 *
 * Registered in the AbstractPageType registry under TYPE_ID = "home".
 *
 * PageBlocCategory is a QObject and is therefore heap-allocated; the
 * QScopedPointer owns it.  The destructor is declared here and defined in
 * the .cpp so that QScopedPointer can see the full PageBlocCategory type at
 * the point of deletion without pulling it into this header.
 */
class PageTypeHome : public AbstractPageType
{
public:
    static constexpr const char *TYPE_ID      = "home";
    static constexpr const char *DISPLAY_NAME = "Home";

    explicit PageTypeHome(CategoryTable &categoryTable);
    ~PageTypeHome() override;

    QString getTypeId()      const override;
    QString getDisplayName() const override;

    const QList<const AbstractPageBloc *> &getPageBlocs() const override;

private:
    PageBlocText                     m_textBlocTop;
    PageBlocImageLinks               m_imageLinksBloc;
    QScopedPointer<PageBlocCategory> m_categoryBloc;
    PageBlocText                     m_textBlocBottom;
    QList<const AbstractPageBloc *>  m_blocs;
};

#endif // PAGETYPEHOME_H
