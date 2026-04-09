#ifndef PAGEBLOCJS_H
#define PAGEBLOCJS_H

#include "website/pages/blocs/AbstractPageBloc.h"

/**
 * A page bloc that holds raw HTML, CSS, and JavaScript fragments.
 *
 * All three fields are inserted into the page without any transformation:
 * - m_html is appended verbatim to the page body HTML.
 * - m_css  is appended verbatim to the page stylesheet.
 * - m_js   is appended verbatim to the page inline scripts.
 *
 * No shortcode expansion is performed; no paragraph wrapping is applied.
 * Designed for embedding self-contained interactive components (widgets,
 * calculators, mini-applications) into a page.
 */
class PageBlocJs : public AbstractPageBloc
{
public:
    static constexpr const char *KEY_HTML = "html";
    static constexpr const char *KEY_CSS  = "css";
    static constexpr const char *KEY_JS   = "js";

    PageBlocJs() = default;
    ~PageBlocJs() override = default;

    QString getName() const override;

    /**
     * Reads KEY_HTML, KEY_CSS, and KEY_JS from values.
     * Unknown keys are silently ignored.
     */
    void load(const QHash<QString, QString> &values) override;

    /** Writes m_html, m_css, and m_js under their respective keys. */
    void save(QHash<QString, QString> &values) const override;

    /**
     * Appends m_html verbatim to html, m_css verbatim to css, and m_js
     * verbatim to js.  origContent is ignored.  No shortcode expansion,
     * no paragraph wrapping, no CSS/JS deduplication — the caller is
     * responsible for ensuring the bloc is not rendered twice per page.
     */
    void addCode(QStringView     origContent,
                 AbstractEngine &engine,
                 int             websiteIndex,
                 QString        &html,
                 QString        &css,
                 QString        &js,
                 QSet<QString>  &cssDoneIds,
                 QSet<QString>  &jsDoneIds) const override;

    /** Returns a new PageBlocJsWidget; ownership is transferred to the caller. */
    AbstractPageBlockWidget *createEditWidget() override;

private:
    QString m_html;
    QString m_css;
    QString m_js;
};

#endif // PAGEBLOCJS_H
