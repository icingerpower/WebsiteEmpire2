#ifndef PAGEBLOCFASHIONTAXONOMYLINKSWIDGET_H
#define PAGEBLOCFASHIONTAXONOMYLINKSWIDGET_H

#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

#include <QHash>
#include <QStringList>

class QListWidget;

/**
 * Editor widget for PageBlocFashionTaxonomyLinks.
 *
 * One labeled, checkable list per dimension (Color, Season, Occasion,
 * Material, Style Aesthetic), stacked vertically — vocabByDimension supplies
 * each dimension's synced items, keyed by taxonomyId (pre-loaded from the
 * local taxonomy store by the caller, one PageBlocFashionTaxonomyLinks::
 * dimensions() entry per key).
 *
 * A dimension with an empty vocabulary shows a label directing the user to
 * the Taxonomies tab instead of an empty list, mirroring
 * PageBlocSymptomLinksWidget's single-dimension fallback.
 *
 * load()/save() key each dimension's checked names under its taxonomyId,
 * matching PageBlocFashionTaxonomyLinks::load()/save()'s storage keys.
 */
class PageBlocFashionTaxonomyLinksWidget : public AbstractPageBlockWidget
{
    Q_OBJECT

public:
    explicit PageBlocFashionTaxonomyLinksWidget(
        const QHash<QString, QStringList> &vocabByDimension, QWidget *parent = nullptr);
    ~PageBlocFashionTaxonomyLinksWidget() override = default;

    void load(const QHash<QString, QString> &values) override;
    void save(QHash<QString, QString> &values) const override;

private:
    // taxonomyId -> its checkable list (absent when that dimension had no
    // synced vocabulary and a fallback label was shown instead).
    QHash<QString, QListWidget *> m_listsByDimension;
};

#endif // PAGEBLOCFASHIONTAXONOMYLINKSWIDGET_H
