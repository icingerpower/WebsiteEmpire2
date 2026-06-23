#include "PaneTaxonomies.h"

#include "website/pages/AbstractPageType.h"
#include "website/pages/blocs/AbstractPageBloc.h"
#include "website/pages/attributes/CategoryTable.h"
#include "website/taxonomy/TaxonomyDb.h"
#include "website/taxonomy/TaxonomyDescriptor.h"
#include "website/taxonomy/TaxonomySettings.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

// =============================================================================
// Construction / destruction
// =============================================================================

PaneTaxonomies::PaneTaxonomies(QWidget *parent)
    : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    outer->addWidget(m_scrollArea);
}

PaneTaxonomies::~PaneTaxonomies() = default;

// =============================================================================
// setup
// =============================================================================

void PaneTaxonomies::setup(const QDir &workingDir)
{
    m_workingDir = workingDir;
    m_isSetup    = true;
    m_categoryTable.reset(new CategoryTable(workingDir));
}

// =============================================================================
// setVisible
// =============================================================================

void PaneTaxonomies::setVisible(bool visible)
{
    QWidget::setVisible(visible);
    if (!visible) {
        return;
    }
    if (m_isSetup && !m_built) {
        _discover();
        _buildCards();
    }
    if (m_isSetup) {
        _refreshStatus();
    }
}

// =============================================================================
// _discover
// =============================================================================

void PaneTaxonomies::_discover()
{
    m_entries.clear();

    const QList<QString> typeIds = AbstractPageType::allTypeIds();
    for (const QString &typeId : std::as_const(typeIds)) {
        std::unique_ptr<AbstractPageType> pageType =
            AbstractPageType::createForTypeId(typeId, *m_categoryTable);
        if (!pageType) {
            continue;
        }

        const QList<const AbstractPageBloc *> &blocs = pageType->getPageBlocs();
        for (const AbstractPageBloc *bloc : std::as_const(blocs)) {
            const std::optional<TaxonomyDescriptor> desc = bloc->taxonomy();
            if (!desc.has_value()) {
                continue;
            }

            // Check for duplicates by id.
            bool found = false;
            for (const TaxonomyEntry &e : m_entries) {
                if (e.descriptor.id == desc->id) {
                    found = true;
                    break;
                }
            }
            if (found) {
                continue;
            }

            TaxonomyEntry entry;
            entry.descriptor = *desc;
            entry.bloc       = bloc;
            entry.pageType   = std::move(pageType);
            m_entries.push_back(std::move(entry));
            break; // pageType was moved; advance to next type
        }
    }
}

// =============================================================================
// _buildCards
// =============================================================================

void PaneTaxonomies::_buildCards()
{
    m_content = new QWidget;
    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        TaxonomyEntry &entry = m_entries[static_cast<std::size_t>(i)];

        auto *card = new QFrame(m_content);
        card->setFrameShape(QFrame::StyledPanel);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 8, 8, 8);
        cardLayout->setSpacing(6);

        // Title
        auto *title = new QLabel(QStringLiteral("<b>") + entry.descriptor.displayName
                                 + QStringLiteral("</b>"), card);
        cardLayout->addWidget(title);

        // Source row
        auto *sourceRow = new QWidget(card);
        auto *sourceLayout = new QHBoxLayout(sourceRow);
        sourceLayout->setContentsMargins(0, 0, 0, 0);

        auto *sourceHeading = new QLabel(tr("Source DB:"), sourceRow);
        sourceLayout->addWidget(sourceHeading);

        entry.sourceLabel = new QLabel(sourceRow);
        entry.sourceLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        entry.sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sourceLayout->addWidget(entry.sourceLabel);

        auto *browseBtn = new QPushButton(tr("Browse…"), sourceRow);
        sourceLayout->addWidget(browseBtn);

        cardLayout->addWidget(sourceRow);

        // Status row
        entry.statusLabel = new QLabel(card);
        cardLayout->addWidget(entry.statusLabel);

        // Sync button
        auto *syncBtn = new QPushButton(tr("Sync"), card);
        cardLayout->addWidget(syncBtn);

        layout->addWidget(card);

        // Separator
        if (i < static_cast<int>(m_entries.size()) - 1) {
            auto *line = new QFrame(m_content);
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);
            layout->addWidget(line);
        }

        // Connect buttons (capture i by value).
        connect(browseBtn, &QPushButton::clicked, this, [this, i]() {
            _onBrowse(i);
        });
        connect(syncBtn, &QPushButton::clicked, this, [this, i]() {
            _onSync(i);
        });
    }

    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    m_scrollArea->setWidget(m_content);
    m_built = true;
}

// =============================================================================
// _refreshStatus
// =============================================================================

void PaneTaxonomies::_refreshStatus()
{
    TaxonomySettings settings(m_workingDir);
    for (TaxonomyEntry &entry : m_entries) {
        const QString sourcePath = settings.sourcePath(entry.descriptor.id);
        if (sourcePath.isEmpty()) {
            entry.sourceLabel->setText(tr("Not configured"));
        } else {
            // Show just the filename for brevity.
            entry.sourceLabel->setText(QFileInfo(sourcePath).fileName()
                                       + QStringLiteral(" (")
                                       + QFileInfo(sourcePath).absolutePath()
                                       + QStringLiteral(")"));
        }

        const int     count    = settings.itemCount(entry.descriptor.id);
        const QString lastSync = settings.lastSync(entry.descriptor.id);

        if (lastSync.isEmpty()) {
            entry.statusLabel->setText(tr("Never synced"));
        } else {
            entry.statusLabel->setText(
                tr("%1 items • Synced: %2").arg(count).arg(lastSync));
        }
    }
}

// =============================================================================
// _onBrowse
// =============================================================================

void PaneTaxonomies::_onBrowse(int entryIndex)
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select source aspire database"),
        QString(),
        QStringLiteral("SQLite databases (*.db)"));

    if (path.isEmpty()) {
        return;
    }

    TaxonomySettings settings(m_workingDir);
    settings.setSourcePath(m_entries[static_cast<std::size_t>(entryIndex)].descriptor.id, path);
    _refreshStatus();
}

// =============================================================================
// _onSync
// =============================================================================

void PaneTaxonomies::_onSync(int entryIndex)
{
    TaxonomyEntry &entry = m_entries[static_cast<std::size_t>(entryIndex)];
    TaxonomySettings settings(m_workingDir);

    const QString sourcePath = settings.sourcePath(entry.descriptor.id);
    if (sourcePath.isEmpty()) {
        QMessageBox::warning(this, tr("No source configured"),
                             tr("Please select a source database first."));
        return;
    }

    entry.bloc->syncTaxonomy(sourcePath, m_workingDir);

    const int count = TaxonomyDb(m_workingDir).count(entry.descriptor.id);
    settings.setLastSync(entry.descriptor.id,
                         QDateTime::currentDateTime().toString(Qt::ISODate));
    settings.setItemCount(entry.descriptor.id, count);

    _refreshStatus();

    QMessageBox::information(this, tr("Sync complete"),
                             tr("Synced %1 items for \"%2\".")
                             .arg(count)
                             .arg(entry.descriptor.displayName));
}
