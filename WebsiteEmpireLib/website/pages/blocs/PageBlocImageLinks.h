#ifndef PAGEBLOCIMAGELINKS_H
#define PAGEBLOCIMAGELINKS_H

#include "website/pages/blocs/AbstractPageBloc.h"

#include <QList>

/**
 * A page bloc that displays a configurable image-link grid.
 *
 * Each item has an image URL, a link type (category / page / url), a link
 * target, and alt text.  The grid uses CSS custom properties for responsive
 * column counts at desktop, tablet and mobile breakpoints.
 *
 * addCode() emits a <section class="image-links-grid"> with one <a> per item
 * that has a non-empty imageUrl.  If every item has an empty imageUrl, nothing
 * is emitted at all (no HTML, CSS or JS).
 *
 * CSS and JS are appended once per page via cssDoneIds / jsDoneIds with the
 * shared ID "page-bloc-image-links".
 *
 * JS uses IntersectionObserver + navigator.sendBeacon to track display rate
 * and click rate on each link.
 *
 * Link resolution:
 *   "category" -> /category/{target}
 *   "page"     -> /{target}
 *   "url"      -> target as-is
 */
class PageBlocImageLinks : public AbstractPageBloc
{
public:
    // ── Serialisation keys ──────────────────────────────────────────────
    static constexpr const char *KEY_COLS_DESKTOP = "cols_desktop";
    static constexpr const char *KEY_ROWS_DESKTOP = "rows_desktop";
    static constexpr const char *KEY_COLS_TABLET  = "cols_tablet";
    static constexpr const char *KEY_ROWS_TABLET  = "rows_tablet";
    static constexpr const char *KEY_COLS_MOBILE  = "cols_mobile";
    static constexpr const char *KEY_ROWS_MOBILE  = "rows_mobile";
    static constexpr const char *KEY_ITEM_COUNT   = "item_count";

    // ── Link type constants ─────────────────────────────────────────────
    static constexpr const char *LINK_TYPE_CATEGORY = "category";
    static constexpr const char *LINK_TYPE_PAGE     = "page";
    static constexpr const char *LINK_TYPE_URL      = "url";

    // ── Item struct ─────────────────────────────────────────────────────
    struct Item {
        QString imageUrl;
        QString linkType;   ///< one of the LINK_TYPE_* constants
        QString linkTarget;
        QString altText;
    };

    PageBlocImageLinks() = default;
    ~PageBlocImageLinks() override = default;

    QString getName() const override;

    /**
     * Reads grid settings and items from the flat key-value map.
     * Unknown keys are silently ignored.
     */
    void load(const QHash<QString, QString> &values) override;

    /**
     * Writes grid settings and all items (including those with empty imageUrl)
     * into the flat key-value map.
     */
    void save(QHash<QString, QString> &values) const override;

    /**
     * Emits responsive image-link grid HTML, CSS (once) and JS (once).
     * Items with empty imageUrl are skipped.  If ALL items are skipped,
     * nothing is emitted at all.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    AbstractPageBlockWidget *createEditWidget() override;

    // ── Accessors ───────────────────────────────────────────────────────
    const QList<Item> &items() const;

    int colsDesktop() const;
    int rowsDesktop() const;
    int colsTablet() const;
    int rowsTablet() const;
    int colsMobile() const;
    int rowsMobile() const;

private:
    int m_colsDesktop = 4;
    int m_rowsDesktop = 2;
    int m_colsTablet  = 2;
    int m_rowsTablet  = 3;
    int m_colsMobile  = 1;
    int m_rowsMobile  = 4;

    QList<Item> m_items;

    /**
     * Resolves a link type + target pair into the final href value.
     *   "category" -> /category/{target}
     *   "page"     -> /{target}
     *   "url"      -> target as-is
     */
    static QString resolveHref(const QString &linkType, const QString &linkTarget);
};

#endif // PAGEBLOCIMAGELINKS_H
