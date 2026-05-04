#include "UpdateStrategyTree.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

static const QString JSON_KEY_ID           = QStringLiteral("id");
static const QString JSON_KEY_NAME         = QStringLiteral("name");
static const QString JSON_KEY_PAGE_TYPE    = QStringLiteral("pageTypeId");
static const QString JSON_KEY_THEME_ID     = QStringLiteral("themeId");
static const QString JSON_KEY_NON_SVG      = QStringLiteral("nonSvgImages");
static const QString JSON_KEY_PROMPTS      = QStringLiteral("prompts");
static const QString JSON_KEY_INSTRUCTIONS = QStringLiteral("instructions");
static const QString JSON_KEY_SAVE_KEY     = QStringLiteral("saveKey");
static const QString JSON_KEY_SKIP_IF_SET  = QStringLiteral("skipIfKeyNonEmpty");

UpdateStrategyTree::UpdateStrategyTree(const QDir &workingDir, QObject *parent)
    : QStandardItemModel(parent)
    , m_filePath(workingDir.absoluteFilePath(QStringLiteral("update_strategies.json")))
{
    _load();
}

// ---- Public API -------------------------------------------------------------

QString UpdateStrategyTree::addStrategy(const QString &name,
                                         const QString &pageTypeId,
                                         const QString &themeId,
                                         bool           nonSvgImages)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *item = new QStandardItem(name);
    item->setData(id,           ROLE_ID);
    item->setData(true,         ROLE_IS_STRATEGY);
    item->setData(pageTypeId,   ROLE_PAGE_TYPE_ID);
    item->setData(themeId,      ROLE_THEME_ID);
    item->setData(nonSvgImages, ROLE_NON_SVG);
    appendRow(item);
    _save();
    return id;
}

QString UpdateStrategyTree::addPrompt(const QString &strategyId,
                                       const QString &name,
                                       const QString &instructions,
                                       const QString &saveKey,
                                       bool           skipIfKeyNonEmpty)
{
    QStandardItem *stratItem = _findStrategyItem(strategyId);
    if (!stratItem) {
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *item = new QStandardItem(name);
    item->setData(id,                ROLE_ID);
    item->setData(false,             ROLE_IS_STRATEGY);
    item->setData(instructions,      ROLE_INSTRUCTIONS);
    item->setData(saveKey,           ROLE_SAVE_KEY);
    item->setData(skipIfKeyNonEmpty, ROLE_SKIP_IF_SET);
    stratItem->appendRow(item);
    _save();
    return id;
}

void UpdateStrategyTree::removeItem(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    auto *item = itemFromIndex(index);
    if (!item) {
        return;
    }
    auto *parent = item->parent();
    if (parent) {
        parent->removeRow(item->row());
    } else {
        removeRow(item->row());
    }
    _save();
}

void UpdateStrategyTree::setInstructions(const QModelIndex &index,
                                          const QString &instructions)
{
    if (!index.isValid()) {
        return;
    }
    auto *item = itemFromIndex(index);
    if (!item || item->data(ROLE_IS_STRATEGY).toBool()) {
        return;
    }
    if (item->data(ROLE_INSTRUCTIONS).toString() == instructions) {
        return;
    }
    item->setData(instructions, ROLE_INSTRUCTIONS);
    _save();
}

QString UpdateStrategyTree::nodeId(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto *item = itemFromIndex(index);
    return item ? item->data(ROLE_ID).toString() : QString{};
}

bool UpdateStrategyTree::isStrategy(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }
    const auto *item = itemFromIndex(index);
    return item ? item->data(ROLE_IS_STRATEGY).toBool() : false;
}

QString UpdateStrategyTree::pageTypeIdFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto *item = itemFromIndex(index);
    if (!item) {
        return {};
    }
    if (item->data(ROLE_IS_STRATEGY).toBool()) {
        return item->data(ROLE_PAGE_TYPE_ID).toString();
    }
    const auto *parent = item->parent();
    return parent ? parent->data(ROLE_PAGE_TYPE_ID).toString() : QString{};
}

