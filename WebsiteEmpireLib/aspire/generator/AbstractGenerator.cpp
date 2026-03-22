#include "AbstractGenerator.h"

#include <QJsonDocument>
#include <QSet>

#include "aspire/attributes/AbstractPageAttributes.h"
#include "aspire/downloader/DownloadedPagesTable.h"

AbstractGenerator::AbstractGenerator(const QDir &workingDir, QObject *parent)
    : QObject(parent)
    , m_workingDir(workingDir)
{
}

const QDir &AbstractGenerator::workingDir() const
{
    return m_workingDir;
}

QSettings &AbstractGenerator::settings() const
{
    if (!m_settings) {
        m_settings.reset(new QSettings(
            m_workingDir.filePath(getId() + QStringLiteral(".ini")),
            QSettings::IniFormat));
    }
    return *m_settings;
}

void AbstractGenerator::loadState()
{
    if (m_stateLoaded) {
        return;
    }
    m_stateLoaded = true;

    const QStringList allIds = buildInitialJobIds();
    const QStringList discovered = settings().value(QStringLiteral("Discovered/ids")).toStringList();
    const QStringList doneList = settings().value(QStringLiteral("Done/ids")).toStringList();
    const QSet<QString> done(doneList.begin(), doneList.end());

    // Pending = (initial jobs + previously discovered jobs) − done, order preserved.
    QStringList combined = allIds;
    for (const QString &id : discovered) {
        if (!combined.contains(id)) {
            combined << id;
        }
    }
    for (const QString &id : std::as_const(combined)) {
        if (!done.contains(id)) {
            m_pending << id;
        }
    }
}

void AbstractGenerator::addDiscoveredJob(const QString &jobId)
{
    // Persist first so the job survives a crash between processReply and the next sync.
    QStringList discovered = settings().value(QStringLiteral("Discovered/ids")).toStringList();
    if (!discovered.contains(jobId)) {
        discovered << jobId;
        settings().setValue(QStringLiteral("Discovered/ids"), discovered);
        settings().sync();
    }

    // addDiscoveredJob() is only called from processReply(), which is only called from
    // recordReply(), which always calls loadState() first — so m_stateLoaded is true here.
    const QStringList done = settings().value(QStringLiteral("Done/ids")).toStringList();
    if (!done.contains(jobId) && !m_pending.contains(jobId)) {
        m_pending << jobId;
    }
}

QStringList AbstractGenerator::getAllJobIds() const
{
    return buildInitialJobIds();
}

int AbstractGenerator::pendingCount()
{
    loadState();
    return m_pending.size();
}

QString AbstractGenerator::getNextJob()
{
    loadState();
    if (m_jobIndex >= m_pending.size()) {
        return {};
    }

    const QString jobId = m_pending.at(m_jobIndex);
    ++m_jobIndex;

    qDebug() << getId() << "- job requested:" << jobId
             << "(" << (m_pending.size() - m_jobIndex) << "remaining)";

    QJsonObject payload = buildJobPayload(jobId);
    payload.insert(QStringLiteral("jobId"), jobId);
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

bool AbstractGenerator::recordReply(const QString &jsonReply)
{
    loadState();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonReply.toUtf8(), &err);
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString jobId = obj.value(QStringLiteral("jobId")).toString();
    if (jobId.isEmpty()) {
        return false;
    }

    const int idx = m_pending.indexOf(jobId);
    if (idx == -1) {
        return false;
    }

    processReply(jobId, obj); // may throw ExceptionWithTitleText

    qDebug() << getId() << "- reply recorded:" << jobId
             << "(" << (m_pending.size() - 1) << "remaining)";

    m_pending.removeAt(idx);
    if (idx < m_jobIndex) {
        --m_jobIndex;
    }

    QStringList done = settings().value(QStringLiteral("Done/ids")).toStringList();
    done << jobId;
    settings().setValue(QStringLiteral("Done/ids"), done);
    settings().sync();

    return true;
}

