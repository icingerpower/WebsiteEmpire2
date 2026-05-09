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
static const QString JSON_KEY_UPDATE_SVG   = QStringLiteral("updateSvg");
static const QString JSON_KEY_UPDATE_IMG   = QStringLiteral("updateImages");

static QStandardItem *makeCheckItem(bool checked)
{
    auto *item = new QStandardItem();
    item->setCheckable(true);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    return item;
}

static QStandardItem *makeInertItem()
{
    auto *item = new QStandardItem();
    item->setFlags(Qt::NoItemFlags);
    return item;
}

UpdateStrategyTree::UpdateStrategyTree(const QDir &workingDir, QObject *parent)
    : QStandardItemModel(parent)
    , m_filePath(workingDir.absoluteFilePath(QStringLiteral("update_strategies.json")))
{
    _load();
    // Set after _load() because clear() inside _load() resets column count and headers.
    setColumnCount(3);
    setHorizontalHeaderLabels({tr("Prompt"), tr("SVG"), tr("Img")});
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
    appendRow({item, makeInertItem(), makeInertItem()});
    _save();
    return id;
}

QString UpdateStrategyTree::addPrompt(const QString &strategyId,
                                       const QString &name,
                                       const QString &instructions,
                                       const QString &saveKey,
                                       bool           skipIfKeyNonEmpty,
                                       bool           updateSvg,
                                       bool           updateImages)
{
    QStandardItem *stratItem = _findStrategyItem(strategyId);
    if (!stratItem) {
        return {};
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *nameItem = new QStandardItem(name);
    nameItem->setData(id,                ROLE_ID);
    nameItem->setData(false,             ROLE_IS_STRATEGY);
    nameItem->setData(instructions,      ROLE_INSTRUCTIONS);
    nameItem->setData(saveKey,           ROLE_SAVE_KEY);
    nameItem->setData(skipIfKeyNonEmpty, ROLE_SKIP_IF_SET);
    stratItem->appendRow({nameItem, makeCheckItem(updateSvg), makeCheckItem(updateImages)});
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

bool UpdateStrategyTree::updateSvgFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }
    const auto *nameItem = itemFromIndex(index.sibling(index.row(), COL_NAME));
    if (!nameItem || nameItem->data(ROLE_IS_STRATEGY).toBool()) {
        return false;
    }
    const auto *svgItem = nameItem->parent()
        ? nameItem->parent()->child(nameItem->row(), COL_UPDATE_SVG)
        : item(nameItem->row(), COL_UPDATE_SVG);
    return svgItem ? svgItem->checkState() == Qt::Checked : false;
}

bool UpdateStrategyTree::updateImagesFor(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return false;
    }
    const auto *nameItem = itemFromIndex(index.sibling(index.row(), COL_NAME));
    if (!nameItem || nameItem->data(ROLE_IS_STRATEGY).toBool()) {
        return false;
    }
    const auto *imgItem = nameItem->parent()
        ? nameItem->parent()->child(nameItem->row(), COL_UPDATE_IMG)
        : item(nameItem->row(), COL_UPDATE_IMG);
    return imgItem ? imgItem->checkState() == Qt::Checked : false;
}

bool UpdateStrategyTree::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const bool ok = QStandardItemModel::setData(index, value, role);
    if (ok && role == Qt::CheckStateRole) {
        _save();
    }
    return ok;
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
            const auto *nameItem = stratItem->child(j, COL_NAME);
            if (!nameItem) {
                continue;
            }
            const auto *svgItem = stratItem->child(j, COL_UPDATE_SVG);
            const auto *imgItem = stratItem->child(j, COL_UPDATE_IMG);
            PromptInfo prompt;
            prompt.id                = nameItem->data(ROLE_ID).toString();
            prompt.name              = nameItem->text();
            prompt.instructions      = nameItem->data(ROLE_INSTRUCTIONS).toString();
            prompt.saveKey           = nameItem->data(ROLE_SAVE_KEY).toString();
            prompt.skipIfKeyNonEmpty = nameItem->data(ROLE_SKIP_IF_SET).toBool();
            prompt.updateSvg         = svgItem && svgItem->checkState() == Qt::Checked;
            prompt.updateImages      = imgItem && imgItem->checkState() == Qt::Checked;
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

            auto *nameItem = new QStandardItem(promptObj.value(JSON_KEY_NAME).toString());
            nameItem->setData(promptId,                                                   ROLE_ID);
            nameItem->setData(false,                                                      ROLE_IS_STRATEGY);
            nameItem->setData(promptObj.value(JSON_KEY_INSTRUCTIONS).toString(),          ROLE_INSTRUCTIONS);
            nameItem->setData(promptObj.value(JSON_KEY_SAVE_KEY).toString(QString{}),     ROLE_SAVE_KEY);
            nameItem->setData(promptObj.value(JSON_KEY_SKIP_IF_SET).toBool(false),        ROLE_SKIP_IF_SET);
            stratItem->appendRow({nameItem,
                makeCheckItem(promptObj.value(JSON_KEY_UPDATE_SVG).toBool(false)),
                makeCheckItem(promptObj.value(JSON_KEY_UPDATE_IMG).toBool(false))});
        }
        appendRow({stratItem, makeInertItem(), makeInertItem()});
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
            const auto *nameItem = stratItem->child(j, COL_NAME);
            if (!nameItem) {
                continue;
            }
            const auto *svgItem = stratItem->child(j, COL_UPDATE_SVG);
            const auto *imgItem = stratItem->child(j, COL_UPDATE_IMG);
            QJsonObject promptObj;
            promptObj.insert(JSON_KEY_ID,           nameItem->data(ROLE_ID).toString());
            promptObj.insert(JSON_KEY_NAME,         nameItem->text());
            promptObj.insert(JSON_KEY_INSTRUCTIONS, nameItem->data(ROLE_INSTRUCTIONS).toString());
            promptObj.insert(JSON_KEY_SAVE_KEY,     nameItem->data(ROLE_SAVE_KEY).toString());
            promptObj.insert(JSON_KEY_SKIP_IF_SET,  nameItem->data(ROLE_SKIP_IF_SET).toBool());
            promptObj.insert(JSON_KEY_UPDATE_SVG,   svgItem && svgItem->checkState() == Qt::Checked);
            promptObj.insert(JSON_KEY_UPDATE_IMG,   imgItem && imgItem->checkState() == Qt::Checked);
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