QString UpdateStrategyTree::themeIdFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto *item = itemFromIndex(index);
    if (!item) {
        return {};
    }
    if (item->data(ROLE_IS_STRATEGY).toBool()) {
        return item->data(ROLE_THEME_ID).toString();
    }
    const auto *parent = item->parent();
    return parent ? parent->data(ROLE_THEME_ID).toString() : QString{};
}

bool UpdateStrategyTree::nonSvgImagesFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }
    const auto *item = itemFromIndex(index);
    if (!item) {
        return false;
    }
    if (item->data(ROLE_IS_STRATEGY).toBool()) {
        return item->data(ROLE_NON_SVG).toBool();
    }
    const auto *parent = item->parent();
    return parent ? parent->data(ROLE_NON_SVG).toBool() : false;
}

QString UpdateStrategyTree::instructionsFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto *item = itemFromIndex(index);
    return item ? item->data(ROLE_INSTRUCTIONS).toString() : QString{};
}

void UpdateStrategyTree::setSaveKey(const QModelIndex &index, const QString &saveKey)
{
    if (!index.isValid()) {
        return;
    }
    auto *item = itemFromIndex(index);
    if (!item || item->data(ROLE_IS_STRATEGY).toBool()) {
        return;
    }
    item->setData(saveKey, ROLE_SAVE_KEY);
    _save();
}

void UpdateStrategyTree::setSkipIfKeyNonEmpty(const QModelIndex &index, bool skip)
{
    if (!index.isValid()) {
        return;
    }
    auto *item = itemFromIndex(index);
    if (!item || item->data(ROLE_IS_STRATEGY).toBool()) {
        return;
    }
    item->setData(skip, ROLE_SKIP_IF_SET);
    _save();
}

QString UpdateStrategyTree::saveKeyFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }
    const auto *item = itemFromIndex(index);
    return item ? item->data(ROLE_SAVE_KEY).toString() : QString{};
}

bool UpdateStrategyTree::skipIfKeyNonEmptyFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }
    const auto *item = itemFromIndex(index);
    return item ? item->data(ROLE_SKIP_IF_SET).toBool() : false;
}

QList<UpdateStrategyTree::StrategyInfo> UpdateStrategyTree::strategies() const
{
    QList<StrategyInfo> result;
    result.reserve(rowCount());
    for (int i = 0; i < rowCount(); ++i) {
        const auto *stratItem = item(i);
        if (!stratItem) {
            continue;
        }
        StrategyInfo info;
        info.id           = stratItem->data(ROLE_ID).toString();
        info.name         = stratItem->text();
        info.pageTypeId   = stratItem->data(ROLE_PAGE_TYPE_ID).toString();
        info.themeId      = stratItem->data(ROLE_THEME_ID).toString();
        info.nonSvgImages = stratItem->data(ROLE_NON_SVG).toBool();

        info.prompts.reserve(stratItem->rowCount());
        for (int j = 0; j < stratItem->rowCount(); ++j) {
            const auto *promptItem = stratItem->child(j);
            if (!promptItem) {
                continue;
            }
            PromptInfo prompt;
            prompt.id                 = promptItem->data(ROLE_ID).toString();
            prompt.name               = promptItem->text();
            prompt.instructions       = promptItem->data(ROLE_INSTRUCTIONS).toString();
            prompt.saveKey            = promptItem->data(ROLE_SAVE_KEY).toString();
            prompt.skipIfKeyNonEmpty  = promptItem->data(ROLE_SKIP_IF_SET).toBool();
            info.prompts.append(prompt);
        }
        result.append(info);
    }
    return result;
}

// ---- Private ----------------------------------------------------------------

