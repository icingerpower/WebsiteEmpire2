#ifndef PAGEBLOCTAXONOMYARTICLE_H
#define PAGEBLOCTAXONOMYARTICLE_H
#include "PageBlocText.h"
#include <QDir>

// Uses the regular article translation protocol; the shared closing text is
// translated independently by --translateCommon and rendered before the links.
class PageBlocTaxonomyArticle : public PageBlocText
{
public:
    void bindContext(const QDir &workingDir, const QString &taxonomyId);
    void addCode(QStringView content, AbstractEngine &engine, int websiteIndex,
                 QString &html, QString &css, QString &js,
                 QSet<QString> &cssDoneIds, QSet<QString> &jsDoneIds) const override;
private:
    QDir m_workingDir;
    QString m_taxonomyId;
};
#endif

