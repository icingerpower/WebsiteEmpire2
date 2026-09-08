#ifndef ABSTRACTPAGEATTRIBUTES_H
#define ABSTRACTPAGEATTRIBUTES_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QMap>
#include <QSharedPointer>

#include <optional>

class QImage;


class AbstractPageAttributes : public QAbstractTableModel
{
    Q_OBJECT

public:
    AbstractPageAttributes(QObject *parent = nullptr);
    void init();
    using QAbstractTableModel::QAbstractTableModel;
    enum class Unit {
        Unit,
        Grams,
        Kg,
        Cup,
        Pound,
        Liter
    };
    struct ReferenceSpec {
        enum class Cardinality {
            Single,
            Multiple,
            MultipleWithQuantity
        };

        QString targetAttributeId;
        Cardinality cardinality;
        QSet<Unit> allowedUnits;
    };

    struct Schema { // Schema html markup
        QString name;
        std::function<QString(QString)> getValue = [](const QString &value) {
            return value; }; // In case the value in database is different than the value in the schema markup that could be standardized
        QSet<QString> validValues;
    };

    struct Attribute {
        QString id; // Mandatory and unique, can't change over time. Unique value for each attribute.
        QString name; // Mandatory. Can use tr as only id is used for saving / loading
        QString description; // Use tr
        QString valueExemple;
        QString valueDefault;
        std::function<QString(const QString &)> validate = [](const QString &value) {
            return value.isEmpty() ? QObject::tr("Value can't be empty") : QString{};
        }; // If a non-empty QString is returned, it is an error
        std::optional<Schema> schema = std::nullopt;
        bool optional = false;
        std::optional<ReferenceSpec> reference = std::nullopt;
        // If set, this attribute holds a list of images rather than a QString.
        // AspiredDb stores it as a BLOB and calls this instead of validate().
        bool isImage = false;
        std::optional<std::function<QString(const QList<QSharedPointer<QImage>> &)>> validateImageList = std::nullopt;
        // If true, this attribute holds the page URL (used by reparse to know
        // which URL to re-fetch for selected rows).  At most one attribute per
        // schema should have isUrl == true.
        bool isUrl = false;
    };

    virtual QString areAttributesCrossValid(
        const QHash<QString, QString> &id_values) const; // Only to check that there is no incompatible values

    virtual QString getId() const = 0;
    virtual QString getName() const = 0;
    virtual QString getDescription() const = 0;
    virtual QSharedPointer<QList<Attribute>> getAttributes() const = 0;

    // Composes a human-readable article topic string from one result row's
    // column values (keyed by Attribute::id, i.e. the DB column name).
    // Default returns an empty string — "not specialized"; callers (see
    // LauncherGeneration) fall back to their legacy single-column topic
    // derivation in that case. Only classes whose rows are NOT a single
    // human-readable name (e.g. multi-slot combination tables such as
    // PageAttributesFashionCombo*) need to override this.
    virtual QString composeArticleTopic(const QHash<QString, QString> &rowValues) const;

    /**
     * Returns false when this result row must NOT become an article, even
     * though it is valid research data. Default true — every row is eligible.
     *
     * Lets a table veto rows whose query formula does not make sense for the
     * specific values in that row (e.g. Fashion's "what shoes to wear with
     * {color} {product}" formula applied to a footwear product: "what shoes to
     * wear with black boots" — boots ARE shoes), or that are deliberately not
     * worth targeting. LauncherGeneration skips ineligible rows when building
     * its topic list; the rows themselves stay untouched in the aspire DB, so
     * nothing is lost and the veto can be relaxed later without regenerating
     * any research data.
     */
    virtual bool isArticleTopicEligible(const QHash<QString, QString> &rowValues) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    static const QMap<QString, const AbstractPageAttributes *> &ALL_PAGE_ATTRIBUTES();

    class Recorder {
    public:
        Recorder(AbstractPageAttributes *pageAttributes);
    };

protected:
    static const QStringList COLUMNS;
    static QMap<QString, const AbstractPageAttributes *> &getPageAttributes();
    QList<QStringList> m_listOfStringList;
};

#define DECLARE_PAGE_ATTRIBUTES(NEW_CLASS) \
NEW_CLASS instance##NEW_CLASS; \
    AbstractPageAttributes::Recorder recorder##NEW_CLASS{&instance##NEW_CLASS};

#endif // ABSTRACTPAGEATTRIBUTES_H
