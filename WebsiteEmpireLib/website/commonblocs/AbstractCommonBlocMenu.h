#ifndef ABSTRACTCOMMONBLOCMENU_H
#define ABSTRACTCOMMONBLOCMENU_H

#include "website/commonblocs/AbstractCommonBloc.h"
#include "website/commonblocs/MenuItem.h"

#include <QHash>
#include <QList>
#include <QString>

/**
 * Base class for menu common blocs (top and bottom navigation).
 *
 * Holds the ordered item list and provides:
 *  - setItems() / items() for the repository to push data before addCode()
 *  - createEditWidget() — returns a WidgetCommonBlocMenu (same widget for
 *    both top and bottom menus, since the editing UI is identical)
 *  - buildRel() static helper — appends "noopener noreferrer" when newTab is
 *    true and rel does not already contain "noopener", as required by Google /
 *    security best practices.
 *
 * Subclasses implement getId(), getName(), supportedScopes(), and addCode().
 */
class AbstractCommonBlocMenu : public AbstractCommonBloc
{
public:
    /** Replace the full item list. */
    void setItems(const QList<MenuItem> &items);

    /** Read-only access to the current item list. */
    const QList<MenuItem> &items() const;

    /** Returns a new WidgetCommonBlocMenu. Caller takes ownership. */
    AbstractCommonBlocWidget *createEditWidget() override;

    /**
     * Serializes m_items to a JSON string stored under the key "items".
     * Implemented here so both CommonBlocMenuTop and CommonBlocMenuBottom
     * share the same persistence logic.
     */
    QVariantMap toMap()              const override;
    void        fromMap(const QVariantMap &map) override;

    // ---- Translation overrides ----
    QHash<QString, QString> sourceTexts()                             const override;
    void        setTranslation(const QString &fieldId,
                               const QString &langCode,
                               const QString &translatedText)              override;
    QStringList missingTranslations(const QString &langCode,
                                    const QString &sourceLangCode)    const override;
    void        saveTranslations(QSettings &settings)                       override;
    void        loadTranslations(QSettings &settings)                       override;

    /**
     * Resolves the translated label for a menu item.
     * Returns the translated label if lang != sourceLang and a translation
     * exists, otherwise returns the original label.
     */
    const QString &resolveLabel(const QString &sourceLabel,
                                const QString &lang,
                                const QString &sourceLang) const;

protected:
    /**
     * Builds the final rel attribute value for an anchor.
     *
     * If newTab is true and rel does not already contain "noopener",
     * "noopener noreferrer" is appended (Google / security guideline).
     * Returns rel unchanged when newTab is false.
     */
    static QString buildRel(const QString &rel, bool newTab);

    QList<MenuItem> m_items;

    /**
     * Per-label translations: sourceLabel -> langCode -> translatedLabel.
     * Removing and re-adding a menu item with the same label preserves
     * its translations.
     */
    QHash<QString, QHash<QString, QString>> m_labelTr;
};

#endif // ABSTRACTCOMMONBLOCMENU_H
