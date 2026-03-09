#ifndef ABSTRACTDOWNLOADER_H
#define ABSTRACTDOWNLOADER_H

#include <QMap>
#include <QObject>
#include <QSharedPointer>


class AbstractDownloader : public QObject
{
    Q_OBJECT

public:
    AbstractDownloader(QObject *parent = nullptr);

    virtual QString getId() const = 0;
    virtual QString getName() const = 0;
    virtual QString getDescription() const = 0;

    virtual QStringList getUrlsToParse(const QString &content) const = 0; // Return the next links to parse from one page content
    virtual QHash<QString, QString> getAttributeValues(const QString &content) const = 0; // Return for each AbstractPageAttributes::Attribute::id a QString value

    static const QMap<QString, const AbstractDownloader *> &ALL_DOWNLOADERS();

    class Recorder {
    public:
        Recorder(AbstractDownloader *downloader);
    };

protected:
    static QMap<QString, const AbstractDownloader *> &getDownloaders();
};

#define DECLARE_DOWNLOADER(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    AbstractDownloader::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTDOWNLOADER_H
