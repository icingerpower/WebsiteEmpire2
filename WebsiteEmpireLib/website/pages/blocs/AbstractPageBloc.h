#ifndef ABSTRACTPAGEBLOC_H
#define ABSTRACTPAGEBLOC_H

#include "website/WebCodeAdder.h"
#include "website/taxonomy/TaxonomyDescriptor.h"

#include <QDir>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class AbstractPageBlockWidget;
class AbstractAttribute;

/**
 * Base class for a page bloc: a self-contained unit that can both contribute
 * HTML/CSS/JS to a generated page (via WebCodeAdder::addCode) and provide a
 * Qt widget for editing its content in the IDE (via createEditWidget).
 *
 * Ownership of the returned widget is transferred to the caller.
 */
class AbstractPageBloc : public WebCodeAdder
{
public:
    virtual ~AbstractPageBloc() = default;

    /**
     * Returns the human-readable name of this bloc type, shown as a section
     * heading above the bloc's editor widget.  Must be translated via tr() or
     * QCoreApplication::translate() in the implementation.
     */
    virtual QString getName() const = 0;

    /**
     * Load this bloc's content from a flat key→value map.
     * Keys not recognised by this bloc are silently ignored (forward
     * compatibility: old data may contain keys for attributes that were
     * later removed from the bloc's definition).
     */
    virtual void load(const QHash<QString, QString> &values) = 0;

    /**
     * Save this bloc's content into a flat key→value map.
     * Keys must be stable identifiers — never change them once data exists
     * in the database.
     */
    virtual void save(QHash<QString, QString> &values) const = 0;

    /**
     * Create and return a widget that lets the user edit this bloc's content.
     * Ownership is transferred to the caller.
     */
    virtual AbstractPageBlockWidget *createEditWidget() = 0;

    /**
     * Returns per-key hints that the AI generation pipeline injects into the
     * JSON schema prompt so Claude knows how to fill each metadata field.
     *
     * Keys must be in the same un-prefixed namespace as save() — the caller
     * (AbstractPageType::collectAiKeyClues) adds the bloc-index prefix.
     *
     * Blocs whose fields are determined by the editor (not the AI) should
     * return an empty hash.  The default implementation does exactly that.
     *
     * Example for PageBlocCategory:
     *   { "categories", "Comma-separated IDs. Choose ONLY from: 1=Knee, 2=Shoulder" }
     *
     * The hint replaces the empty-string placeholder in the schema JSON, so
     * Claude sees the guidance inline with the field it must fill.
     */
    virtual QHash<QString, QString> getAiKeyClues() const;

    /**
     * Describes this bloc's optional AI-update capability.
     * Returns an engaged optional when this bloc can have its content rewritten
     * or populated by the AI update pipeline (LauncherUpdate).
     * Blocs that are not AI-updatable return std::nullopt (the default).
     */
    struct AiUpdateSpec {
        enum class Validator {
            ArticleFormat,            ///< must start with [TITLE level="1"], >= 2000 chars
            CommaSeparatedInts,       ///< one or more comma-separated positive integers
            NonEmpty,                 ///< any non-empty trimmed string
            CommaSeparatedVocabulary  ///< each comma-token must be in allowedValues (or empty string)
        };
        QString     dataKey;        ///< un-prefixed save/load key (e.g. "text", "categories")
        QString     displayName;    ///< human-readable label for the UI dropdown
        QString     formatPrompt;   ///< Call-2 instructions combined with aiKeyClue by the launcher
        Validator   validator = Validator::NonEmpty;
        QStringList allowedValues;  ///< used by CommaSeparatedVocabulary: accepted tokens
    };
    virtual std::optional<AiUpdateSpec> getAiUpdateSpec() const;

    /**
     * Returns the semantic attributes this bloc exposes for indexing and
     * faceted search (categories, properties, qualities).
     *
     * The returned list contains raw pointers to static const instances owned
     * by the concrete subclass — no allocation, no ownership transfer.
     * The default implementation returns a reference to a static empty list.
     * Subclasses that expose attributes must store the list as a member and
     * return a const reference to it to avoid per-call copies.
     */
    virtual const QList<const AbstractAttribute *> &getAttributes() const;

    /**
     * Returns true if this bloc requires a second AI generation pass that runs
     * after the main article generation (text + metadata + primary SVG) is
     * complete and saved.
     *
     * The default returns false.  AbstractSecondaryPageBloc overrides this to
     * return true and adds the pure virtual interface for the second pass.
     *
     * LauncherGeneration iterates the page type's blocs after reaching
     * PageGenerationState::MainImageReady and dispatches second-pass prompts
     * only to blocs for which this returns true.
     */
    virtual bool isSecondTimeGeneration() const;

    /**
     * Returns a descriptor if this bloc contributes items to a shared taxonomy
     * (e.g. symptom names). Returns empty optional by default.
     * PaneTaxonomies uses this to discover all taxonomies across all page types.
     */
    virtual std::optional<TaxonomyDescriptor> taxonomy() const { return {}; }

    /**
     * Reads vocabulary items from sourceDbPath (an aspire database) and writes
     * them into the local taxonomy store via TaxonomyDb in workingDir.
     * Called by PaneTaxonomies when the user clicks Sync.
     * Default is a no-op.
     */
    virtual void syncTaxonomy(const QString & /*sourceDbPath*/,
                              const QDir    & /*workingDir*/) const {}

    /**
     * Returns items from the local taxonomy store for use in the edit widget.
     * Called by createEditWidget() — never reads from aspire databases.
     * Default returns an empty list.
     */
    virtual QStringList loadTaxonomy(const QDir & /*workingDir*/) const { return {}; }
};

#endif // ABSTRACTPAGEBLOC_H