AbstractGenerator::Recorder::Recorder(AbstractGenerator *generator)
{
    getGenerators().insert(generator->getId(), generator);
}

const QMap<QString, const AbstractGenerator *> &AbstractGenerator::ALL_GENERATORS()
{
    return getGenerators();
}

QMap<QString, const AbstractGenerator *> &AbstractGenerator::getGenerators()
{
    static QMap<QString, const AbstractGenerator *> map;
    return map;
}

// ---- Parameters ------------------------------------------------------------

QList<AbstractGenerator::Param> AbstractGenerator::getParams() const
{
    return {};
}

QString AbstractGenerator::checkParams(const QList<Param> & /*params*/) const
{
    return {};
}

QVariant AbstractGenerator::paramValue(const QString &id) const
{
    const QVariant saved = settings().value(QStringLiteral("Params/") + id);
    if (saved.isValid()) {
        return saved;
    }
    for (const Param &p : getParams()) {
        if (p.id == id) {
            return p.defaultValue;
        }
    }
    return {};
}

void AbstractGenerator::saveParamValue(const QString &id, const QVariant &value)
{
    settings().setValue(QStringLiteral("Params/") + id, value);
    settings().sync();
    onParamChanged(id);
}

QList<AbstractGenerator::Param> AbstractGenerator::currentParams() const
{
    QList<Param> params = getParams();
    for (Param &p : params) {
        const QVariant saved = settings().value(QStringLiteral("Params/") + p.id);
        if (saved.isValid()) {
            p.defaultValue = saved;
        }
    }
    return params;
}

void AbstractGenerator::onParamChanged(const QString & /*id*/)
{
    // Default: no-op.
}

void AbstractGenerator::resetState()
{
    m_stateLoaded = false;
    m_pending.clear();
    m_jobIndex = 0;
}

// ---- Results table ---------------------------------------------------------

QMap<QString, AbstractPageAttributes *> AbstractGenerator::createResultPageAttributes() const
{
    return {};
}

DownloadedPagesTable *AbstractGenerator::openResultsTable()
{
    if (!m_resultsTables.isEmpty()) {
        return m_resultsTables.first();
    }

    const QMap<QString, AbstractPageAttributes *> attrMap = createResultPageAttributes();
    if (attrMap.isEmpty()) {
        return nullptr;
    }

    QDir resultsDir(m_workingDir.filePath(QStringLiteral("results_db")));
    resultsDir.mkpath(QStringLiteral("."));

    for (auto it = attrMap.constBegin(); it != attrMap.constEnd(); ++it) {
        AbstractPageAttributes *attrs = it.value();
        const QString tableId = attrs->getId();
        m_resultTableOrder << qMakePair(it.key(), tableId);
        m_resultsTables.insert(tableId,
            new DownloadedPagesTable(resultsDir, tableId, attrs, this));
    }
    return m_resultsTables.first();
}

QList<QPair<QString, DownloadedPagesTable *>> AbstractGenerator::openResultsTables()
{
    openResultsTable(); // ensure all tables are open
    QList<QPair<QString, DownloadedPagesTable *>> result;
    for (const auto &pair : std::as_const(m_resultTableOrder)) {
        result << qMakePair(pair.first, m_resultsTables.value(pair.second));
    }
    return result;
}

DownloadedPagesTable *AbstractGenerator::resultsTable() const
{
    return m_resultsTables.isEmpty() ? nullptr : m_resultsTables.first();
}

DownloadedPagesTable *AbstractGenerator::resultsTable(const QString &attrId) const
{
    return m_resultsTables.value(attrId, nullptr);
}

void AbstractGenerator::recordResultPage(const QString &attrId,
                                         const QHash<QString, QString> &attrs)
{
    DownloadedPagesTable *table = m_resultsTables.value(attrId, nullptr);
    if (table) {
        table->recordPage(attrs);
    }
}
