#ifndef UPDATESTRATEGYTREE_H
#define UPDATESTRATEGYTREE_H

#include <QDir>
#include <QList>
#include <QStandardItemModel>
#include <QString>

/**
 * JSON-persisted tree model for update strategies.
 *
 * Top-level items are strategy nodes; each strategy has one or more prompt
 * child nodes.  Strategy nodes carry the page-type / theme configuration;
 * prompt nodes carry the update instructions shown in PaneUpdate's editor.
 *
 * Persisted to update_strategies.json in the working directory.
 */
class UpdateStrategyTree : public QStandardItemModel
{
    Q_OBJECT
public:
    static constexpr int ROLE_ID           = Qt::UserRole + 1;
    static constexpr int ROLE_IS_STRATEGY  = Qt::UserRole + 2;
    static constexpr int ROLE_PAGE_TYPE_ID = Qt::UserRole + 3;
    static constexpr int ROLE_THEME_ID     = Qt::UserRole + 4;
    static constexpr int ROLE_NON_SVG      = Qt::UserRole + 5;
    static constexpr int ROLE_INSTRUCTIONS = Qt::UserRole + 6;
    static constexpr int ROLE_SAVE_KEY     = Qt::UserRole + 7;
    static constexpr int ROLE_SKIP_IF_SET  = Qt::UserRole + 8;

    // Column indices for the tree view
    static constexpr int COL_NAME       = 0;
    static constexpr int COL_UPDATE_SVG = 1;
    static constexpr int COL_UPDATE_IMG = 2;

    explicit UpdateStrategyTree(const QDir &workingDir, QObject *parent = nullptr);

    /** Appends a strategy top-level item and saves; returns its stable UUID. */
    QString addStrategy(const QString &name,
                        const QString &pageTypeId,
                        const QString &themeId,
                        bool           nonSvgImages);

    /**
     * Appends a prompt child to the strategy with strategyId and saves.
     * Returns the new prompt's UUID, or empty if strategyId is not found.
     * saveKey: prefixed data key to save output into (empty = legacy "1_text").
     * skipIfKeyNonEmpty: when true, pages whose saveKey value is already set are skipped.
     * updateSvg/updateImages: when true, the launcher constrains Claude to only modify
     * SVG blocks / image references respectively.
     */
    QString addPrompt(const QString &strategyId,
                      const QString &name,
                      const QString &instructions,
                      const QString &saveKey          = {},
                      bool           skipIfKeyNonEmpty = false,
                      bool           updateSvg         = false,
                      bool           updateImages      = false);

    /** Removes the item at index (strategy removal also removes all children). */
    void removeItem(const QModelIndex &index);

    /** Updates the instructions text for a prompt node and saves. */
    void setInstructions(const QModelIndex &index, const QString &instructions);

    void setSaveKey(const QModelIndex &index, const QString &saveKey);
    void setSkipIfKeyNonEmpty(const QModelIndex &index, bool skip);
    QString saveKeyFor(const QModelIndex &index) const;
    bool    skipIfKeyNonEmptyFor(const QModelIndex &index) const;
    bool    updateSvgFor(const QModelIndex &index) const;
    bool    updateImagesFor(const QModelIndex &index) const;

    // Overridden to auto-save when a checkbox column is toggled in the view.
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    // --- read-only accessors by model index ---

    QString nodeId(const QModelIndex &index) const;
    bool    isStrategy(const QModelIndex &index) const;

    /** pageTypeId of a strategy node, or of the parent strategy for a prompt node. */
    QString pageTypeIdFor(const QModelIndex &index) const;
    QString themeIdFor(const QModelIndex &index) const;
    bool    nonSvgImagesFor(const QModelIndex &index) const;
    QString instructionsFor(const QModelIndex &index) const;

    // --- data structures for the headless launcher ---

    struct PromptInfo {
        QString id;
        QString name;
        QString instructions;
        QString saveKey;               ///< prefixed data key to save output (empty = "1_text")
        bool    skipIfKeyNonEmpty = false;
        bool    updateSvg         = false; ///< Claude must only modify <svg> blocks
        bool    updateImages      = false; ///< Claude must only modify image references
    };
    struct StrategyInfo {
        QString id;
        QString name;
        QString pageTypeId;
        QString themeId;
        bool    nonSvgImages = false;
        QList<PromptInfo> prompts;
    };

    /** Returns a snapshot of all strategies with their prompts. */
    QList<StrategyInfo> strategies() const;

private:
    void           _load();
    void           _save() const;
    QStandardItem *_findStrategyItem(const QString &strategyId) const;

    QString m_filePath;
};

#endif // UPDATESTRATEGYTREE_H
