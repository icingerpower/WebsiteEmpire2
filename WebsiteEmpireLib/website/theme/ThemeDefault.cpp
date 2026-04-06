#include "ThemeDefault.h"

DECLARE_THEME(ThemeDefault)

ThemeDefault::ThemeDefault()
    : AbstractTheme()
{}

ThemeDefault::ThemeDefault(const QDir &workingDir, QObject *parent)
    : AbstractTheme(workingDir, parent)
{}

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
    return {&m_menuBottom, &m_footer};
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
    };
}
