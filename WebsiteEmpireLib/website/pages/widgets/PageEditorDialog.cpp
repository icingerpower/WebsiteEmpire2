#include "PageEditorDialog.h"
#include "ui_PageEditorDialog.h"

#include "DialogPermalinkHistory.h"
#include "website/pages/AbstractPageType.h"
#include "website/pages/IPageRepository.h"
#include "website/pages/PageRecord.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/blocs/widgets/AbstractPageBlockWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

// =============================================================================
// Constructor / Destructor
// =============================================================================

PageEditorDialog::PageEditorDialog(IPageRepository &repo,
                                   CategoryTable   &categoryTable,
                                   int              pageId,
                                   const QString   &editingLangCode,
                                   QWidget         *parent)
    : QDialog(parent)
    , ui(new Ui::PageEditorDialog)
    , m_repo(repo)
    , m_categoryTable(categoryTable)
    , m_pageId(pageId)
    , m_editingLangCode(editingLangCode)
{
    ui->setupUi(this);

    const bool isCreate = (pageId == -1);

    // Populate type combo.
    for (const QString &typeId : AbstractPageType::allTypeIds()) {
        auto tmp = AbstractPageType::createForTypeId(typeId, categoryTable);
        if (tmp) {
            ui->comboType->addItem(tmp->getDisplayName(), typeId);
        }
    }
    ui->comboType->setEnabled(isCreate);

    if (isCreate) {
        setWindowTitle(tr("New Page"));
        connect(ui->comboType, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &PageEditorDialog::_onTypeChanged);
        if (ui->comboType->count() > 0) {
            _onTypeChanged(0);
        }
    } else {
        setWindowTitle(tr("Edit Page"));

        // Show the history button in edit mode.
        ui->buttonPermalinkHistory->setVisible(true);
        connect(ui->buttonPermalinkHistory, &QToolButton::clicked,
                this, &PageEditorDialog::_onHistoryClicked);

        const auto &rec = m_repo.findById(pageId);
        if (rec) {
            const int idx = ui->comboType->findData(rec->typeId);
            if (idx >= 0) {
                ui->comboType->setCurrentIndex(idx);
            }
            ui->editPermalink->setText(rec->permalink);
            _loadBlocs(rec->typeId);

            const QHash<QString, QString> &data = m_repo.loadData(pageId);
            m_pageType->load(data);
            const auto &blocs = m_pageType->getPageBlocs();
            for (int i = 0; i < blocs.size() && i < m_blocWidgets.size(); ++i) {
                QHash<QString, QString> sub;
                blocs.at(i)->save(sub);
                m_blocWidgets.at(i)->load(sub);
            }

            // -----------------------------------------------------------------
            // Translation lock
            // -----------------------------------------------------------------
            if (rec->sourcePageId > 0) {
                const QString &tAt = m_repo.translatedAt(pageId);

                if (tAt.isEmpty()) {
                    // Not yet AI-translated: lock the editor.
                    m_locked = true;
                    ui->lblTranslationStatus->setText(
                        tr("This page has not been AI-translated yet. "
                           "Run Translate from the Pages pane before editing."));
                    ui->lblTranslationStatus->setStyleSheet(
                        QStringLiteral("QLabel { color: #8b0000; background: #fff0f0;"
                                       " padding: 6px; border-radius: 3px; }"));
                    ui->lblTranslationStatus->setVisible(true);
                    ui->scrollArea->setEnabled(false);
                    if (auto *okBtn = ui->buttonBox->button(QDialogButtonBox::Ok)) {
                        okBtn->setEnabled(false);
                    }
                } else {
                    // Check staleness: source updated after last translation.
                    const auto &srcRec = m_repo.findById(rec->sourcePageId);
                    if (srcRec && srcRec->updatedAt > tAt) {
                        ui->lblTranslationStatus->setText(
                            tr("The source page was updated (%1) after this translation "
                               "was last processed (%2). Consider re-running Translate.")
                                .arg(srcRec->updatedAt, tAt));
                        ui->lblTranslationStatus->setStyleSheet(
                            QStringLiteral("QLabel { color: #7a4900; background: #fffbe6;"
                                           " padding: 6px; border-radius: 3px; }"));
                        ui->lblTranslationStatus->setVisible(true);
                    }
                }
            }
        }
    }

    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &PageEditorDialog::_onAccepted);
}

PageEditorDialog::~PageEditorDialog()
{
    delete ui;
}

// =============================================================================
// Private helpers
// =============================================================================

void PageEditorDialog::_clearBlocs()
{
    QLayout *lay = ui->scrollContents->layout();
    while (lay->count() > 1) {
        QLayoutItem *item = lay->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_blocWidgets.clear();
    m_pageType.reset();
}

void PageEditorDialog::_loadBlocs(const QString &typeId)
{
    _clearBlocs();
    m_pageType = AbstractPageType::createForTypeId(typeId, m_categoryTable);
    if (!m_pageType) {
        return;
    }

    QLayout *lay = ui->scrollContents->layout();
    const auto &blocs = m_pageType->getPageBlocs();
    for (int i = 0; i < blocs.size(); ++i) {
        AbstractPageBloc *bloc = const_cast<AbstractPageBloc *>(blocs.at(i));
        AbstractPageBlockWidget *w = bloc->createEditWidget();

        auto *container = new QWidget(ui->scrollContents);
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->addWidget(new QLabel(bloc->getName(), container));
        w->setParent(container);
        containerLayout->addWidget(w);

        static_cast<QVBoxLayout *>(lay)->insertWidget(i, container);
        m_blocWidgets.append(w);
    }
}

// =============================================================================
// Slots
// =============================================================================

void PageEditorDialog::_onTypeChanged(int index)
{
    if (index < 0) {
        return;
    }
    const QVariant typeData = ui->comboType->itemData(index);
    const QString &typeId = typeData.toString();
    _loadBlocs(typeId);
}

void PageEditorDialog::_onAccepted()
{
    if (!m_pageType || m_locked) {
        return;
    }

    const QString editPermalinkText = ui->editPermalink->text();
    const QString &permalink = editPermalinkText.trimmed();
    const QVariant currentTypeData = ui->comboType->currentData();
    const QString &typeId    = currentTypeData.toString();

    const auto &blocs = m_pageType->getPageBlocs();
    for (int i = 0; i < blocs.size() && i < m_blocWidgets.size(); ++i) {
        QHash<QString, QString> sub;
        m_blocWidgets.at(i)->save(sub);
        const_cast<AbstractPageBloc *>(blocs.at(i))->load(sub);
    }

    QHash<QString, QString> data;
    m_pageType->save(data);

    if (m_pageId == -1) {
        const int newId = m_repo.create(typeId, permalink, m_editingLangCode);
        m_repo.saveData(newId, data);
    } else {
        m_repo.updatePermalink(m_pageId, permalink);
        // Preserve internal system keys (__ prefix) that blocs do not manage
        // (e.g. __legal_def_id).  saveData() replaces the entire page_data set,
        // so we must merge them back in or they would be silently lost.
        const QHash<QString, QString> &existing = m_repo.loadData(m_pageId);
        for (auto it = existing.cbegin(); it != existing.cend(); ++it) {
            if (it.key().startsWith(QStringLiteral("__"))) {
                data.insert(it.key(), it.value());
            }
        }
        m_repo.saveData(m_pageId, data);
    }

    accept();
}

void PageEditorDialog::_onHistoryClicked()
{
    if (m_pageId < 0) {
        return;
    }
    DialogPermalinkHistory dlg(m_repo, m_pageId, this);
    dlg.exec();
}
