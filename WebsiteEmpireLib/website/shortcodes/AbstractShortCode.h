#ifndef ABSTRACTSHORTCODE_H
#define ABSTRACTSHORTCODE_H

#include "website/WebCodeAdder.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

/**
 * Base class for all shortcode implementations using the Recorder self-registration pattern.
 *
 * ShortCodes follow the syntax:
 *   [TAG key="value" key2=unquoted][/TAG]
 *   [TAG key="value"]inner content[/TAG]
 *
 * The primary entry point is addCode() (inherited from WebCodeAdder): it receives a
 * QStringView into the page buffer, parses and validates the shortcode, then appends
 * the generated HTML/CSS/JS. No copy of the input text is made.
 *
 * Subclasses must implement:
 *   - getTag()               — unique tag name (e.g. "VIDEO", "IMG", "LINK")
 *   - availableArguments()   — declares type-specific args, mandatory status,
 *                              and per-argument translatability
 *   - isArgumentValueValid() — semantic validation beyond mandatory/allowedValues
 *   - addCode()              — inherited from WebCodeAdder; calls parseAndValidate()
 *                              then builds the HTML/CSS/JS output
 *
 * Usage (in the .cpp file):
 *   DECLARE_SHORTCODE(ShortCodeVideo)
 *
 * Registers the shortcode into ALL_SHORTCODES() at static initialisation time.
 * Q_ASSERT fires if two shortcodes register the same tag.
 */
class AbstractShortCode : public WebCodeAdder
{
public:
    // -------------------------------------------------------------------------
    // Data types
    // -------------------------------------------------------------------------

    /**
     * Whether an argument value carries user-visible text that may need translation.
     *
     * - No       : never translated (e.g. URL, numeric id)
     * - Yes      : must always be translated
     * - Optional : translation is at the caller's discretion
     */
    enum class Translatable {
        No,
        Yes,
        Optional
    };

    /**
     * Declares one argument accepted by a shortcode.
     *
     * - mandatory:     missing argument raises ExceptionWithTitleText
     * - allowedValues: non-empty → value must be one of these strings; empty = any value
     * - translatable:  whether the value carries translatable text
     */
    struct ArgumentDef {
        QString      id;
        bool         mandatory     = false;
        QStringList  allowedValues; ///< empty = any value accepted
        Translatable translatable  = Translatable::No;
    };

    /** Parsed representation of one shortcode block. */
    struct ParsedShortCode {
        QString                 tag;
        QHash<QString, QString> arguments;
        QString                 innerContent;
    };

    // -------------------------------------------------------------------------
    // Public interface
    // -------------------------------------------------------------------------

    virtual ~AbstractShortCode() = default;

    /** Unique tag name this shortcode handles (e.g. "VIDEO"). */
    virtual QString getTag() const = 0;

    /** Returns argument definitions for this shortcode type. */
    virtual QList<ArgumentDef> availableArguments() const = 0;

    /**
     * Additional semantic validation for a single argument value, called after
     * mandatory/allowedValues checks have already passed.
     * Returns true if the value is acceptable.
     */
    virtual bool isArgumentValueValid(const QString &argId, const QString &value) const = 0;

    // addCode(QStringView, QString&, QString&, QString&, QSet<QString>&, QSet<QString>&) const
    // is declared pure virtual in WebCodeAdder and must be implemented by each
    // concrete shortcode. Implementations should call parseAndValidate() internally.
    // Use cssDoneIds / jsDoneIds to avoid emitting the same CSS / JS block twice.

    // -------------------------------------------------------------------------
    // Factory / registry
    // -------------------------------------------------------------------------

    /**
     * Returns the registered AbstractShortCode instance for the given tag name,
     * or nullptr if the tag is not registered.
     * Accepts a QStringView to avoid a copy when scanning page text.
     */
    static const AbstractShortCode *forTag(QStringView tag);

    /** Returns all registered shortcodes keyed by tag name. */
    static const QHash<QString, const AbstractShortCode *> &ALL_SHORTCODES();

    /**
     * Self-registration helper — construct one via DECLARE_SHORTCODE at file scope.
     * Q_ASSERT fires if the tag is already registered.
     */
    class Recorder
    {
    public:
        explicit Recorder(const AbstractShortCode *shortcode);
    };

protected:
    // -------------------------------------------------------------------------
    // Helpers for subclass addCode() implementations
    // -------------------------------------------------------------------------

    /**
     * Parses a raw shortcode view into a ParsedShortCode.
     * Raises ExceptionWithTitleText on syntax errors:
     *   - Unrecognised overall structure
     *   - Mismatched opening/closing tag
     *   - Duplicate argument key
     *
     * Supported value forms:
     *   key="double-quoted"  key='single-quoted'  key=unquotedToken
     *
     * Note: values must not contain an unescaped ] character.
     */
    static ParsedShortCode parse(QStringView text);

    /**
     * Convenience method that calls parse() then validate() in one step.
     * Subclasses should prefer this over calling the two separately.
     */
    ParsedShortCode parseAndValidate(QStringView text) const;

private:
    /**
     * Validates a ParsedShortCode against availableArguments().
     * Checks: mandatory presence → no unknown args → allowedValues → isArgumentValueValid().
     * Raises ExceptionWithTitleText on the first failing check.
     */
    void validate(const ParsedShortCode &parsed) const;
};

/**
 * Place at file scope in the .cpp of each concrete shortcode.
 * Instantiates the class and registers it via Recorder at static init time.
 */
#define DECLARE_SHORTCODE(NEW_CLASS)                                             \
    NEW_CLASS instance##NEW_CLASS;                                               \
    AbstractShortCode::Recorder recorder##NEW_CLASS { &instance##NEW_CLASS };

#endif // ABSTRACTSHORTCODE_H