void UpdateStrategyTree::_load()
{
    clear();
    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) {
        return;
    }
    for (const QJsonValue &stratVal : doc.array()) {
        if (!stratVal.isObject()) {
            continue;
        }
        const QJsonObject stratObj = stratVal.toObject();
        QString stratId = stratObj.value(JSON_KEY_ID).toString();
        if (stratId.isEmpty()) {
            stratId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        auto *stratItem = new QStandardItem(stratObj.value(JSON_KEY_NAME).toString());
        stratItem->setData(stratId,                                        ROLE_ID);
        stratItem->setData(true,                                           ROLE_IS_STRATEGY);
        stratItem->setData(stratObj.value(JSON_KEY_PAGE_TYPE).toString(),  ROLE_PAGE_TYPE_ID);
        stratItem->setData(stratObj.value(JSON_KEY_THEME_ID).toString(),   ROLE_THEME_ID);
        stratItem->setData(stratObj.value(JSON_KEY_NON_SVG).toBool(false), ROLE_NON_SVG);

        const QJsonArray prompts = stratObj.value(JSON_KEY_PROMPTS).toArray();
        for (const QJsonValue &promptVal : std::as_const(prompts)) {
            if (!promptVal.isObject()) {
                continue;
            }
            const QJsonObject promptObj = promptVal.toObject();
            QString promptId = promptObj.value(JSON_KEY_ID).toString();
            if (promptId.isEmpty()) {
                promptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }

            auto *promptItem = new QStandardItem(promptObj.value(JSON_KEY_NAME).toString());
            promptItem->setData(promptId,                                                    ROLE_ID);
            promptItem->setData(false,                                                       ROLE_IS_STRATEGY);
            promptItem->setData(promptObj.value(JSON_KEY_INSTRUCTIONS).toString(),           ROLE_INSTRUCTIONS);
            promptItem->setData(promptObj.value(JSON_KEY_SAVE_KEY).toString(QString{}),      ROLE_SAVE_KEY);
            promptItem->setData(promptObj.value(JSON_KEY_SKIP_IF_SET).toBool(false),         ROLE_SKIP_IF_SET);
            stratItem->appendRow(promptItem);
        }
        appendRow(stratItem);
    }
}

void UpdateStrategyTree::_save() const
{
    QJsonArray arr;
    for (int i = 0; i < rowCount(); ++i) {
        const auto *stratItem = item(i);
        if (!stratItem) {
            continue;
        }
        QJsonObject stratObj;
        stratObj.insert(JSON_KEY_ID,        stratItem->data(ROLE_ID).toString());
        stratObj.insert(JSON_KEY_NAME,      stratItem->text());
        stratObj.insert(JSON_KEY_PAGE_TYPE, stratItem->data(ROLE_PAGE_TYPE_ID).toString());
        stratObj.insert(JSON_KEY_THEME_ID,  stratItem->data(ROLE_THEME_ID).toString());
        stratObj.insert(JSON_KEY_NON_SVG,   stratItem->data(ROLE_NON_SVG).toBool());

        QJsonArray promptsArr;
        for (int j = 0; j < stratItem->rowCount(); ++j) {
            const auto *promptItem = stratItem->child(j);
            if (!promptItem) {
                continue;
            }
            QJsonObject promptObj;
            promptObj.insert(JSON_KEY_ID,           promptItem->data(ROLE_ID).toString());
            promptObj.insert(JSON_KEY_NAME,         promptItem->text());
            promptObj.insert(JSON_KEY_INSTRUCTIONS, promptItem->data(ROLE_INSTRUCTIONS).toString());
            promptObj.insert(JSON_KEY_SAVE_KEY,     promptItem->data(ROLE_SAVE_KEY).toString());
            promptObj.insert(JSON_KEY_SKIP_IF_SET,  promptItem->data(ROLE_SKIP_IF_SET).toBool());
            promptsArr.append(promptObj);
        }
        stratObj.insert(JSON_KEY_PROMPTS, promptsArr);
        arr.append(stratObj);
    }

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "UpdateStrategyTree: failed to save" << m_filePath << ":" << file.errorString();
        return;
    }
    file.write(QJsonDocument(arr).toJson());
}

QStandardItem *UpdateStrategyTree::_findStrategyItem(const QString &strategyId) const
{
    for (int i = 0; i < rowCount(); ++i) {
        auto *stratItem = item(i);
        if (stratItem && stratItem->data(ROLE_ID).toString() == strategyId) {
            return stratItem;
        }
    }
    return nullptr;
}
