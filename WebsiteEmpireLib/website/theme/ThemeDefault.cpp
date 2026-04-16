#include "ThemeDefault.h"

DECLARE_THEME(ThemeDefault)

ThemeDefault::ThemeDefault()
    : AbstractTheme()
{}

ThemeDefault::ThemeDefault(const QDir &workingDir, QObject *parent)
    : AbstractTheme(workingDir, parent)
{
    _loadBlocsData();
}

AbstractTheme *ThemeDefault::create(const QDir &workingDir, QObject *parent) const
{
    return new ThemeDefault(workingDir, parent);
}

QString ThemeDefault::getId() const
{
    return QStringLiteral("default");
}

QString ThemeDefault::getName() const
{
    return tr("Default");
}

QList<AbstractCommonBloc *> ThemeDefault::getTopBlocs()
{
    return {&m_header, &m_menuTop};
}

QList<AbstractCommonBloc *> ThemeDefault::getBottomBlocs()
{
    return {&m_menuBottom, &m_footer, &m_cookies};
}

QList<Param> ThemeDefault::getParams() const
{
    return {
        {
            QStringLiteral("primary_color"),
            tr("Primary colour"),
            tr("Main brand colour used for headings, links, and primary buttons."),
            QVariant(QStringLiteral("#1a73e8"))
        },
        {
            QStringLiteral("secondary_color"),
            tr("Secondary colour"),
            tr("Accent colour used for highlights, badges, and secondary buttons."),
            QVariant(QStringLiteral("#fbbc04"))
        },
        {
            QStringLiteral("font_family"),
            tr("Font family"),
            tr("CSS font-family value applied to the body element (e.g. \"sans-serif\", \"Georgia, serif\")."),
            QVariant(QStringLiteral("sans-serif"))
        },
        {
            QStringLiteral("font_size_base"),
            tr("Base font size"),
            tr("CSS font-size applied to the body element (e.g. \"16px\" or \"1rem\")."),
            QVariant(QStringLiteral("16px"))
        },
        {
            QStringLiteral("font_url"),
            tr("Google Fonts URL"),
            tr("Full URL to a Google Fonts stylesheet. Leave blank to use the system font."),
            QVariant(QStringLiteral("https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap"))
        },
        {
            QStringLiteral("max_content_width"),
            tr("Max content width"),
            tr("Maximum width of the page content area (e.g. \"860px\" or \"72ch\")."),
            QVariant(QStringLiteral("860px"))
        },
        {
            QStringLiteral("body_bg_color"),
            tr("Body background colour"),
            tr("Background colour of the full page behind the content."),
            QVariant(QStringLiteral("#f4f6fb"))
        },
        {
            QStringLiteral("body_text_color"),
            tr("Body text colour"),
            tr("Default text colour for body copy."),
            QVariant(QStringLiteral("#1f2937"))
        },
    };
}
