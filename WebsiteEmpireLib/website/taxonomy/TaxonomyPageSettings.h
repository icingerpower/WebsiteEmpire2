#ifndef TAXONOMYPAGESETTINGS_H
#define TAXONOMYPAGESETTINGS_H
#include <QDir>
#include <QList>
#include <QMap>
#include <QString>
class IPageRepository;
struct PageRecord;

// Per-taxonomy settings are separate from article strategies to prevent stale
// GUI models from overwriting each other's edits. Translation keys include the
// source hash so editing the closing text invalidates its old translations.
class TaxonomyPageSettings
{
public:
    explicit TaxonomyPageSettings(const QDir &workingDir);
    QString strategyId(const QString &taxonomyId) const;
    void setStrategyId(const QString &taxonomyId, const QString &id);
    QString closingText(const QString &taxonomyId) const;
    void setClosingText(const QString &taxonomyId, const QString &text);
    static QString translationKey(const QString &text);
    QMap<QString, QMap<QString, QString>> translationTemplates() const;
    QString translatedClosingText(const QString &taxonomyId, const QString &lang) const;
    QList<PageRecord> pendingPages(const QString &taxonomyId, const QString &lang,
                                  IPageRepository &repo) const;
private:
    QDir m_workingDir;
};
#endif
